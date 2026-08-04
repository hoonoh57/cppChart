#include "comparison_transform.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace trading::app
{
    namespace
    {
        bool ValidValue(double value) noexcept
        {
            return std::isfinite(value) && value != 0.0;
        }

        double RawValue(const Bar& bar, double divisor) noexcept
        {
            return static_cast<double>(bar.close) / divisor;
        }

        double TransformValue(
            double comparison,
            double primary,
            double anchorComparison,
            double anchorPrimary,
            ComparisonValueMode mode) noexcept
        {
            if (mode == ComparisonValueMode::RawClose) return comparison;
            if (!ValidValue(anchorComparison)) return 0.0;

            const double comparisonRatio = comparison / anchorComparison;
            if (mode == ComparisonValueMode::Indexed100) {
                return comparisonRatio * 100.0;
            }
            if (mode == ComparisonValueMode::ReturnPercent) {
                return (comparisonRatio - 1.0) * 100.0;
            }
            if (!ValidValue(anchorPrimary) || !ValidValue(primary)) return 0.0;
            const double primaryRatio = primary / anchorPrimary;
            if (!ValidValue(primaryRatio)) return 0.0;
            return comparisonRatio / primaryRatio * 100.0;
        }
    }

    ComparisonTransformResult TransformComparisonBars(
        const std::vector<Bar>& comparisonBars,
        const std::vector<Bar>& primaryBars,
        ComparisonValueMode mode,
        double valueDivisor)
    {
        ComparisonTransformResult result;
        if (!std::isfinite(valueDivisor) || valueDivisor <= 0.0) return result;

        std::map<EpochMillis, double> primaryByTimestamp;
        for (const Bar& bar : primaryBars) {
            primaryByTimestamp[bar.closeTimestampMs] =
                static_cast<double>(bar.close);
        }

        // FindFirstCommonAnchor contract: normalized comparison modes keep
        // the first valid common timestamp fixed for the complete query range.
        result.points.reserve(comparisonBars.size());
        for (const Bar& bar : comparisonBars) {
            const double comparison = RawValue(bar, valueDivisor);
            if (!std::isfinite(comparison)) continue;

            double primary = 0.0;
            const auto primaryIt = primaryByTimestamp.find(bar.closeTimestampMs);
            if (primaryIt != primaryByTimestamp.end()) primary = primaryIt->second;

            const bool requiresPrimary =
                mode == ComparisonValueMode::RelativeStrength100;
            if (!result.hasAnchor) {
                if (!ValidValue(comparison)) continue;
                if (requiresPrimary && !ValidValue(primary)) continue;
                result.anchorTimestampMs = bar.closeTimestampMs;
                result.anchorComparisonValue = comparison;
                result.anchorPrimaryValue = primary;
                result.hasAnchor = true;
            }

            if (requiresPrimary && !ValidValue(primary)) continue;
            render::LinePoint point;
            point.timestampMs = bar.closeTimestampMs;
            point.value = TransformValue(
                comparison,
                primary,
                result.anchorComparisonValue,
                result.anchorPrimaryValue,
                mode);
            if (std::isfinite(point.value)) result.points.push_back(point);
        }
        return result;
    }

    bool TransformComparisonLivePoint(
        const Bar& comparisonLiveBar,
        const std::vector<Bar>& primaryBars,
        const ComparisonTransformResult& completed,
        ComparisonValueMode mode,
        double valueDivisor,
        render::LinePoint& point) noexcept
    {
        if (!completed.hasAnchor ||
            !std::isfinite(valueDivisor) || valueDivisor <= 0.0)
        {
            return false;
        }

        double primary = 0.0;
        if (mode == ComparisonValueMode::RelativeStrength100) {
            const auto iterator = std::lower_bound(
                primaryBars.begin(),
                primaryBars.end(),
                comparisonLiveBar.closeTimestampMs,
                [](const Bar& bar, EpochMillis timestamp) {
                    return bar.closeTimestampMs < timestamp;
                });
            if (iterator == primaryBars.end() ||
                iterator->closeTimestampMs != comparisonLiveBar.closeTimestampMs)
            {
                return false;
            }
            primary = static_cast<double>(iterator->close);
            if (!ValidValue(primary)) return false;
        }

        const double comparison = RawValue(comparisonLiveBar, valueDivisor);
        point.timestampMs = comparisonLiveBar.closeTimestampMs;
        point.value = TransformValue(
            comparison,
            primary,
            completed.anchorComparisonValue,
            completed.anchorPrimaryValue,
            mode);
        return std::isfinite(point.value);
    }

    const char* ComparisonValueModeName(
        ComparisonValueMode mode) noexcept
    {
        switch (mode) {
        case ComparisonValueMode::Indexed100:
            return "기준값 100";
        case ComparisonValueMode::ReturnPercent:
            return "누적 수익률 %";
        case ComparisonValueMode::RelativeStrength100:
            return "주 종목 대비 상대강도";
        default:
            return "원시 종가";
        }
    }
}
