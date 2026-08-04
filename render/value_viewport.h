#pragma once

namespace trading::render
{
    struct ValueViewport final
    {
        double minimum = 0.0;
        double maximum = 0.0;
        bool initialized = false;
        bool autoScale = true;

        double Span() const noexcept
        {
            return maximum > minimum
                ? maximum - minimum
                : 0.0;
        }
    };

    void ResetValueViewport(
        ValueViewport& viewport,
        double dataMinimum,
        double dataMaximum,
        double topPaddingFraction = 0.10,
        double bottomPaddingFraction = 0.06) noexcept;

    void FollowValueRange(
        ValueViewport& viewport,
        double dataMinimum,
        double dataMaximum,
        double topPaddingFraction = 0.10,
        double bottomPaddingFraction = 0.06) noexcept;

    void PanValueViewport(
        ValueViewport& viewport,
        double verticalPixels,
        double plotHeight) noexcept;

    void ZoomValueViewport(
        ValueViewport& viewport,
        double anchorRatioFromTop,
        double verticalPixels,
        double sensitivity = 0.01) noexcept;
}
