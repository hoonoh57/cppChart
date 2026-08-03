#pragma once

namespace trading::render
{
    using AxisCoordinate = double;

    struct ChartViewport final
    {
        AxisCoordinate visibleStart = 0.0;
        AxisCoordinate visibleEnd = 0.0;
        bool initialized = false;
        bool autoScroll = true;

        AxisCoordinate Span() const noexcept
        {
            return visibleEnd > visibleStart
                ? visibleEnd - visibleStart
                : 0.0;
        }
    };

    void ResetViewport(
        ChartViewport& viewport,
        AxisCoordinate dataStart,
        AxisCoordinate dataEnd,
        AxisCoordinate preferredSpan = 0.0) noexcept;

    void FollowLatest(
        ChartViewport& viewport,
        AxisCoordinate dataStart,
        AxisCoordinate dataEnd) noexcept;

    void ClampViewport(
        ChartViewport& viewport,
        AxisCoordinate dataStart,
        AxisCoordinate dataEnd,
        AxisCoordinate minimumSpan) noexcept;

    void ZoomViewport(
        ChartViewport& viewport,
        AxisCoordinate dataStart,
        AxisCoordinate dataEnd,
        double anchorRatio,
        double wheelSteps,
        AxisCoordinate minimumSpan) noexcept;

    void PanViewport(
        ChartViewport& viewport,
        AxisCoordinate dataStart,
        AxisCoordinate dataEnd,
        double visibleSpanFraction) noexcept;
}
