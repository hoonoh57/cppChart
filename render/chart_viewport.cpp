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

        double SafeFraction(double value, double maximum) noexcept
        {
            if (!std::isfinite(value)) return 0.0;
            return (std::max)(0.0, (std::min)(maximum, value));
        }

        AxisCoordinate MaximumViewportSpan(
            AxisCoordinate dataSpan,
            double maximumRightOverscrollFraction) noexcept
        {
            if (dataSpan <= 0.0) return 0.0;
            const double overscroll = SafeFraction(
                maximumRightOverscrollFraction,
                0.90);
            return overscroll > 0.0
                ? dataSpan / (1.0 - overscroll)
                : dataSpan;
        }

        AxisCoordinate ClampSpan(
            AxisCoordinate requested,
            AxisCoordinate maximumSpan,
            AxisCoordinate minimumSpan) noexcept
        {
            if (maximumSpan <= 0.0) return 0.0;
            const AxisCoordinate safeMinimum = (std::max)(0.001, minimumSpan);
            return (std::max)(
                (std::min)(requested, maximumSpan),
                (std::min)(safeMinimum, maximumSpan));
        }

        AxisCoordinate LatestPadding(
            AxisCoordinate span,
            double rightPaddingFraction) noexcept
        {
            return span * SafeFraction(rightPaddingFraction, 0.45);
        }

        bool Near(
            AxisCoordinate left,
            AxisCoordinate right,
            AxisCoordinate span) noexcept
        {
            return std::fabs(left - right) <=
                (std::max)(0.001, span / 100.0);
        }
    }

    void ResetViewport(
        ChartViewport& viewport,
        AxisCoordinate dataStart,
        AxisCoordinate dataEnd,
        AxisCoordinate preferredDataSpan,
        double rightPaddingFraction,
        double maximumRightOverscrollFraction) noexcept
    {
        const AxisCoordinate dataSpan = SafeDataSpan(dataStart, dataEnd);
        if (dataSpan <= 0.0) {
            viewport = {};
            return;
        }

        const double paddingFraction = SafeFraction(
            rightPaddingFraction,
            0.45);
        const AxisCoordinate requestedDataSpan =
            preferredDataSpan > 0.0
                ? (std::min)(preferredDataSpan, dataSpan)
                : dataSpan;
        const AxisCoordinate requestedSpan = paddingFraction < 1.0
            ? requestedDataSpan / (1.0 - paddingFraction)
            : requestedDataSpan;
        const AxisCoordinate maximumSpan = MaximumViewportSpan(
            dataSpan,
            (std::max)(
                maximumRightOverscrollFraction,
                paddingFraction));
        const AxisCoordinate span = ClampSpan(
            requestedSpan,
            maximumSpan,
            0.001);
        const AxisCoordinate padding = LatestPadding(
            span,
            paddingFraction);

        viewport.visibleEnd = dataEnd + padding;
        viewport.visibleStart = viewport.visibleEnd - span;
        if (viewport.visibleStart < dataStart) {
            viewport.visibleStart = dataStart;
            viewport.visibleEnd = viewport.visibleStart + span;
        }
        viewport.initialized = true;
        viewport.autoScroll = true;
    }

    void FollowLatest(
        ChartViewport& viewport,
        AxisCoordinate dataStart,
        AxisCoordinate dataEnd,
        double rightPaddingFraction,
        double maximumRightOverscrollFraction) noexcept
    {
        const AxisCoordinate dataSpan = SafeDataSpan(dataStart, dataEnd);
        if (dataSpan <= 0.0) {
            viewport = {};
            return;
        }
        if (!viewport.initialized) {
            ResetViewport(
                viewport,
                dataStart,
                dataEnd,
                0.0,
                rightPaddingFraction,
                maximumRightOverscrollFraction);
            return;
        }
        if (!viewport.autoScroll) {
            ClampViewport(
                viewport,
                dataStart,
                dataEnd,
                0.001,
                maximumRightOverscrollFraction);
            return;
        }

        const AxisCoordinate span = ClampSpan(
            viewport.Span(),
            MaximumViewportSpan(
                dataSpan,
                maximumRightOverscrollFraction),
            0.001);
        viewport.visibleEnd = dataEnd + LatestPadding(
            span,
            rightPaddingFraction);
        viewport.visibleStart = viewport.visibleEnd - span;
        if (viewport.visibleStart < dataStart) {
            viewport.visibleStart = dataStart;
            viewport.visibleEnd = viewport.visibleStart + span;
        }
        viewport.initialized = true;
    }

    void ClampViewport(
        ChartViewport& viewport,
        AxisCoordinate dataStart,
        AxisCoordinate dataEnd,
        AxisCoordinate minimumSpan,
        double maximumRightOverscrollFraction) noexcept
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
            MaximumViewportSpan(
                dataSpan,
                maximumRightOverscrollFraction),
            minimumSpan);
        AxisCoordinate start = viewport.visibleStart;
        AxisCoordinate end = start + span;
        const AxisCoordinate maximumEnd =
            dataEnd + span * SafeFraction(
                maximumRightOverscrollFraction,
                0.90);

        if (start < dataStart) {
            start = dataStart;
            end = start + span;
        }
        if (end > maximumEnd) {
            end = maximumEnd;
            start = end - span;
        }
        if (start < dataStart) {
            start = dataStart;
            end = start + span;
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
        AxisCoordinate minimumSpan,
        double rightPaddingFraction,
        double maximumRightOverscrollFraction) noexcept
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
            ResetViewport(
                viewport,
                dataStart,
                dataEnd,
                0.0,
                rightPaddingFraction,
                maximumRightOverscrollFraction);
        }

        anchorRatio = (std::max)(0.0, (std::min)(1.0, anchorRatio));
        const AxisCoordinate maximumSpan = MaximumViewportSpan(
            dataSpan,
            maximumRightOverscrollFraction);
        const AxisCoordinate oldSpan = ClampSpan(
            viewport.Span(),
            maximumSpan,
            minimumSpan);
        const double factor = std::pow(0.80, wheelSteps);
        const AxisCoordinate newSpan = ClampSpan(
            oldSpan * factor,
            maximumSpan,
            minimumSpan);
        const AxisCoordinate anchor =
            viewport.visibleStart + oldSpan * anchorRatio;

        viewport.visibleStart = anchor - newSpan * anchorRatio;
        viewport.visibleEnd = viewport.visibleStart + newSpan;
        const AxisCoordinate latestEnd =
            dataEnd + LatestPadding(newSpan, rightPaddingFraction);
        viewport.autoScroll = Near(
            viewport.visibleEnd,
            latestEnd,
            newSpan);
        ClampViewport(
            viewport,
            dataStart,
            dataEnd,
            minimumSpan,
            maximumRightOverscrollFraction);
    }

    void PanViewport(
        ChartViewport& viewport,
        AxisCoordinate dataStart,
        AxisCoordinate dataEnd,
        double visibleSpanFraction,
        double rightPaddingFraction,
        double maximumRightOverscrollFraction) noexcept
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
            ResetViewport(
                viewport,
                dataStart,
                dataEnd,
                0.0,
                rightPaddingFraction,
                maximumRightOverscrollFraction);
        }

        const AxisCoordinate span = ClampSpan(
            viewport.Span(),
            MaximumViewportSpan(
                dataSpan,
                maximumRightOverscrollFraction),
            0.001);
        const AxisCoordinate delta = span * visibleSpanFraction;

        viewport.visibleStart += delta;
        viewport.visibleEnd += delta;
        viewport.autoScroll = false;
        ClampViewport(
            viewport,
            dataStart,
            dataEnd,
            0.001,
            maximumRightOverscrollFraction);

        const AxisCoordinate latestEnd =
            dataEnd + LatestPadding(span, rightPaddingFraction);
        if (Near(viewport.visibleEnd, latestEnd, span)) {
            viewport.autoScroll = true;
        }
    }
}
