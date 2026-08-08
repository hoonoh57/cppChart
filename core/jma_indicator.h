#pragma once

#include "indicator_engine.h"

#include <cstddef>

namespace trading::indicators
{
    constexpr std::size_t JmaValueOutput = 0;
    constexpr std::size_t JmaUpOutput = 1;
    constexpr std::size_t JmaDownOutput = 2;
    constexpr std::size_t JmaSlopeOutput = 3;

    bool RegisterJmaIndicator(IndicatorRegistry& registry);
}
