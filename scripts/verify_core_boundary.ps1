[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"


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
