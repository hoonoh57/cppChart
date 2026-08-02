[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

function Assert-NativeSuccess {
    param([string]$Operation)
    if ($LASTEXITCODE -ne 0) {
        throw "$Operation failed, exit code=$LASTEXITCODE"
    }
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere `
    -latest `
    -products "*" `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (-not $installation) {
    throw "Visual Studio C++ toolchain not found"
}

$devCmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"
$buildCommand = "`"$devCmd`" -no_logo -arch=x64 -host_arch=x64 && call build.bat"

& cmd.exe /d /s /c $buildCommand
Assert-NativeSuccess "Trading shell build"

if (-not (Test-Path ".\shell.exe")) {
    throw "shell.exe was not produced"
}

$coreSources = @(
    "tests\core_tests.cpp",
    "core\command_bus.cpp",
    "core\fault_policy.cpp",
    "core\json_lite.cpp",
    "core\parameter_store.cpp",
    "core\trading_state.cpp"
) -join " "

$testCommand = @"
`"$devCmd`" -no_logo -arch=x64 -host_arch=x64 && cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc $coreSources /Fe:core_tests.exe && core_tests.exe
"@

& cmd.exe /d /s /c $testCommand.Trim()
Assert-NativeSuccess "Core tests"

git diff --check
Assert-NativeSuccess "git diff --check"

Write-Host "Remote verification passed." -ForegroundColor Green
