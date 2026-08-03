[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$requiredFiles = @(
    'app\indicator_module.h',
    'app\indicator_module.cpp',
    'app\indicator_render_adapter.h',
    'app\indicator_render_adapter.cpp',
    'app\chart_workspace_module.h',
    'app\chart_workspace_module.cpp',
    'tests\indicator_module_tests.cpp',
    'tests\indicator_module_revision_tests.cpp',
    'tests\indicator_render_adapter_tests.cpp',
    'tests\indicator_reference_adapter_tests.cpp',
    'tests\chart_workspace_indicator_tests.cpp'
)
foreach ($relative in $requiredFiles) {
    if (-not (Test-Path (Join-Path $repoRoot $relative))) {
        throw "Indicator module contract file is missing: $relative"
    }
}

$header = Get-Content (Join-Path $repoRoot 'app\indicator_module.h') -Raw
foreach ($marker in @(
    'class IndicatorModule final',
    'struct IndicatorMarketSource final',
    'std::shared_ptr<const std::vector<Bar>> completedBars',
    'std::shared_ptr<const std::vector<indicators::IndicatorValue>>',
    'FeatureMetrics metrics')) {
    if (-not $header.Contains($marker)) {
        throw "Indicator module public contract is missing: $marker"
    }
}

$implementation = Get-Content (
    Join-Path $repoRoot 'app\indicator_module.cpp') -Raw
foreach ($marker in @(
    'completedRevision_ != source.completedRevision',
    'indicator live timestamp changed without completed revision',
    'runtime.instance.Update(source.liveBar)',
    'runtime.completedValues',
    '++metrics_.mergedEventCount',
    '++calculationRevision_',
    'level == FeatureLevel::Visible ||')) {
    if (-not $implementation.Contains($marker)) {
        throw "Indicator module lifecycle/cache invariant is missing: $marker"
    }
}
if ($implementation.Contains('calculationRevision_ = 0;')) {
    throw 'Indicator calculation revision must remain monotonic across Off/reconfigure invalidation'
}

$adapterHeader = Get-Content (
    Join-Path $repoRoot 'app\indicator_render_adapter.h') -Raw
foreach ($marker in @(
    'struct IndicatorOutputBinding final',
    'struct IndicatorReferenceBinding final',
    'IndicatorRenderKind kind',
    'std::size_t outputIndex',
    'std::vector<IndicatorReferenceBinding> references',
    'class IndicatorRenderAdapter final',
    'render::RenderDocument& document')) {
    if (-not $adapterHeader.Contains($marker)) {
        throw "Generic indicator render binding contract is missing: $marker"
    }
}

$adapter = Get-Content (
    Join-Path $repoRoot 'app\indicator_render_adapter.cpp') -Raw
foreach ($marker in @(
    'binding.indicatorId',
    'binding.outputIndex',
    'binding.kind == IndicatorRenderKind::Line',
    'cache.completedIdentity',
    'line.points.SetShared(',
    'histogram.points.SetShared(',
    'RebuildLineCache(',
    'RebuildHistogramCache(',
    'for (const IndicatorReferenceBinding& reference',
    'pane->referenceLines.push_back(',
    'render::ValidateRenderDocument(document, validationError)')) {
    if (-not $adapter.Contains($marker)) {
        throw "Cached generic indicator render adapter is missing: $marker"
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

$renderContract = Get-Content (
    Join-Path $repoRoot 'render\render_document.h') -Raw
if (-not $renderContract.Contains('SharedTailSeries<LinePoint> points')) {
    throw 'LineSeries must share immutable completed history and a mutable live tail'
}

$workspaceHeader = Get-Content (
    Join-Path $repoRoot 'app\chart_workspace_module.h') -Raw
foreach ($marker in @(
    'std::uint64_t indicatorRevision = 0',
    'const IndicatorModuleSnapshot& indicatorSnapshot',
    'IndicatorRenderAdapter& indicatorAdapter',
    'IndicatorCompositeRevision(')) {
    if (-not $workspaceHeader.Contains($marker)) {
        throw "Indicator-aware chart workspace contract is missing: $marker"
    }
}

$workspace = Get-Content (
    Join-Path $repoRoot 'app\chart_workspace_module.cpp') -Raw
foreach ($marker in @(
    'indicatorAdapter->Apply(',
    'indicatorRevision_ == nextIndicatorRevision',
    'IndicatorCompositeRevision(',
    'indicatorSnapshot->level',
    'indicator render contribution failed')) {
    if (-not $workspace.Contains($marker)) {
        throw "Chart workspace indicator composition invariant is missing: $marker"
    }
}

$cacheTests = Get-Content (
    Join-Path $repoRoot 'tests\indicator_module_tests.cpp') -Raw
foreach ($marker in @(
    'completed SMA output pointer must be reused on live-tail updates',
    'completed VWAP output pointer must be reused on live-tail updates',
    'live timestamp changed without completed revision must fail',
    'completed-revision promotion must rebuild indicators',
    'Off indicator module must release calculated series')) {
    if (-not $cacheTests.Contains($marker)) {
        throw "Indicator module cache regression coverage is missing: $marker"
    }
}

$revisionTests = Get-Content (
    Join-Path $repoRoot 'tests\indicator_module_revision_tests.cpp') -Raw
foreach ($marker in @(
    'unchanged source must not advance calculation revision',
    'Off invalidation must advance calculation revision',
    'rebuilt indicator output must advance calculation revision')) {
    if (-not $revisionTests.Contains($marker)) {
        throw "Indicator module revision regression coverage is missing: $marker"
    }
}

$adapterTests = Get-Content (
    Join-Path $repoRoot 'tests\indicator_render_adapter_tests.cpp') -Raw
foreach ($marker in @(
    'NaN line gap must split into two finite line segments',
    'completed line render points must be reused on live updates',
    'completed histogram render points must be reused on live updates',
    'live-only update must preserve render structure revision',
    'missing indicator render source must fail closed')) {
    if (-not $adapterTests.Contains($marker)) {
        throw "Indicator render adapter regression coverage is missing: $marker"
    }
}

$referenceTests = Get-Content (
    Join-Path $repoRoot 'tests\indicator_reference_adapter_tests.cpp') -Raw
foreach ($marker in @(
    'line and reference IDs must share one uniqueness domain',
    'generic indicator reference lines must apply',
    'ADX 20 and 25 reference lines must be published',
    'reference-line contribution must advance structure revision')) {
    if (-not $referenceTests.Contains($marker)) {
        throw "Indicator reference-line regression coverage is missing: $marker"
    }
}

$workspaceTests = Get-Content (
    Join-Path $repoRoot 'tests\chart_workspace_indicator_tests.cpp') -Raw
foreach ($marker in @(
    'indicator revision change must publish a new document',
    'indicator-only update must reuse completed render points',
    'render plan revision must invalidate workspace document',
    'Off indicator snapshot must remove indicator contributions',
    'failed indicator contribution must retain last good document')) {
    if (-not $workspaceTests.Contains($marker)) {
        throw "Chart workspace indicator regression coverage is missing: $marker"
    }
}

$runAll = Get-Content (Join-Path $repoRoot 'tests\run_all.bat') -Raw
foreach ($marker in @(
    'indicator_module_tests.exe',
    'indicator_module_revision_tests.exe',
    'indicator_render_adapter_tests.exe',
    'indicator_reference_adapter_tests.exe',
    'chart_workspace_indicator_tests.exe',
    'app\indicator_module.cpp',
    'app\indicator_render_adapter.cpp',
    'app\chart_workspace_module.cpp')) {
    if (-not $runAll.Contains($marker)) {
        throw "Indicator module/adapter/workspace test is not in the complete suite: $marker"
    }
}

Write-Host 'Indicator module cache, generic line/histogram/reference adapter, and chart workspace composition contracts passed.' -ForegroundColor Green
