#pragma once

#include "intuitive_strength_engine.h"

#include <cstddef>
#include <string>
#include <vector>

namespace trading::stock_pool::intuitive
{
    struct TradeCostConfig final
    {
        double feePercentEachSide = 0.015;
        double sellTaxPercent = 0.15;
        double slippageBps = 2.0;
    };

    struct CrossWaveCapture final
    {
        std::size_t crossUpIndex = 0U;
        std::size_t crossDownIndex = 0U;
        EpochMillis crossUpTime = 0;
        EpochMillis crossDownTime = 0;
        double entryClose = 0.0;
        double exitClose = 0.0;
        double returnPercent = 0.0;
        bool closed = false;
    };

    struct CausalTrade final
    {
        std::size_t signalIndex = 0U;
        std::size_t entryIndex = 0U;
        std::size_t exitSignalIndex = 0U;
        std::size_t exitIndex = 0U;
        EpochMillis signalTime = 0;
        EpochMillis entryTime = 0;
        EpochMillis exitSignalTime = 0;
        EpochMillis exitTime = 0;
        double entryPrice = 0.0;
        double exitPrice = 0.0;
        double grossReturnPercent = 0.0;
        double netReturnPercent = 0.0;
        double mfePercent = 0.0;
        double maePercent = 0.0;
        bool closed = false;
    };

    struct MemberTradeEvaluation final
    {
        std::size_t memberIndex = 0U;
        std::string code;
        std::string name;
        double sessionReturnPercent = 0.0;
        double crossWaveCompoundPercent = 0.0;
        double executionGrossCompoundPercent = 0.0;
        double executionNetCompoundPercent = 0.0;
        double bestMfePercent = 0.0;
        double worstMaePercent = 0.0;
        int closedTradeCount = 0;
        bool hasOpenTrade = false;
        std::vector<CrossWaveCapture> waves;
        std::vector<CausalTrade> trades;
    };

    MemberTradeEvaluation EvaluateCausalJmaTrades(
        const MemberSeries& member,
        const MemberStrengthSeries& strength,
        const StrengthConfig& config,
        EpochMillis asOfTime,
        const TradeCostConfig& costs = TradeCostConfig{});
}
