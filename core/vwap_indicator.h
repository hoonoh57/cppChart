#pragma once

#include "indicator_engine.h"

#include <cstddef>

namespace trading::indicators
{
    constexpr std::size_t VwapValueOutput = 0;
    constexpr std::size_t VwapUpper1Output = 1;
    constexpr std::size_t VwapLower1Output = 2;
    constexpr std::size_t VwapUpper2Output = 3;
    constexpr std::size_t VwapLower2Output = 4;

    bool RegisterVwapIndicator(IndicatorRegistry& registry);
}
