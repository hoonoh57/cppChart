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
    Start-Process -FilePath (Join-Path $RepoRoot "shell.exe") -WorkingDirectory $RepoRoot
}
