#include "time_axis.h"

#include <algorithm>
#include <cmath>

namespace trading::render
{
    bool OrdinalTimeAxis::Reset(
        const std::vector<EpochMillis>& timestamps,
        std::string& error)
    {
        if (timestamps.empty()) {
            error = "time axis requires at least one timestamp";
            return false;
        }

        EpochMillis previous = 0;
        for (std::size_t index = 0; index < timestamps.size(); ++index) {
            const EpochMillis timestamp = timestamps[index];
            if (timestamp <= 0) {
                error = "time axis contains a non-positive timestamp";
                return false;
            }
            if (index > 0 && timestamp <= previous) {
                error = "time axis timestamps must be strictly increasing";
                return false;
            }
            previous = timestamp;
        }

        timestamps_ = timestamps;
        error.clear();
        return true;
    }

    void OrdinalTimeAxis::Clear() noexcept
    {
        timestamps_.clear();
    }

    bool OrdinalTimeAxis::Empty() const noexcept
    {
        return timestamps_.empty();
    }

    std::size_t OrdinalTimeAxis::Size() const noexcept
    {
        return timestamps_.size();
    }

    AxisCoordinate OrdinalTimeAxis::Minimum() const noexcept
    {
        return 0.0;
    }

    AxisCoordinate OrdinalTimeAxis::Maximum() const noexcept
    {
        if (timestamps_.size() <= 1) return 1.0;
        return static_cast<AxisCoordinate>(timestamps_.size() - 1U);
    }

    AxisCoordinate OrdinalTimeAxis::CoordinateForTimestamp(
        EpochMillis timestampMs) const noexcept
    {
        if (timestamps_.empty()) return 0.0;
        if (timestamps_.size() == 1) return 0.5;
        if (timestampMs <= timestamps_.front()) return 0.0;
        if (timestampMs >= timestamps_.back()) return Maximum();

        const auto upper = std::lower_bound(
            timestamps_.begin(),
            timestamps_.end(),
            timestampMs);
        const std::size_t upperIndex = static_cast<std::size_t>(
            upper - timestamps_.begin());
        if (upper != timestamps_.end() && *upper == timestampMs) {
            return static_cast<AxisCoordinate>(upperIndex);
        }

        const std::size_t lowerIndex = upperIndex - 1U;
        const EpochMillis lowerTimestamp = timestamps_[lowerIndex];
        const EpochMillis upperTimestamp = timestamps_[upperIndex];
        const double denominator = static_cast<double>(
            upperTimestamp - lowerTimestamp);
        const double ratio = denominator > 0.0
            ? static_cast<double>(timestampMs - lowerTimestamp) / denominator
            : 0.0;
        return static_cast<AxisCoordinate>(lowerIndex) +
            (std::max)(0.0, (std::min)(1.0, ratio));
    }

    EpochMillis OrdinalTimeAxis::TimestampForCoordinate(
        AxisCoordinate coordinate) const noexcept
    {
        if (timestamps_.empty()) return 0;
        if (timestamps_.size() == 1) return timestamps_.front();
        if (!std::isfinite(coordinate)) return timestamps_.front();

        const AxisCoordinate clamped = (std::max)(
            Minimum(),
            (std::min)(Maximum(), coordinate));
        const long long rounded = std::llround(clamped);
        const long long maximumIndex =
            static_cast<long long>(timestamps_.size() - 1U);
        const long long bounded = (std::max)(
            0LL,
            (std::min)(maximumIndex, rounded));
        const std::size_t index = static_cast<std::size_t>(bounded);
        return timestamps_[index];
    }

    const std::vector<EpochMillis>& OrdinalTimeAxis::Timestamps() const noexcept
    {
        return timestamps_;
    }
}
