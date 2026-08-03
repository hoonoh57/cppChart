#include "chart_viewport.h"

#include <algorithm>
#include <cmath>

namespace trading::render
{
    namespace
    {
        AxisCoordinate SafeDataSpan(
            AxisCoordinate dataStart,
            AxisCoordinate dataEnd) noexcept
        {
            return
                std::isfinite(dataStart) &&
                std::isfinite(dataEnd) &&
                dataEnd > dataStart
                    ? dataEnd - dataStart
                    : 0.0;
        }

        AxisCoordinate ClampSpan(
            AxisCoordinate requested,
            AxisCoordinate dataSpan,
            AxisCoordinate minimumSpan) noexcept
        {
            if (dataSpan <= 0.0) return 0.0;
            const AxisCoordinate safeMinimum = (std::max)(0.001, minimumSpan);
            return (std::max)(
                (std::min)(requested, dataSpan),
                (std::min)(safeMinimum, dataSpan));
        }
    }

    void ResetViewport(
        ChartViewport& viewport,
        AxisCoordinate dataStart,
        AxisCoordinate dataEnd,
        AxisCoordinate preferredSpan) noexcept
    {
        const AxisCoordinate dataSpan = SafeDataSpan(dataStart, dataEnd);
        if (dataSpan <= 0.0) {
            viewport = {};
            return;
        }

        const AxisCoordinate span =
            preferredSpan > 0.0
                ? ClampSpan(preferredSpan, dataSpan, 0.001)
                : dataSpan;
        viewport.visibleEnd = dataEnd;
        viewport.visibleStart = dataEnd - span;
        viewport.initialized = true;
        viewport.autoScroll = true;
    }

    void FollowLatest(
        ChartViewport& viewport,
        AxisCoordinate dataStart,
        AxisCoordinate dataEnd) noexcept
    {
        const AxisCoordinate dataSpan = SafeDataSpan(dataStart, dataEnd);
        if (dataSpan <= 0.0) {
            viewport = {};
            return;
        }
        if (!viewport.initialized) {
            ResetViewport(viewport, dataStart, dataEnd);
            return;
        }
        if (!viewport.autoScroll) {
            ClampViewport(viewport, dataStart, dataEnd, 0.001);
            return;
        }

        const AxisCoordinate span = ClampSpan(
            viewport.Span(),
            dataSpan,
            0.001);
        viewport.visibleEnd = dataEnd;
        viewport.visibleStart = dataEnd - span;
        viewport.initialized = true;
    }

    void ClampViewport(
        ChartViewport& viewport,
        AxisCoordinate dataStart,
        AxisCoordinate dataEnd,
        AxisCoordinate minimumSpan) noexcept
    {
        const AxisCoordinate dataSpan = SafeDataSpan(dataStart, dataEnd);
        if (dataSpan <= 0.0) {
            viewport = {};
            return;
        }
        if (!viewport.initialized) {
            ResetViewport(viewport, dataStart, dataEnd);
            return;
        }

        const AxisCoordinate span = ClampSpan(
            viewport.Span(),
            dataSpan,
            minimumSpan);
        AxisCoordinate start = viewport.visibleStart;
        AxisCoordinate end = start + span;

        if (start < dataStart) {
            start = dataStart;
            end = start + span;
        }
        if (end > dataEnd) {
            end = dataEnd;
            start = end - span;
        }

        viewport.visibleStart = start;
        viewport.visibleEnd = end;
        viewport.initialized = true;
    }

    void ZoomViewport(
        ChartViewport& viewport,
        AxisCoordinate dataStart,
        AxisCoordinate dataEnd,
        double anchorRatio,
        double wheelSteps,
        AxisCoordinate minimumSpan) noexcept
    {
        const AxisCoordinate dataSpan = SafeDataSpan(dataStart, dataEnd);
        if (
            dataSpan <= 0.0 ||
            !std::isfinite(wheelSteps) ||
            wheelSteps == 0.0)
        {
            return;
        }
        if (!viewport.initialized) {
            ResetViewport(viewport, dataStart, dataEnd);
        }

        anchorRatio = (std::max)(0.0, (std::min)(1.0, anchorRatio));
        const AxisCoordinate oldSpan = ClampSpan(
            viewport.Span(),
            dataSpan,
            minimumSpan);
        const double factor = std::pow(0.80, wheelSteps);
        const AxisCoordinate newSpan = ClampSpan(
            oldSpan * factor,
            dataSpan,
            minimumSpan);
        const AxisCoordinate anchor =
            viewport.visibleStart + oldSpan * anchorRatio;

        viewport.visibleStart = anchor - newSpan * anchorRatio;
        viewport.visibleEnd = viewport.visibleStart + newSpan;
        viewport.autoScroll =
            viewport.visibleEnd >= dataEnd - (std::max)(0.001, newSpan / 100.0);
        ClampViewport(
            viewport,
            dataStart,
            dataEnd,
            minimumSpan);
    }

    void PanViewport(
        ChartViewport& viewport,
        AxisCoordinate dataStart,
        AxisCoordinate dataEnd,
        double visibleSpanFraction) noexcept
    {
        const AxisCoordinate dataSpan = SafeDataSpan(dataStart, dataEnd);
        if (
            dataSpan <= 0.0 ||
            !std::isfinite(visibleSpanFraction) ||
            visibleSpanFraction == 0.0)
        {
            return;
        }
        if (!viewport.initialized) {
            ResetViewport(viewport, dataStart, dataEnd);
        }

        const AxisCoordinate span = ClampSpan(
            viewport.Span(),
            dataSpan,
            0.001);
        const AxisCoordinate delta = span * visibleSpanFraction;

        viewport.visibleStart += delta;
        viewport.visibleEnd += delta;
        viewport.autoScroll = false;
        ClampViewport(viewport, dataStart, dataEnd, 0.001);

        if (viewport.visibleEnd >= dataEnd - 0.001) {
            viewport.autoScroll = true;
        }
    }
}
