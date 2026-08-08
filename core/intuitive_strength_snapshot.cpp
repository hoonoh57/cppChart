#include "intuitive_strength_engine.h"

#include <algorithm>
#include <utility>

namespace trading::stock_pool::intuitive
{
    namespace
    {
        bool TryLatestPointAtOrBefore(
            const MemberStrengthSeries& member,
            EpochMillis asOfTime,
            StrengthPoint& point)
        {
            if (member.points.empty()) return false;
            auto iterator = std::upper_bound(
                member.points.begin(),
                member.points.end(),
                asOfTime,
                [](EpochMillis value, const StrengthPoint& candidate) {
                    return value < candidate.asOf;
                });
            if (iterator == member.points.begin()) return false;
            --iterator;
            point = *iterator;
            return true;
        }

        bool RowOrder(const StrengthRow& left, const StrengthRow& right)
        {
            if (left.buyEligible != right.buyEligible) {
                return left.buyEligible > right.buyEligible;
            }
            if (left.buyEligible) {
                if (left.point.crossJmaSlopePercent !=
                    right.point.crossJmaSlopePercent)
                {
                    return left.point.crossJmaSlopePercent >
                        right.point.crossJmaSlopePercent;
                }
                if (left.point.tickAvailable != right.point.tickAvailable) {
                    return left.point.tickAvailable > right.point.tickAvailable;
                }
                if (left.point.tickAvailable &&
                    left.point.tickRatePerSecond !=
                        right.point.tickRatePerSecond)
                {
                    return left.point.tickRatePerSecond >
                        right.point.tickRatePerSecond;
                }
                if (left.point.fastJmaSlopePercent !=
                    right.point.fastJmaSlopePercent)
                {
                    return left.point.fastJmaSlopePercent >
                        right.point.fastJmaSlopePercent;
                }
            }
            if (left.point.sessionReturnPercent !=
                right.point.sessionReturnPercent)
            {
                return left.point.sessionReturnPercent >
                    right.point.sessionReturnPercent;
            }
            return left.code < right.code;
        }
    }

    StrengthSnapshot BuildStrengthSnapshotAtTime(
        const std::vector<MemberStrengthSeries>& series,
        EpochMillis asOfTime,
        const StrengthConfig&)
    {
        StrengthSnapshot snapshot;
        snapshot.asOf = asOfTime;

        for (const MemberStrengthSeries& member : series) {
            StrengthPoint point;
            if (!TryLatestPointAtOrBefore(member, asOfTime, point)) continue;

            StrengthRow row;
            row.memberIndex = member.memberIndex;
            row.code = member.code;
            row.name = member.name;
            row.market = member.market;
            row.point = point;
            row.buyEligible =
                point.inEvaluationWindow &&
                point.fresh &&
                point.bullishRegime &&
                point.barsSinceCross >= 0 &&
                point.crossJmaSlopePercent > 0.0;
            snapshot.rows.push_back(std::move(row));
        }

        std::stable_sort(
            snapshot.rows.begin(),
            snapshot.rows.end(),
            RowOrder);

        int priority = 1;
        for (StrengthRow& row : snapshot.rows) {
            if (row.buyEligible) row.buyPriority = priority++;
        }
        return snapshot;
    }
}
