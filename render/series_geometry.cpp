#include "series_geometry.h"

#include <algorithm>
#include <cmath>

namespace trading::render
{
    float SeriesBodyWidth(
        float plotWidth,
        AxisCoordinate visibleSpan,
        float fillRatio,
        float minimumWidth,
        float maximumWidth) noexcept
    {
        if (!std::isfinite(plotWidth) || plotWidth <= 0.0f) {
            return (std::max)(0.0f, minimumWidth);
        }
        if (!std::isfinite(visibleSpan) || visibleSpan <= 0.0) {
            visibleSpan = 1.0;
        }
        fillRatio = (std::max)(0.05f, (std::min)(1.0f, fillRatio));
        minimumWidth = (std::max)(0.0f, minimumWidth);
        maximumWidth = (std::max)(minimumWidth, maximumWidth);

        const float slotPitch =
            plotWidth / static_cast<float>((std::max)(1.0, visibleSpan));
        return (std::min)(
            maximumWidth,
            (std::max)(minimumWidth, slotPitch * fillRatio));
    }
}
