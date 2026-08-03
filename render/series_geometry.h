#pragma once

#include "chart_viewport.h"

namespace trading::render
{
    float SeriesBodyWidth(
        float plotWidth,
        AxisCoordinate visibleSpan,
        float fillRatio = 0.58f,
        float minimumWidth = 1.0f,
        float maximumWidth = 18.0f) noexcept;
}
