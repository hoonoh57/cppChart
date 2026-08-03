[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

foreach ($relative in @(
    'app\indicator_render_contributor.h',
    'app\indicator_render_contributor.cpp',
    'tests\indicator_render_contributor_tests.cpp')) {
    if (Test-Path (Join-Path $repoRoot $relative)) {
        throw "Duplicate type-switched indicator render path must not exist: $relative"
    }
}

$workspaceHeader = Get-Content (
    Join-Path $repoRoot 'app\chart_workspace_module.h') -Raw
foreach ($marker in @(
    'const IndicatorModuleSnapshot& indicatorSnapshot',
    'const IndicatorRenderAdapter& indicatorAdapter',
    'bool NeedsUpdate(',
    'IndicatorCompositeRevision(')) {
    if (-not $workspaceHeader.Contains($marker)) {
        throw "Indicator-aware workspace update contract is missing: $marker"
    }
}

$workspace = Get-Content (
    Join-Path $repoRoot 'app\chart_workspace_module.cpp') -Raw
foreach ($marker in @(
    'indicatorAdapter->Apply(',
    'IsVisibleLevel(indicatorSnapshot->level)',
    'IndicatorCompositeRevision(',
    'static_cast<std::uint64_t>(snapshot.level)',
    'static_cast<std::uint64_t>(snapshot.state)')) {
    if (-not $workspace.Contains($marker)) {
        throw "Indicator-aware workspace revision/lifecycle invariant is missing: $marker"
    }
}

$tests = Get-Content (
    Join-Path $repoRoot 'tests\chart_workspace_indicator_tests.cpp') -Raw
foreach ($marker in @(
    'empty indicator-aware workspace must need an update',
    'unchanged indicator snapshot and plan must not rebuild',
    'indicator calculation revision must invalidate workspace',
    'render-plan revision must invalidate workspace',
    'indicator execution-level change must invalidate workspace',
    'Visible indicator reactivation must invalidate market-only document')) {
    if (-not $tests.Contains($marker)) {
        throw "Indicator-aware workspace invalidation coverage is missing: $marker"
    }
}

$runAll = Get-Content (Join-Path $repoRoot 'tests\run_all.bat') -Raw
if (-not $runAll.Contains('chart_workspace_indicator_tests.exe')) {
    throw 'Indicator-aware chart workspace test is not in the complete suite'
}

Write-Host 'Single cached indicator renderer and indicator-aware workspace revision contracts passed.' -ForegroundColor Green
