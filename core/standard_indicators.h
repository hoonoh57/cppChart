#pragma once

#include "indicator_engine.h"

#include <cstddef>

namespace trading::indicators
{
    constexpr std::size_t EmaValueOutput = 0U;

    constexpr std::size_t BollingerMiddleOutput = 0U;
    constexpr std::size_t BollingerUpperOutput = 1U;
    constexpr std::size_t BollingerLowerOutput = 2U;

    constexpr std::size_t RsiValueOutput = 0U;

    constexpr std::size_t MacdValueOutput = 0U;
    constexpr std::size_t MacdSignalOutput = 1U;
    constexpr std::size_t MacdHistogramOutput = 2U;

    constexpr std::size_t DmiPlusOutput = 0U;
    constexpr std::size_t DmiMinusOutput = 1U;
    constexpr std::size_t DmiAdxOutput = 2U;

    constexpr std::size_t SuperTrendValueOutput = 0U;
    constexpr std::size_t SuperTrendUpOutput = 1U;
    constexpr std::size_t SuperTrendDownOutput = 2U;

    bool RegisterEmaIndicator(IndicatorRegistry& registry);
    bool RegisterBollingerIndicator(IndicatorRegistry& registry);
    bool RegisterRsiIndicator(IndicatorRegistry& registry);
    bool RegisterMacdIndicator(IndicatorRegistry& registry);
    bool RegisterDmiIndicator(IndicatorRegistry& registry);
    bool RegisterSuperTrendIndicator(IndicatorRegistry& registry);
}
