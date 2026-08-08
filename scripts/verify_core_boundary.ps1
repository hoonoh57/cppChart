[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$coreRoot = Join-Path $repoRoot "core"

$indicatorManagerSource = Get-Content (
    Join-Path $repoRoot 'ui\indicator_manager_ui.cpp') -Raw
$disabledScopeMarkers = @(
    'const bool applyDisabled = !state.dirty;',
    'if (applyDisabled) ImGui::BeginDisabled();',
    'if (applyDisabled) ImGui::EndDisabled();'
)
foreach ($marker in $disabledScopeMarkers) {
    if (($indicatorManagerSource.Split($marker).Count - 1) -ne 1) {
        throw "Indicator manager disabled-scope marker must occur exactly once: $marker"
    }
}
foreach ($staleMarker in @(
    'if (!state.dirty) ImGui::BeginDisabled();',
    'if (!state.dirty) ImGui::EndDisabled();',
    'if (!g_indicatorPropertyDirty) ImGui::BeginDisabled();',
    'if (!g_indicatorPropertyDirty) ImGui::EndDisabled();')) {
    if ($indicatorManagerSource.Contains($staleMarker)) {
        throw "Indicator manager disabled scope re-reads mutable state: $staleMarker"
    }
}

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
