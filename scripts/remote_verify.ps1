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

& (Join-Path $PSScriptRoot "verify_core_boundary.ps1")

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

$testCommand = "`"$devCmd`" -no_logo -arch=x64 -host_arch=x64 && call tests\run_all.bat"
& cmd.exe /d /s /c $testCommand
Assert-NativeSuccess "Headless tests"

git diff --check
Assert-NativeSuccess "git diff --check"

Write-Host "Remote verification passed." -ForegroundColor Green
