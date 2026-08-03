#pragma once

#include "../core/market_types.h"

namespace trading::render
{
    struct ChartViewport final
    {
        EpochMillis visibleStartMs = 0;
        EpochMillis visibleEndMs = 0;
        bool initialized = false;
        bool autoScroll = true;

        EpochMillis SpanMs() const noexcept
        {
            return visibleEndMs > visibleStartMs
                ? visibleEndMs - visibleStartMs
                : 0;
        }
    };

    void ResetViewport(
        ChartViewport& viewport,
        EpochMillis dataStartMs,
        EpochMillis dataEndMs) noexcept;

    void FollowLatest(
        ChartViewport& viewport,
        EpochMillis dataStartMs,
        EpochMillis dataEndMs) noexcept;

    void ClampViewport(
        ChartViewport& viewport,
        EpochMillis dataStartMs,
        EpochMillis dataEndMs,
        EpochMillis minimumSpanMs) noexcept;

    void ZoomViewport(
        ChartViewport& viewport,
        EpochMillis dataStartMs,
        EpochMillis dataEndMs,
        double anchorRatio,
        double wheelSteps,
        EpochMillis minimumSpanMs) noexcept;

    void PanViewport(
        ChartViewport& viewport,
        EpochMillis dataStartMs,
        EpochMillis dataEndMs,
        double visibleSpanFraction) noexcept;
}
