#include "chart_viewport.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace trading::render
{
    namespace
    {
        EpochMillis SafeDataSpan(
            EpochMillis dataStartMs,
            EpochMillis dataEndMs) noexcept
        {
            return dataEndMs > dataStartMs
                ? dataEndMs - dataStartMs
                : 0;
        }

        EpochMillis ClampSpan(
            EpochMillis requested,
            EpochMillis dataSpan,
            EpochMillis minimumSpanMs) noexcept
        {
            if (dataSpan <= 0) return 0;
            const EpochMillis safeMinimum =
                (std::max)(static_cast<EpochMillis>(1), minimumSpanMs);
            return (std::max)(
                (std::min)(requested, dataSpan),
                (std::min)(safeMinimum, dataSpan));
        }
    }

    void ResetViewport(
        ChartViewport& viewport,
        EpochMillis dataStartMs,
        EpochMillis dataEndMs) noexcept
    {
        const EpochMillis dataSpan =
            SafeDataSpan(dataStartMs, dataEndMs);
        if (dataSpan <= 0) {
            viewport = {};
            return;
        }

        viewport.visibleStartMs = dataStartMs;
        viewport.visibleEndMs = dataEndMs;
        viewport.initialized = true;
        viewport.autoScroll = true;
    }

    void FollowLatest(
        ChartViewport& viewport,
        EpochMillis dataStartMs,
        EpochMillis dataEndMs) noexcept
    {
        const EpochMillis dataSpan =
            SafeDataSpan(dataStartMs, dataEndMs);
        if (dataSpan <= 0) {
            viewport = {};
            return;
        }
        if (!viewport.initialized) {
            ResetViewport(viewport, dataStartMs, dataEndMs);
            return;
        }
        if (!viewport.autoScroll) {
            ClampViewport(viewport, dataStartMs, dataEndMs, 1);
            return;
        }

        const EpochMillis span = ClampSpan(
            viewport.SpanMs(),
            dataSpan,
            1);
        viewport.visibleEndMs = dataEndMs;
        viewport.visibleStartMs = dataEndMs - span;
        if (viewport.visibleStartMs < dataStartMs) {
            viewport.visibleStartMs = dataStartMs;
        }
        viewport.initialized = true;
    }

    void ClampViewport(
        ChartViewport& viewport,
        EpochMillis dataStartMs,
        EpochMillis dataEndMs,
        EpochMillis minimumSpanMs) noexcept
    {
        const EpochMillis dataSpan =
            SafeDataSpan(dataStartMs, dataEndMs);
        if (dataSpan <= 0) {
            viewport = {};
            return;
        }
        if (!viewport.initialized) {
            ResetViewport(viewport, dataStartMs, dataEndMs);
            return;
        }

        const EpochMillis span = ClampSpan(
            viewport.SpanMs(),
            dataSpan,
            minimumSpanMs);

        EpochMillis start = viewport.visibleStartMs;
        EpochMillis end = start + span;

        if (start < dataStartMs) {
            start = dataStartMs;
            end = start + span;
        }
        if (end > dataEndMs) {
            end = dataEndMs;
            start = end - span;
        }

        viewport.visibleStartMs = start;
        viewport.visibleEndMs = end;
        viewport.initialized = true;
    }

    void ZoomViewport(
        ChartViewport& viewport,
        EpochMillis dataStartMs,
        EpochMillis dataEndMs,
        double anchorRatio,
        double wheelSteps,
        EpochMillis minimumSpanMs) noexcept
    {
        const EpochMillis dataSpan =
            SafeDataSpan(dataStartMs, dataEndMs);
        if (dataSpan <= 0 || wheelSteps == 0.0) return;
        if (!viewport.initialized) {
            ResetViewport(viewport, dataStartMs, dataEndMs);
        }

        anchorRatio = (std::max)(0.0, (std::min)(1.0, anchorRatio));
        const EpochMillis oldSpan = ClampSpan(
            viewport.SpanMs(),
            dataSpan,
            minimumSpanMs);
        const double factor = std::pow(0.80, wheelSteps);
        const EpochMillis requestedSpan = static_cast<EpochMillis>(
            std::llround(static_cast<double>(oldSpan) * factor));
        const EpochMillis newSpan = ClampSpan(
            requestedSpan,
            dataSpan,
            minimumSpanMs);

        const double anchorTime =
            static_cast<double>(viewport.visibleStartMs) +
            static_cast<double>(oldSpan) * anchorRatio;
        EpochMillis newStart = static_cast<EpochMillis>(std::llround(
            anchorTime - static_cast<double>(newSpan) * anchorRatio));

        viewport.visibleStartMs = newStart;
        viewport.visibleEndMs = newStart + newSpan;
        viewport.autoScroll =
            viewport.visibleEndMs >= dataEndMs - (std::max)(
                static_cast<EpochMillis>(1),
                newSpan / 100);
        ClampViewport(
            viewport,
            dataStartMs,
            dataEndMs,
            minimumSpanMs);
    }

    void PanViewport(
        ChartViewport& viewport,
        EpochMillis dataStartMs,
        EpochMillis dataEndMs,
        double visibleSpanFraction) noexcept
    {
        const EpochMillis dataSpan =
            SafeDataSpan(dataStartMs, dataEndMs);
        if (
            dataSpan <= 0 ||
            !std::isfinite(visibleSpanFraction) ||
            visibleSpanFraction == 0.0)
        {
            return;
        }
        if (!viewport.initialized) {
            ResetViewport(viewport, dataStartMs, dataEndMs);
        }

        const EpochMillis span = ClampSpan(
            viewport.SpanMs(),
            dataSpan,
            1);
        const EpochMillis delta = static_cast<EpochMillis>(std::llround(
            static_cast<double>(span) * visibleSpanFraction));

        viewport.visibleStartMs += delta;
        viewport.visibleEndMs += delta;
        viewport.autoScroll = false;
        ClampViewport(viewport, dataStartMs, dataEndMs, 1);

        if (viewport.visibleEndMs >= dataEndMs) {
            viewport.autoScroll = true;
        }
    }
}
