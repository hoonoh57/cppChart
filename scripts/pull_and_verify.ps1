[CmdletBinding()]
param(
    [string]$Branch = "main",
    [switch]$Launch
)

$ErrorActionPreference = "Stop"

# Windows PowerShell 5.1은 BOM 없는 UTF-8 스크립트를 시스템 ANSI로 해석할 수 있다.
# 이 파일은 UTF-8 BOM으로 저장하며, 자식 프로세스와 콘솔 출력도 UTF-8로 고정한다.
$utf8 = New-Object System.Text.UTF8Encoding($false)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8

function Assert-NativeSuccess {
    param([string]$Operation)
    if ($LASTEXITCODE -ne 0) {
        throw "$Operation 실패, exit code=$LASTEXITCODE"
    }
}

function Read-DotEnvValues {
    param([string]$Path)

    $values = @{}
    if (-not (Test-Path -LiteralPath $Path)) {
        return $values
    }

    $text = [System.IO.File]::ReadAllText($Path)
    $lineNumber = 0
    foreach ($rawLine in ($text -split "`r?`n")) {
        $lineNumber += 1
        $line = $rawLine.Trim()
        if (-not $line -or $line.StartsWith('#')) {
            continue
        }
        if ($line.StartsWith('export ')) {
            $line = $line.Substring(7).Trim()
        }

        $separator = $line.IndexOf('=')
        if ($separator -lt 1) {
            throw "$Path 환경설정 $lineNumber 줄 형식 오류"
        }

        $key = $line.Substring(0, $separator).Trim()
        $value = $line.Substring($separator + 1).Trim()
        if ($value.Length -ge 2) {
            $first = $value[0]
            $last = $value[$value.Length - 1]
            if (($first -eq '"' -and $last -eq '"') -or
                ($first -eq "'" -and $last -eq "'")) {
                $value = $value.Substring(1, $value.Length - 2)
            }
        }
        $values[$key] = $value
    }

    return $values
}

function Test-TruthyValue {
    param([string]$Value)
    if ($null -eq $Value) {
        return $false
    }
    $normalized = $Value.Trim().ToUpperInvariant()
    return @('1', 'TRUE', 'YES', 'Y', 'ON') -contains $normalized
}

function Assert-KiwoomLaunchConfig {
    param([string]$RepoRoot)

    $candidatePaths = New-Object System.Collections.Generic.List[string]
    if ($env:TRADING_CONFIG) {
        $candidatePaths.Add([System.IO.Path]::GetFullPath($env:TRADING_CONFIG))
    }
    $candidatePaths.Add((Join-Path $RepoRoot '.env'))
    $candidatePaths.Add((Join-Path (Split-Path $RepoRoot -Parent) '.env'))

    $configPath = $null
    $values = @{}
    foreach ($candidate in $candidatePaths) {
        if (Test-Path -LiteralPath $candidate) {
            $configPath = (Resolve-Path -LiteralPath $candidate).Path
            $values = Read-DotEnvValues -Path $configPath
            break
        }
    }

    $overrideKeys = @(
        'TRADING_MODE',
        'KIWOOM_MOCK',
        'KIWOOM_MOCK_APP_KEY',
        'KIWOOM_MOCK_SECRET_KEY',
        'KIWOOM_APP_KEY',
        'KIWOOM_SECRET_KEY',
        'KIWOOM_ACCOUNT',
        'KIWOOM_REST_BASE_URL',
        'KIWOOM_WEBSOCKET_URL'
    )
    foreach ($key in $overrideKeys) {
        $environmentValue = [Environment]::GetEnvironmentVariable($key)
        if ($environmentValue) {
            $values[$key] = $environmentValue
        }
    }

    $canonicalMode = ''
    if ($values.ContainsKey('TRADING_MODE')) {
        $canonicalMode = [string]$values['TRADING_MODE']
    }
    $legacyMode = ''
    if ($values.ContainsKey('KIWOOM_MOCK')) {
        $legacyMode = [string]$values['KIWOOM_MOCK']
    }

    $modeValid = $canonicalMode.Trim().ToUpperInvariant() -eq 'KIWOOM_MOCK'
    if (-not $modeValid) {
        $modeValid = Test-TruthyValue -Value $legacyMode
    }

    if (-not $modeValid) {
        $source = if ($configPath) { $configPath } else { '환경변수 또는 .env 없음' }
        throw "키움 모의투자 모드 설정이 없습니다. source=$source, TRADING_MODE=KIWOOM_MOCK 또는 KIWOOM_MOCK=true가 필요합니다."
    }

    $appKey = ''
    if ($values.ContainsKey('KIWOOM_MOCK_APP_KEY')) {
        $appKey = [string]$values['KIWOOM_MOCK_APP_KEY']
    }
    if (-not $appKey -and $values.ContainsKey('KIWOOM_APP_KEY')) {
        $appKey = [string]$values['KIWOOM_APP_KEY']
    }

    $secretKey = ''
    if ($values.ContainsKey('KIWOOM_MOCK_SECRET_KEY')) {
        $secretKey = [string]$values['KIWOOM_MOCK_SECRET_KEY']
    }
    if (-not $secretKey -and $values.ContainsKey('KIWOOM_SECRET_KEY')) {
        $secretKey = [string]$values['KIWOOM_SECRET_KEY']
    }

    if (-not $appKey -or -not $secretKey) {
        $source = if ($configPath) { $configPath } else { '프로세스 환경변수' }
        throw "키움 모의투자 App Key 또는 Secret Key가 비어 있습니다. source=$source"
    }

    $displaySource = if ($configPath) { $configPath } else { '프로세스 환경변수' }
    Write-Host "`n=== 키움 설정 사전검사 ===" -ForegroundColor Cyan
    Write-Host "config_source=$displaySource"
    Write-Host "mode=KIWOOM_MOCK"
    Write-Host "app_key=set"
    Write-Host "secret_key=set"
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot

if (-not (Test-Path ".git")) {
    throw "Git 저장소가 아닙니다: $RepoRoot"
}

$dirty = @(git status --porcelain)
if ($dirty.Count -gt 0) {
    throw "로컬 변경사항이 있어 pull을 중단합니다.`n$($dirty -join "`n")"
}

Get-Process -Name shell -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

Write-Host "`n=== 원격 갱신 ===" -ForegroundColor Cyan
git fetch origin --prune --tags
Assert-NativeSuccess "git fetch"

git show-ref --verify --quiet "refs/remotes/origin/$Branch"
if ($LASTEXITCODE -ne 0) {
    throw "원격 브랜치가 없습니다: origin/$Branch"
}

git show-ref --verify --quiet "refs/heads/$Branch"
$localExists = ($LASTEXITCODE -eq 0)

if ($localExists) {
    git switch $Branch
    Assert-NativeSuccess "브랜치 전환"
}

if (-not $localExists) {
    git switch --track -c $Branch "origin/$Branch"
    Assert-NativeSuccess "추적 브랜치 생성"
}

git pull --ff-only origin $Branch
Assert-NativeSuccess "git pull --ff-only"

Write-Host "`n=== 빌드 검증 ===" -ForegroundColor Cyan
cmd /c .\build.bat
if ($LASTEXITCODE -ne 0) {
    throw "빌드 실패"
}

git diff --check
Assert-NativeSuccess "git diff --check"

$dirtyAfter = @(git status --porcelain)
if ($dirtyAfter.Count -gt 0) {
    throw "빌드 후 추적 파일이 변경됐습니다.`n$($dirtyAfter -join "`n")"
}

Write-Host "`n=== pull 및 검증 완료 ===" -ForegroundColor Green
git status
git log --oneline --decorate -6

if ($Launch) {
    Assert-KiwoomLaunchConfig -RepoRoot $RepoRoot
    Start-Process -FilePath (Join-Path $RepoRoot "shell.exe") -WorkingDirectory $RepoRoot
}
