#pragma once

#include <algorithm>
#include <cmath>

namespace trading::render
{
    struct HorizontalLabelPlacement final
    {
        float left = 0.0f;
        float right = 0.0f;
    };

    inline HorizontalLabelPlacement PlaceCenteredHorizontalLabel(
        float anchorX,
        float requestedWidth,
        float minimumX,
        float maximumX) noexcept
    {
        if (!std::isfinite(minimumX)) minimumX = 0.0f;
        if (!std::isfinite(maximumX) || maximumX < minimumX) {
            maximumX = minimumX;
        }
        const float available = maximumX - minimumX;
        if (!std::isfinite(requestedWidth) || requestedWidth < 0.0f) {
            requestedWidth = 0.0f;
        }
        const float width = (std::min)(requestedWidth, available);
        if (!std::isfinite(anchorX)) anchorX = minimumX;

        const float unclampedLeft = anchorX - width * 0.5f;
        const float maximumLeft = maximumX - width;
        const float left = (std::max)(
            minimumX,
            (std::min)(maximumLeft, unclampedLeft));
        return { left, left + width };
    }
}
