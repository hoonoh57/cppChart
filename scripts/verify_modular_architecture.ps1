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
    '.\render\render_document.h',
    '.\render\render_document.cpp',
    '.\render\market_chart_builder.h',
    '.\render\market_chart_builder.cpp',
    '.\ui\render_document_renderer.h',
    '.\ui\render_document_renderer.cpp'
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
    'static trading::render::RenderDocument g_mainRenderDocument'
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

Write-Host 'Major-feature modules and generic renderer boundary verified.'
