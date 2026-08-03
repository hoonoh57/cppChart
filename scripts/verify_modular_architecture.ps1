$ErrorActionPreference = 'Stop'

$requiredFiles = @(
    '.\docs\ARCHITECTURE_CONSTITUTION.md',
    '.\docs\MODULARIZATION_PLAN.md',
    '.\app\feature_registry.h',
    '.\app\feature_registry.cpp',
    '.\app\market_data_module.h',
    '.\app\market_data_module.cpp',
    '.\app\chart_workspace_module.h',
    '.\app\chart_workspace_module.cpp',
    '.\core\indicator_engine.h',
    '.\core\indicator_engine.cpp',
    '.\core\sma_indicator.h',
    '.\core\sma_indicator.cpp',
    '.\render\chart_viewport.h',
    '.\render\chart_viewport.cpp',
    '.\render\time_axis.h',
    '.\render\time_axis.cpp',
    '.\render\render_document.h',
    '.\render\render_document.cpp',
    '.\render\value_grid.h',
    '.\render\value_grid.cpp',
    '.\render\series_geometry.h',
    '.\render\series_geometry.cpp',
    '.\render\cursor_label_layout.h',
    '.\render\market_chart_builder.h',
    '.\render\market_chart_builder.cpp',
    '.\ui\render_document_renderer.h',
    '.\ui\render_document_renderer.cpp',
    '.\tests\indicator_engine_tests.cpp'
)

foreach ($file in $requiredFiles) {
    if (-not (Test-Path $file)) {
        throw "Required modular architecture file is missing: $file"
    }
}

$shell = Get-Content '.\shell_main.cpp' -Raw
$requiredShellMarkers = @(
    'app/market_data_module.h',
    'app/feature_registry.h',
    'render/market_chart_builder.h',
    'ui/render_document_renderer.h',
    'MarketDataModule g_marketDataModule',
    'ChartWorkspaceModule g_chartWorkspaceModule',
    'FeatureRegistry g_featureRegistry',
    'DrawRenderDocument'
)
foreach ($marker in $requiredShellMarkers) {
    if (-not $shell.Contains($marker)) {
        throw "Shell is not using modular architecture marker: $marker"
    }
}

$forbiddenShellMarkers = @(
    'struct MarketDataView final',
    'static std::mutex g_marketDataMutex',
    'static void ApplyStockTradeTick(',
    'static void ApplyMinuteBars(',
    'static void DrawRealCandles(',
    'static trading::render::RenderDocument g_mainRenderDocument',
    'CopyVisibleBars(visibleLimit)',
    'const std::vector<trading::Bar> visibleBars'
)
foreach ($marker in $forbiddenShellMarkers) {
    if ($shell.Contains($marker)) {
        throw "Market-data or renderer implementation remains in shell_main.cpp: $marker"
    }
}

$renderer = Get-Content '.\ui\render_document_renderer.cpp' -Raw
$forbiddenRendererMarkers = @(
    'Kiwoom',
    'ka10080',
    '0B',
    'StockTradeTick',
    'OrderIntent',
    'TradingState',
    'SMA',
    'JMA',
    'VWAP',
    'Strategy'
)
foreach ($marker in $forbiddenRendererMarkers) {
    if ($renderer.Contains($marker)) {
        throw "Generic renderer contains feature-specific dependency: $marker"
    }
}

$renderContract = Get-Content '.\render\render_document.h' -Raw
$forbiddenContractMarkers = @(
    'windows.h',
    'd3d11.h',
    'imgui.h',
    'winhttp.h',
    'kiwoom'
)
foreach ($marker in $forbiddenContractMarkers) {
    if ($renderContract.ToLowerInvariant().Contains($marker)) {
        throw "Render contract contains platform or broker dependency: $marker"
    }
}

$requiredSharedTailMarkers = @(
    'class SharedTailSeries final',
    'SetShared(',
    'SharedPrefix()',
    'HasLiveTail()'
)
foreach ($marker in $requiredSharedTailMarkers) {
    if (-not $renderContract.Contains($marker)) {
        throw "Shared immutable history/live-tail render contract is missing: $marker"
    }
}

$timeAxis = Get-Content '.\render\time_axis.cpp' -Raw
$requiredTimeAxisMarkers = @(
    'OrdinalTimeAxis::Reset',
    'CoordinateForTimestamp',
    'TimestampForCoordinate',
    'timestamps must be strictly increasing'
)
foreach ($marker in $requiredTimeAxisMarkers) {
    if (-not $timeAxis.Contains($marker)) {
        throw "Compressed ordinal time-axis contract is missing: $marker"
    }
}

$requiredRendererAxisMarkers = @(
    'OrdinalTimeAxis',
    'DefaultVisibleSpan',
    'timeAxisRevision',
    'CoordinateForTimestamp',
    'TimestampForCoordinate'
)
foreach ($marker in $requiredRendererAxisMarkers) {
    if (-not $renderer.Contains($marker)) {
        throw "Renderer is not using the compressed trading-time axis: $marker"
    }
}
if ($renderer.Contains('timestamp - range.minimum')) {
    throw 'Renderer still maps wall-clock elapsed time directly to horizontal pixels'
}

$marketModule = Get-Content '.\app\market_data_module.cpp' -Raw
$requiredMarketMarkers = @(
    'completedBars_',
    'liveBar_',
    'completedRevision_',
    'SeriesSnapshot()',
    'liveWindow.reserve(2)'
)
foreach ($marker in $requiredMarketMarkers) {
    if (-not $marketModule.Contains($marker)) {
        throw "MarketDataModule live-tail split is missing: $marker"
    }
}

$requiredInteractionMarkers = @(
    'ImGuiButtonFlags_MouseButtonLeft',
    'ImGuiButtonFlags_MouseButtonRight',
    'draggingLeft || draggingRight',
    'QuantizeValue(',
    'SeriesBodyWidth(',
    'DrawCursorTimeLabel(',
    'PlaceCenteredHorizontalLabel('
)
foreach ($marker in $requiredInteractionMarkers) {
    if (-not $renderer.Contains($marker)) {
        throw "Chart interaction contract is missing: $marker"
    }
}
if ($renderer.Contains('static_cast<double>(nearest->close)')) {
    throw 'Horizontal crosshair must not be forced to nearest candle close'
}

$indicatorContract = Get-Content '.\core\indicator_engine.h' -Raw
$requiredIndicatorContractMarkers = @(
    'struct IndicatorSpec final',
    'class IndicatorInstance final',
    'class IndicatorRegistry final',
    'SerializeIndicatorSpec(',
    'ParseIndicatorSpec(',
    'CalculateBatch('
)
foreach ($marker in $requiredIndicatorContractMarkers) {
    if (-not $indicatorContract.Contains($marker)) {
        throw "Reusable indicator contract is missing: $marker"
    }
}

$smaIndicator = Get-Content '.\core\sma_indicator.cpp' -Raw
$requiredSmaMarkers = @(
    'RegisterSmaIndicator',
    'bar.closeTimestampMs == state.latestTimestampMs',
    'state.sum += close - state.window[state.latestIndex]',
    'IndicatorFault::TimestampMovedBackward'
)
foreach ($marker in $requiredSmaMarkers) {
    if (-not $smaIndicator.Contains($marker)) {
        throw "Incremental SMA contract is missing: $marker"
    }
}

$indicatorTests = Get-Content '.\tests\indicator_engine_tests.cpp' -Raw
$requiredIndicatorTestMarkers = @(
    'batch/incremental value parity mismatch',
    'same-timestamp update must replace the mutable live tail',
    'failed batch calculation must not expose partial results'
)
foreach ($marker in $requiredIndicatorTestMarkers) {
    if (-not $indicatorTests.Contains($marker)) {
        throw "Indicator parity regression coverage is missing: $marker"
    }
}

$runAll = Get-Content '.\tests\run_all.bat' -Raw
foreach ($marker in @(
    'indicator_engine_tests.exe',
    'core\indicator_engine.cpp',
    'core\sma_indicator.cpp')) {
    if (-not $runAll.Contains($marker)) {
        throw "Indicator engine is not in the complete headless suite: $marker"
    }
}

Write-Host 'Major-feature modules, M6 chart contracts, and the M7 batch/incremental SMA foundation are verified.'
