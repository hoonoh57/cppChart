$ErrorActionPreference = 'Stop'

$productionFiles = @(
    '.\shell_main.cpp',
    '.\core\runtime_config.h',
    '.\core\runtime_config.cpp',
    '.\core\kiwoom_session.cpp',
    '.\core\kiwoom_runtime_engine.h',
    '.\core\kiwoom_runtime_engine.cpp',
    '.\core\kiwoom_market_data.h',
    '.\core\kiwoom_market_data.cpp'
)

$forbiddenMarkers = @(
    'MakeMockData',
    'MockFeedThread',
    'std::mt19937',
    'normal_distribution',
    'ApplyLocalFill',
    'g_localOrderSequence',
    'RuntimeMode::LocalMock',
    '[LOCAL MOCK]',
    '효성중공업'
)

foreach ($file in $productionFiles) {
    $content = Get-Content $file -Raw
    foreach ($marker in $forbiddenMarkers) {
        if ($content.Contains($marker)) {
            throw "Synthetic production marker '$marker' remains in $file"
        }
    }
}

$shell = Get-Content '.\shell_main.cpp' -Raw
$requiredMarkers = @(
    'CPPCHART_REAL_DATA_ONLY',
    'MarketDataState::Error',
    '합성 데이터는 제거되었으며 오류를 숨기지 않습니다',
    'CanSubmitEntryOrders',
    'CanSubmitLiquidationOrders',
    'RequestStockMinuteBars',
    'ka10080'
)

foreach ($marker in $requiredMarkers) {
    if (-not $shell.Contains($marker)) {
        throw "Required real-data-only marker '$marker' is missing"
    }
}

Write-Host 'Production runtime contains no synthetic market data, positions, fills, or random feed.'
