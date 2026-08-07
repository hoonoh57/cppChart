#include "stock_pool_strength_cross.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace trading::stock_pool::strategy
{
    namespace
    {
        constexpr double kEpsilon = 1.0e-12;

        const RankRow* FindRow(
            const RankingSnapshot& snapshot,
            const std::string& code)
        {
            for (const RankRow& row : snapshot.rows) {
                if (row.code == code) return &row;
            }
            return nullptr;
        }

        double SafeReturn(double exitPrice, double entryPrice)
        {
            if (!std::isfinite(exitPrice) || !std::isfinite(entryPrice) ||
                entryPrice <= kEpsilon)
            {
                return 0.0;
            }
            return (exitPrice / entryPrice - 1.0) * 100.0;
        }

        void AppendTrade(
            StrengthCrossSummary& summary,
            const MemberSeries& member,
            std::size_t entryIndex,
            std::size_t exitIndex,
            double entryPrice,
            double exitPrice,
            StrengthCrossExitReason reason)
        {
            Trade trade;
            trade.code = member.code;
            trade.name = member.name;
            trade.entryIndex = entryIndex;
            trade.exitIndex = exitIndex;
            trade.entryPrice = entryPrice;
            trade.exitPrice = exitPrice;
            trade.returnPercent = SafeReturn(exitPrice, entryPrice);
            summary.backtest.trades.push_back(std::move(trade));

            switch (reason) {
            case StrengthCrossExitReason::TakeProfit:
                ++summary.takeProfitCount;
                break;
            case StrengthCrossExitReason::StopLoss:
                ++summary.stopLossCount;
                break;
            case StrengthCrossExitReason::SessionClose:
                ++summary.sessionCloseCount;
                break;
            }
        }

        void FinalizeMetrics(BacktestResult& result)
        {
            if (result.trades.empty()) return;

            double total = 0.0;
            result.bestReturnPercent =
                -std::numeric_limits<double>::infinity();
            result.worstReturnPercent =
                std::numeric_limits<double>::infinity();

            for (const Trade& trade : result.trades) {
                total += trade.returnPercent;
                if (trade.returnPercent > 0.0) ++result.winCount;
                result.bestReturnPercent =
                    (std::max)(result.bestReturnPercent, trade.returnPercent);
                result.worstReturnPercent =
                    (std::min)(result.worstReturnPercent, trade.returnPercent);
            }

            result.averageReturnPercent =
                total / static_cast<double>(result.trades.size());
            result.winRatePercent =
                100.0 * static_cast<double>(result.winCount) /
                static_cast<double>(result.trades.size());
        }
    }

    bool IsUpwardStrengthCross(
        double previousStrength,
        double currentStrength,
        double threshold) noexcept
    {
        return std::isfinite(previousStrength) &&
            std::isfinite(currentStrength) &&
            std::isfinite(threshold) &&
            previousStrength <= threshold &&
            currentStrength > threshold;
    }

    StrengthCrossSummary RunStrengthCrossBacktest(
        const std::vector<MemberSeries>& members,
        int rankingTopM,
        const ScoringProfile& scoringProfile,
        const StrengthCrossProfile& strategyProfile)
    {
        StrengthCrossSummary summary;
        summary.contract =
            "10m strength <=100 -> >100; next-bar open; TP +1.00%; SL -1.00%; rank ignored";
        if (members.empty()) return summary;

        std::size_t commonBars = std::numeric_limits<std::size_t>::max();
        for (const MemberSeries& member : members) {
            commonBars = (std::min)(commonBars, member.bars.size());
        }
        const std::size_t startIndex = static_cast<std::size_t>(
            (std::max)(1, scoringProfile.minimumHistoryBars) - 1);
        if (commonBars <= startIndex) return summary;

        rankingTopM = (std::max)(1, rankingTopM);
        const double targetFraction =
            (std::max)(0.0, strategyProfile.takeProfitPercent) / 100.0;
        const double stopFraction =
            (std::max)(0.0, strategyProfile.stopLossPercent) / 100.0;

        struct PendingEntry final
        {
            std::size_t memberIndex = 0U;
            std::size_t executeIndex = 0U;
        };

        struct Position final
        {
            std::size_t memberIndex = 0U;
            std::size_t entryIndex = 0U;
            double entryPrice = 0.0;
        };

        RankingEngine engine;
        std::unordered_map<std::string, PendingEntry> pendingEntries;
        std::unordered_map<std::string, Position> positions;
        RankingSnapshot previousSnapshot;
        bool hasPreviousSnapshot = false;

        for (std::size_t index = startIndex; index < commonBars; ++index) {
            for (auto iterator = pendingEntries.begin();
                 iterator != pendingEntries.end();)
            {
                if (iterator->second.executeIndex != index) {
                    ++iterator;
                    continue;
                }

                const PendingEntry pending = iterator->second;
                const MemberSeries& member = members[pending.memberIndex];
                const Bar& bar = member.bars[index];
                if (positions.count(member.code) == 0U &&
                    std::isfinite(bar.open) && bar.open > 0.0)
                {
                    Position position;
                    position.memberIndex = pending.memberIndex;
                    position.entryIndex = index;
                    position.entryPrice = bar.open;
                    positions.emplace(member.code, position);
                }
                iterator = pendingEntries.erase(iterator);
            }

            for (auto iterator = positions.begin();
                 iterator != positions.end();)
            {
                const Position position = iterator->second;
                const MemberSeries& member = members[position.memberIndex];
                const Bar& bar = member.bars[index];
                const double targetPrice =
                    position.entryPrice * (1.0 + targetFraction);
                const double stopPrice =
                    position.entryPrice * (1.0 - stopFraction);

                bool exit = false;
                double exitPrice = 0.0;
                StrengthCrossExitReason reason =
                    StrengthCrossExitReason::SessionClose;

                if (bar.open <= stopPrice) {
                    exit = true;
                    exitPrice = bar.open;
                    reason = StrengthCrossExitReason::StopLoss;
                }
                else if (bar.open >= targetPrice) {
                    exit = true;
                    exitPrice = bar.open;
                    reason = StrengthCrossExitReason::TakeProfit;
                }
                else {
                    const bool stopTouched = bar.low <= stopPrice;
                    const bool targetTouched = bar.high >= targetPrice;
                    if (stopTouched && targetTouched) {
                        exit = true;
                        if (strategyProfile.stopFirstWhenBothTouched) {
                            exitPrice = stopPrice;
                            reason = StrengthCrossExitReason::StopLoss;
                        }
                        else {
                            exitPrice = targetPrice;
                            reason = StrengthCrossExitReason::TakeProfit;
                        }
                    }
                    else if (stopTouched) {
                        exit = true;
                        exitPrice = stopPrice;
                        reason = StrengthCrossExitReason::StopLoss;
                    }
                    else if (targetTouched) {
                        exit = true;
                        exitPrice = targetPrice;
                        reason = StrengthCrossExitReason::TakeProfit;
                    }
                }

                if (!exit) {
                    ++iterator;
                    continue;
                }

                AppendTrade(
                    summary,
                    member,
                    position.entryIndex,
                    index,
                    position.entryPrice,
                    exitPrice,
                    reason);
                iterator = positions.erase(iterator);
            }

            RankingSnapshot snapshot =
                engine.Evaluate(
                    members,
                    index,
                    rankingTopM,
                    scoringProfile);

            for (RankRow& row : snapshot.rows) {
                row.published = false;
            }

            if (hasPreviousSnapshot && index + 1U < commonBars) {
                for (RankRow& current : snapshot.rows) {
                    if (!current.eligible ||
                        positions.count(current.code) != 0U ||
                        pendingEntries.count(current.code) != 0U)
                    {
                        continue;
                    }
                    const RankRow* previous =
                        FindRow(previousSnapshot, current.code);
                    if (previous == nullptr || !previous->eligible) continue;
                    if (!IsUpwardStrengthCross(
                            previous->strength,
                            current.strength,
                            strategyProfile.entryStrength))
                    {
                        continue;
                    }

                    current.published = true;
                    PendingEntry pending;
                    pending.memberIndex = current.memberIndex;
                    pending.executeIndex = index + 1U;
                    pendingEntries.emplace(current.code, pending);
                    ++summary.upwardCrossCount;
                }
            }

            summary.backtest.snapshots.push_back(snapshot);
            previousSnapshot = std::move(snapshot);
            hasPreviousSnapshot = true;
        }

        const std::size_t finalIndex = commonBars - 1U;
        for (const auto& pair : positions) {
            const Position& position = pair.second;
            const MemberSeries& member = members[position.memberIndex];
            const double exitPrice = member.bars[finalIndex].close;
            AppendTrade(
                summary,
                member,
                position.entryIndex,
                finalIndex,
                position.entryPrice,
                exitPrice,
                StrengthCrossExitReason::SessionClose);
        }

        FinalizeMetrics(summary.backtest);
        return summary;
    }
}
