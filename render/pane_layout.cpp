#include "pane_layout.h"

#include <algorithm>
#include <cmath>

namespace trading::render
{
    bool AdjustAdjacentPaneWeights(
        float availableHeight,
        float minimumPaneHeight,
        float totalWeight,
        float deltaPixels,
        float& upperWeight,
        float& lowerWeight) noexcept
    {
        if (!std::isfinite(availableHeight) || availableHeight <= 0.0f ||
            !std::isfinite(minimumPaneHeight) || minimumPaneHeight < 0.0f ||
            !std::isfinite(totalWeight) || totalWeight <= 0.0f ||
            !std::isfinite(deltaPixels) ||
            !std::isfinite(upperWeight) || upperWeight <= 0.0f ||
            !std::isfinite(lowerWeight) || lowerWeight <= 0.0f)
        {
            return false;
        }

        const float upperHeight =
            availableHeight * upperWeight / totalWeight;
        const float lowerHeight =
            availableHeight * lowerWeight / totalWeight;
        const float minimumDelta = minimumPaneHeight - upperHeight;
        const float maximumDelta = lowerHeight - minimumPaneHeight;
        const float clampedDelta = (std::max)(
            minimumDelta,
            (std::min)(maximumDelta, deltaPixels));
        if (std::fabs(clampedDelta) < 0.001f) return false;

        const float deltaWeight =
            clampedDelta / availableHeight * totalWeight;
        const float originalPairWeight = upperWeight + lowerWeight;
        upperWeight += deltaWeight;
        lowerWeight -= deltaWeight;

        if (upperWeight <= 0.0f || lowerWeight <= 0.0f) return false;
        const float adjustedPairWeight = upperWeight + lowerWeight;
        if (std::fabs(adjustedPairWeight - originalPairWeight) > 0.0001f) {
            lowerWeight += originalPairWeight - adjustedPairWeight;
        }
        return true;
    }
}
