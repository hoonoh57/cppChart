#pragma once

#include "../core/intuitive_strength_engine.h"
#include "../core/stock_pool_engine.h"

#include <cstddef>
#include <string>

namespace trading::stock_pool::ui
{
    struct TickDetailUiState final
    {
        bool open = false;
        std::size_t memberIndex = 0U;
        bool showCandles = true;
        bool showFastJma = true;
        bool showSlowJma = true;
        bool showCrossSignals = true;
        bool showBuyEligible = true;
        bool showTradeMarkers = true;
        bool showSlope = true;
        bool showTickRate = true;
        bool showMacdAtr = true;
        bool showObv = true;
        double feePercentEachSide = 0.015;
        double sellTaxPercent = 0.15;
        double slippageBps = 2.0;
    };

    void OpenTickDetail(
        TickDetailUiState& state,
        std::size_t memberIndex) noexcept;

    // Returns true when a calculation parameter was edited and the caller
    // must rebuild the intuitive-strength series with the same market data.
    bool DrawTickDetailWindow(
        TickDetailUiState& state,
        const MemberSeries* member,
        const intuitive::MemberStrengthSeries* strength,
        intuitive::StrengthConfig& config,
        const std::string& tradingDate,
        int tickSize,
        int asOfMinute);
}
