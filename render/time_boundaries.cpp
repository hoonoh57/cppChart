#include "time_boundaries.h"

#include <algorithm>
#include <cstdint>

namespace trading::render
{
    namespace
    {
        constexpr EpochMillis MinuteMs = 60LL * 1000LL;
        constexpr EpochMillis DayMs = 24LL * 60LL * MinuteMs;

        std::int64_t LocalDayNumber(
            EpochMillis timestampMs,
            int utcOffsetMinutes) noexcept
        {
            const EpochMillis offsetMs =
                static_cast<EpochMillis>(utcOffsetMinutes) * MinuteMs;
            const EpochMillis shifted = timestampMs + offsetMs;
            if (shifted >= 0) return shifted / DayMs;
            return -(((-shifted) + DayMs - 1) / DayMs);
        }

        EpochMillis InferExpectedStep(
            const std::vector<EpochMillis>& timestamps)
        {
            std::vector<EpochMillis> positiveSteps;
            positiveSteps.reserve(timestamps.size());

            for (std::size_t index = 1; index < timestamps.size(); ++index) {
                const EpochMillis step =
                    timestamps[index] - timestamps[index - 1];
                if (step > 0 && step <= 6LL * 60LL * 60LL * 1000LL) {
                    positiveSteps.push_back(step);
                }
            }

            if (positiveSteps.empty()) return 0;
            const std::size_t middle = positiveSteps.size() / 2;
            std::nth_element(
                positiveSteps.begin(),
                positiveSteps.begin() +
                    static_cast<std::ptrdiff_t>(middle),
                positiveSteps.end());
            return positiveSteps[middle];
        }
    }

    std::vector<TimeBoundary> FindTimeBoundaries(
        const std::vector<EpochMillis>& orderedTimestamps,
        int utcOffsetMinutes,
        int gapMultiplier)
    {
        std::vector<TimeBoundary> result;
        if (orderedTimestamps.size() < 2) return result;

        const EpochMillis expectedStep =
            InferExpectedStep(orderedTimestamps);
        const int safeGapMultiplier = (std::max)(2, gapMultiplier);
        const EpochMillis gapThreshold = expectedStep > 0
            ? expectedStep * safeGapMultiplier
            : 0;

        result.reserve(orderedTimestamps.size() / 64 + 1);
        for (std::size_t index = 1; index < orderedTimestamps.size(); ++index) {
            const EpochMillis previous = orderedTimestamps[index - 1];
            const EpochMillis current = orderedTimestamps[index];
            if (current <= previous) continue;

            TimeBoundary boundary;
            boundary.timestampMs = current;

            if (
                LocalDayNumber(previous, utcOffsetMinutes) !=
                LocalDayNumber(current, utcOffsetMinutes))
            {
                boundary.kind = TimeBoundaryKind::CalendarDate;
                result.push_back(boundary);
                continue;
            }

            if (
                gapThreshold > 0 &&
                current - previous > gapThreshold)
            {
                boundary.kind = TimeBoundaryKind::SessionGap;
                result.push_back(boundary);
            }
        }

        return result;
    }
}
