#pragma once

#include "indicator_engine.h"

#include <cstddef>

namespace trading::indicators
{
    constexpr std::size_t AdxValueOutput = 0;

    bool RegisterAdxIndicator(IndicatorRegistry& registry);
}
