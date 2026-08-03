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

$adapterHeader = Get-Content (
    Join-Path $repoRoot 'app\indicator_render_adapter.h') -Raw
foreach ($marker in @(
    'struct IndicatorOutputBinding final',
    'struct IndicatorReferenceBinding final',
    'std::vector<IndicatorReferenceBinding> references')) {
    if (-not $adapterHeader.Contains($marker)) {
        throw "Generic indicator render binding type is missing: $marker"
    }
}

$adapter = Get-Content (
    Join-Path $repoRoot 'app\indicator_render_adapter.cpp') -Raw
foreach ($marker in @(
    'for (const IndicatorReferenceBinding& reference : plan.references)',
    'pane->referenceLines.push_back(',
    'duplicate indicator render element id',
    'line.points.SetShared(',
    'histogram.points.SetShared(')) {
    if (-not $adapter.Contains($marker)) {
        throw "Generic cached indicator render implementation is missing: $marker"
    }
}
foreach ($forbidden in @(
    'series.spec.type == "SMA"',
    'series.spec.type == "JMA"',
    'series.spec.type == "VWAP"',
    'series.spec.type == "OBV"',
    'series.spec.type == "ADX"')) {
    if ($adapter.Contains($forbidden)) {
        throw "Indicator render adapter contains a central type switch: $forbidden"
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

$adapterTests = Get-Content (
    Join-Path $repoRoot 'tests\indicator_render_adapter_tests.cpp') -Raw
foreach ($marker in @(
    'reference-line id must not collide with a series id',
    'generic indicator reference-line contribution is missing',
    'reference line must remain stable on live-only updates',
    'completed line render points must be reused on live updates',
    'completed histogram render points must be reused on live updates')) {
    if (-not $adapterTests.Contains($marker)) {
        throw "Generic indicator render adapter coverage is missing: $marker"
    }
}

$runAll = Get-Content (Join-Path $repoRoot 'tests\run_all.bat') -Raw
foreach ($marker in @(
    'indicator_render_adapter_tests.exe',
    'chart_workspace_indicator_tests.exe')) {
    if (-not $runAll.Contains($marker)) {
        throw "Indicator render/workspace test is not in the complete suite: $marker"
    }
}

Write-Host 'Single cached indicator renderer, generic reference lines, and indicator-aware workspace revision contracts passed.' -ForegroundColor Green
