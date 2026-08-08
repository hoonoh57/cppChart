#pragma once

#include "indicator_engine.h"

#include <cstddef>

namespace trading::indicators
{
    constexpr std::size_t ObvValueOutput = 0;
    constexpr std::size_t ObvSignalOutput = 1;
    constexpr std::size_t ObvDirectionOutput = 2;

    bool RegisterObvIndicator(IndicatorRegistry& registry);
}
