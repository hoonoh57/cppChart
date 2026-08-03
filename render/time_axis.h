#pragma once

#include "chart_viewport.h"
#include "../core/market_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace trading::render
{
    class OrdinalTimeAxis final
    {
    public:
        bool Reset(
            const std::vector<EpochMillis>& timestamps,
            std::string& error);

        void Clear() noexcept;

        bool Empty() const noexcept;
        std::size_t Size() const noexcept;
        AxisCoordinate Minimum() const noexcept;
        AxisCoordinate Maximum() const noexcept;

        AxisCoordinate CoordinateForTimestamp(
            EpochMillis timestampMs) const noexcept;

        EpochMillis TimestampForCoordinate(
            AxisCoordinate coordinate) const noexcept;

        const std::vector<EpochMillis>& Timestamps() const noexcept;

    private:
        std::vector<EpochMillis> timestamps_;
    };
}
