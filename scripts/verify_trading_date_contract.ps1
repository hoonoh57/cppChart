[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$requiredFiles = @(
    'core\market_types.h',
    'core\vwap_indicator.h',
    'core\vwap_indicator.cpp',
    'tests\trading_date_tests.cpp',
    'tests\market_trading_date_tests.cpp',
    'tests\vwap_indicator_tests.cpp'
)
foreach ($relative in $requiredFiles) {
    if (-not (Test-Path (Join-Path $repoRoot $relative))) {
        throw "Trading-date contract file is missing: $relative"
    }
}

$marketTypes = Get-Content (Join-Path $repoRoot 'core\market_types.h') -Raw
foreach ($marker in @(
    'using TradingDateYmd',
    'IsValidTradingDateYmd(',
    'KstTradingDateYmdFromEpoch(',
    'ResolveTradingDate(',
    'TradingDateYmd tradingDateYmd')) {
    if (-not $marketTypes.Contains($marker)) {
        throw "Explicit trading-date value contract is missing: $marker"
    }
}

$vwap = Get-Content (Join-Path $repoRoot 'core\vwap_indicator.cpp') -Raw
foreach ($marker in @(
    'IsValidTradingDateYmd(bar.tradingDateYmd)',
    'bar.tradingDateYmd != state.lastTradingDateYmd',
    'priceSquaredVolume',
    'VwapValueOutput',
    'VwapUpper1Output',
    'VwapLower1Output',
    'VwapUpper2Output',
    'VwapLower2Output')) {
    if (-not $vwap.Contains($marker)) {
        throw "Session VWAP contract is missing: $marker"
    }
}

$marketTests = Get-Content (
    Join-Path $repoRoot 'tests\market_trading_date_tests.cpp') -Raw
foreach ($marker in @(
    'REST minute bar must normalize first KST trading date',
    'REST minute bar must normalize second KST trading date',
    'real-time created bar must inherit normalized KST trading date')) {
    if (-not $marketTests.Contains($marker)) {
        throw "Market trading-date integration coverage is missing: $marker"
    }
}

$vwapTests = Get-Content (
    Join-Path $repoRoot 'tests\vwap_indicator_tests.cpp') -Raw
foreach ($marker in @(
    'trading-date change must reset VWAP',
    'missing VWAP TradingDate must fail closed',
    'same-timestamp VWAP update must replace the live tail')) {
    if (-not $vwapTests.Contains($marker)) {
        throw "VWAP trading-date regression coverage is missing: $marker"
    }
}

$runAll = Get-Content (Join-Path $repoRoot 'tests\run_all.bat') -Raw
foreach ($marker in @(
    'trading_date_tests.exe',
    'market_trading_date_tests.exe',
    'vwap_indicator_tests.exe')) {
    if (-not $runAll.Contains($marker)) {
        throw "Trading-date test is not in the complete suite: $marker"
    }
}

Write-Host 'Explicit KST trading-date propagation and session VWAP contracts passed.' -ForegroundColor Green
