#include "stock_pool_evaluator.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace trading::stock_pool::evaluation
{
    namespace
    {
        double MaximumReturnPercent(const MemberSeries& member)
        {
            if (member.bars.empty() || member.bars.front().close <= 0.0) {
                return 0.0;
            }
            const double anchor = member.bars.front().close;
            double maximum = 0.0;
            for (const Bar& bar : member.bars) {
                if (bar.high <= 0.0) continue;
                maximum = (std::max)(
                    maximum,
                    (bar.high / anchor - 1.0) * 100.0);
            }
            return maximum;
        }
    }

    EvaluationReport EvaluateWinnerCapture(
        const std::vector<MemberSeries>& members,
        const BacktestResult& backtest,
        int winnerK)
    {
        EvaluationReport report;
        winnerK = (std::max)(0, winnerK);
        if (winnerK == 0 || members.empty()) return report;

        struct Candidate final
        {
            const MemberSeries* member = nullptr;
            double maximumReturnPercent = 0.0;
        };

        std::vector<Candidate> candidates;
        candidates.reserve(members.size());
        for (const MemberSeries& member : members) {
            Candidate candidate;
            candidate.member = &member;
            candidate.maximumReturnPercent = MaximumReturnPercent(member);
            candidates.push_back(candidate);
        }
        std::stable_sort(
            candidates.begin(),
            candidates.end(),
            [](const Candidate& left, const Candidate& right) {
                if (std::abs(
                        left.maximumReturnPercent -
                        right.maximumReturnPercent) > 1.0e-12)
                {
                    return left.maximumReturnPercent >
                        right.maximumReturnPercent;
                }
                return left.member->code < right.member->code;
            });

        const std::size_t count = (std::min)(
            candidates.size(),
            static_cast<std::size_t>(winnerK));
        report.winnerCount = static_cast<int>(count);
        for (std::size_t winnerIndex = 0U;
             winnerIndex < count;
             ++winnerIndex)
        {
            const Candidate& candidate = candidates[winnerIndex];
            WinnerCapture winner;
            winner.code = candidate.member->code;
            winner.name = candidate.member->name;
            winner.maximumReturnPercent = candidate.maximumReturnPercent;

            for (std::size_t snapshotIndex = 0U;
                 snapshotIndex < backtest.snapshots.size();
                 ++snapshotIndex)
            {
                const RankingSnapshot& snapshot =
                    backtest.snapshots[snapshotIndex];
                const auto found = std::find_if(
                    snapshot.rows.begin(),
                    snapshot.rows.end(),
                    [&](const RankRow& row) {
                        return row.code == winner.code && row.published;
                    });
                if (found == snapshot.rows.end()) continue;
                winner.captured = true;
                winner.firstPublishedSnapshot = snapshotIndex;
                winner.firstPublishedMinute =
                    static_cast<int>(snapshot.asOfIndex);
                ++report.capturedWinnerCount;
                break;
            }
            report.winners.push_back(std::move(winner));
        }

        if (report.winnerCount > 0) {
            report.captureRatePercent =
                100.0 * static_cast<double>(report.capturedWinnerCount) /
                static_cast<double>(report.winnerCount);
        }
        return report;
    }
}
