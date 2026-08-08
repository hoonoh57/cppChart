#pragma once

#include "../core/market_types.h"

#include <vector>

namespace trading::render
{
    enum class TimeBoundaryKind
    {
        CalendarDate,
        SessionGap
    };

    struct TimeBoundary final
    {
        EpochMillis timestampMs = 0;
        TimeBoundaryKind kind = TimeBoundaryKind::SessionGap;
    };

    std::vector<TimeBoundary> FindTimeBoundaries(
        const std::vector<EpochMillis>& orderedTimestamps,
        int utcOffsetMinutes = 540,
        int gapMultiplier = 3);
}
