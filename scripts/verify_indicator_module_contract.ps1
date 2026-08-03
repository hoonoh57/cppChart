[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$requiredFiles = @(
    'app\indicator_module.h',
    'app\indicator_module.cpp',
    'tests\indicator_module_tests.cpp',
    'tests\indicator_module_revision_tests.cpp'
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

$runAll = Get-Content (Join-Path $repoRoot 'tests\run_all.bat') -Raw
foreach ($marker in @(
    'indicator_module_tests.exe',
    'indicator_module_revision_tests.exe',
    'app\indicator_module.cpp')) {
    if (-not $runAll.Contains($marker)) {
        throw "Indicator module test is not in the complete suite: $marker"
    }
}

Write-Host 'Indicator module execution-level, cache, and monotonic revision contracts passed.' -ForegroundColor Green
