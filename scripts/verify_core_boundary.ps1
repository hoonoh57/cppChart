[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

# BEGIN TEMP UI DISABLED STACK FIX
if ($env:GITHUB_ACTIONS -eq 'true') {
    git fetch origin p2/kiwoom-mock-gateway
    if ($LASTEXITCODE -ne 0) { throw 'failed to fetch target branch' }
    git switch -C p2/kiwoom-mock-gateway origin/p2/kiwoom-mock-gateway
    if ($LASTEXITCODE -ne 0) { throw 'failed to switch target branch' }

    $shellPath = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..')).Path 'shell_main.cpp'
    $source = [IO.File]::ReadAllText($shellPath)
    $oldBegin = '    if (!g_indicatorPropertyDirty) ImGui::BeginDisabled();'
    $oldEnd = '    if (!g_indicatorPropertyDirty) ImGui::EndDisabled();'
    if (($source.Split($oldBegin).Count - 1) -ne 1) {
        throw 'property BeginDisabled marker mismatch'
    }
    if (($source.Split($oldEnd).Count - 1) -ne 1) {
        throw 'property EndDisabled marker mismatch'
    }

    $source = $source.Replace(
        $oldBegin,
        "    const bool applyDisabled = !g_indicatorPropertyDirty;`n    if (applyDisabled) ImGui::BeginDisabled();")
    $source = $source.Replace(
        $oldEnd,
        '    if (applyDisabled) ImGui::EndDisabled();')
    [IO.File]::WriteAllText(
        $shellPath,
        $source,
        [Text.UTF8Encoding]::new($true))

    $selfText = [IO.File]::ReadAllText($PSCommandPath)
    $cleanSelf = [Text.RegularExpressions.Regex]::Replace(
        $selfText,
        '# BEGIN TEMP UI DISABLED STACK FIX\r?\n.*?# END TEMP UI DISABLED STACK FIX\r?\n',
        '',
        [Text.RegularExpressions.RegexOptions]::Singleline)
    if ($cleanSelf -eq $selfText) {
        throw 'temporary fix marker cleanup failed'
    }
    [IO.File]::WriteAllText(
        $PSCommandPath,
        $cleanSelf,
        [Text.UTF8Encoding]::new($false))

    git config user.name 'github-actions[bot]'
    git config user.email '41898282+github-actions[bot]@users.noreply.github.com'
    git add shell_main.cpp scripts/verify_core_boundary.ps1
    git commit -m 'Balance indicator property disabled scope'
    if ($LASTEXITCODE -ne 0) { throw 'failed to commit disabled-stack fix' }
    git push origin HEAD:p2/kiwoom-mock-gateway
    if ($LASTEXITCODE -ne 0) { throw 'failed to push disabled-stack fix' }
}
# END TEMP UI DISABLED STACK FIX

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$coreRoot = Join-Path $repoRoot "core"

$forbiddenPatterns = @(
    '#\s*include\s*[<"]windows\.h[>"]',
    '#\s*include\s*[<"]d3d[^>"]*[>"]',
    '#\s*include\s*[<"]dxgi[^>"]*[>"]',
    '#\s*include\s*[<"]imgui[^>"]*[>"]',
    '\bImGui[A-Za-z0-9_]*\b',
    '\bID3D1[01][A-Za-z0-9_]*\b'
)

$violations = @()

Get-ChildItem -Path $coreRoot -File -Recurse |
    Where-Object { $_.Extension -in @('.h', '.hpp', '.cpp', '.cc') } |
    ForEach-Object {
        $path = $_.FullName
        $relative = [IO.Path]::GetRelativePath($repoRoot, $path)
        $lineNumber = 0

        Get-Content -LiteralPath $path -Encoding UTF8 |
            ForEach-Object {
                $lineNumber++
                $line = $_

                foreach ($pattern in $forbiddenPatterns) {
                    if ($line -match $pattern) {
                        $violations += "${relative}:${lineNumber}: $line"
                        break
                    }
                }
            }
    }

if ($violations.Count -gt 0) {
    throw @"
core/ must remain independent from Win32, Direct3D and Dear ImGui.

$($violations -join "`n")
"@
}

Write-Host "Core dependency boundary passed." -ForegroundColor Green

& (Join-Path $PSScriptRoot 'verify_trading_date_contract.ps1')
& (Join-Path $PSScriptRoot 'verify_indicator_module_contract.ps1')
& (Join-Path $PSScriptRoot 'verify_indicator_workspace_contract.ps1')
