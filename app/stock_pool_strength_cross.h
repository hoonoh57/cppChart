#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../core/stock_pool_engine.h"

namespace trading::stock_pool::strategy
{
    enum class StrengthCrossExitReason
    {
        TakeProfit,
        StopLoss,
        SessionClose
    };

    struct StrengthCrossProfile final
    {
        double entryStrength = 100.0;
        double takeProfitPercent = 1.0;
        double stopLossPercent = 1.0;
        bool stopFirstWhenBothTouched = true;
    };

    struct StrengthCrossSummary final
    {
        BacktestResult backtest;
        std::size_t upwardCrossCount = 0U;
        std::size_t takeProfitCount = 0U;
        std::size_t stopLossCount = 0U;
        std::size_t sessionCloseCount = 0U;
        std::string contract;
    };

    bool IsUpwardStrengthCross(
        double previousStrength,
        double currentStrength,
        double threshold) noexcept;

    StrengthCrossSummary RunStrengthCrossBacktest(
        const std::vector<MemberSeries>& members,
        int rankingTopM,
        const ScoringProfile& scoringProfile,
        const StrengthCrossProfile& strategyProfile);
}
