[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$requiredFiles = @(
    'app\indicator_module.h',
    'app\indicator_module.cpp',
    'app\indicator_render_adapter.h',
    'app\indicator_render_adapter.cpp',
    'tests\indicator_module_tests.cpp',
    'tests\indicator_module_revision_tests.cpp',
    'tests\indicator_render_adapter_tests.cpp'
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
    'IndicatorRenderKind kind',
    'std::size_t outputIndex',
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

$runAll = Get-Content (Join-Path $repoRoot 'tests\run_all.bat') -Raw
foreach ($marker in @(
    'indicator_module_tests.exe',
    'indicator_module_revision_tests.exe',
    'indicator_render_adapter_tests.exe',
    'app\indicator_module.cpp',
    'app\indicator_render_adapter.cpp')) {
    if (-not $runAll.Contains($marker)) {
        throw "Indicator module/adapter test is not in the complete suite: $marker"
    }
}

Write-Host 'Indicator module execution-level, cache, monotonic revision, and generic cached render adapter contracts passed.' -ForegroundColor Green
