#pragma once

namespace trading::render
{
    bool AdjustAdjacentPaneWeights(
        float availableHeight,
        float minimumPaneHeight,
        float totalWeight,
        float deltaPixels,
        float& upperWeight,
        float& lowerWeight) noexcept;
}
