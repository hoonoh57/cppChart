#include "stock_pool_engine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace trading::stock_pool
{
    namespace
    {
        constexpr double kEpsilon = 1.0e-12;

        struct FeatureWork final
        {
            std::size_t memberIndex = 0;
            std::string code;
            std::string name;
            std::string market;
            bool eligible = false;
            double return1m = 0.0;
            double return5m = 0.0;
            double sessionReturn = 0.0;
            double turnoverVelocity = 0.0;
            double turnoverAcceleration = 0.0;
            double pullbackRecovery = 0.0;
            double strengthDrawdown = 0.0;
            double persistence = 0.0;
            double return1mPct = 0.0;
            double return5mPct = 0.0;
            double sessionReturnPct = 0.0;
            double turnoverPct = 0.0;
            double turnoverAccelerationPct = 0.0;
            double pullbackRecoveryPct = 0.0;
            double strengthDrawdownPct = 0.0;
            double persistencePct = 0.0;
            double strength = 0.0;
        };

        double SafeReturn(double current, double previous)
        {
            if (!std::isfinite(current) || !std::isfinite(previous) ||
                std::abs(previous) <= kEpsilon)
            {
                return 0.0;
            }
            return (current / previous - 1.0) * 100.0;
        }

        double Clamp(double value, double minimum, double maximum)
        {
            return (std::max)(minimum, (std::min)(maximum, value));
        }

        double Median(std::vector<double> values)
        {
            if (values.empty()) return 0.0;
            std::sort(values.begin(), values.end());
            const std::size_t middle = values.size() / 2U;
            if ((values.size() & 1U) != 0U) return values[middle];
            return (values[middle - 1U] + values[middle]) * 0.5;
        }

        double PercentileOf(double value, const std::vector<double>& values)
        {
            if (values.empty()) return 0.0;
            std::size_t lower = 0U;
            std::size_t equal = 0U;
            for (double candidate : values) {
                if (candidate < value - kEpsilon) ++lower;
                else if (std::abs(candidate - value) <= kEpsilon) ++equal;
            }
            const double centered =
                static_cast<double>(lower) +
                0.5 * static_cast<double>((std::max)(std::size_t{1U}, equal));
            return 100.0 * centered / static_cast<double>(values.size());
        }

        template <typename Selector>
        std::vector<double> CollectEligible(
            const std::vector<FeatureWork>& work,
            Selector selector)
        {
            std::vector<double> result;
            result.reserve(work.size());
            for (const FeatureWork& item : work) {
                if (item.eligible) result.push_back(selector(item));
            }
            return result;
        }

        const Bar* TryBar(
            const MemberSeries& member,
            std::size_t index)
        {
            if (index >= member.bars.size()) return nullptr;
            return &member.bars[index];
        }

        double CurrentPrice(
            const std::vector<MemberSeries>& members,
            const RankRow& row,
            std::size_t index)
        {
            if (row.memberIndex >= members.size()) return 0.0;
            const Bar* bar = TryBar(members[row.memberIndex], index);
            return bar != nullptr ? bar->close : 0.0;
        }
    }

    void RankingEngine::Reset()
    {
        previousRanks_.clear();
        topMStreaks_.clear();
        peakStrengths_.clear();
    }

    RankingSnapshot RankingEngine::Evaluate(
        const std::vector<MemberSeries>& members,
        std::size_t asOfIndex,
        int topM,
        const ScoringProfile& profile)
    {
        RankingSnapshot snapshot;
        snapshot.asOfIndex = asOfIndex;
        snapshot.regime =
            static_cast<int>(asOfIndex) < profile.openingEndMinute
                ? TimeRegime::OpeningLeadership
                : TimeRegime::LaterStructure;
        topM = (std::max)(1, topM);

        std::vector<FeatureWork> work;
        work.reserve(members.size());
        for (std::size_t memberIndex = 0U;
             memberIndex < members.size();
             ++memberIndex)
        {
            const MemberSeries& member = members[memberIndex];
            FeatureWork feature;
            feature.memberIndex = memberIndex;
            feature.code = member.code;
            feature.name = member.name;
            feature.market = member.market;
            feature.persistence = static_cast<double>(topMStreaks_[member.code]);

            if (asOfIndex >= member.bars.size() ||
                asOfIndex + 1U <
                    static_cast<std::size_t>(profile.minimumHistoryBars))
            {
                work.push_back(std::move(feature));
                continue;
            }

            const Bar& current = member.bars[asOfIndex];
            const Bar& previous = member.bars[asOfIndex - 1U];
            const Bar& fiveBack =
                member.bars[asOfIndex >= 5U ? asOfIndex - 5U : 0U];
            const Bar& first = member.bars.front();
            if (!std::isfinite(current.close) || current.close <= 0.0 ||
                !std::isfinite(current.cumulativeTurnover))
            {
                work.push_back(std::move(feature));
                continue;
            }

            feature.eligible = true;
            feature.return1m = SafeReturn(current.close, previous.close);
            feature.return5m = SafeReturn(current.close, fiveBack.close);
            feature.sessionReturn = SafeReturn(current.close, first.close);
            feature.turnoverVelocity =
                (std::max)(0.0,
                    current.cumulativeTurnover - previous.cumulativeTurnover);
            if (asOfIndex >= 2U) {
                const Bar& twoBack = member.bars[asOfIndex - 2U];
                const double previousVelocity =
                    (std::max)(0.0,
                        previous.cumulativeTurnover -
                        twoBack.cumulativeTurnover);
                feature.turnoverAcceleration =
                    feature.turnoverVelocity - previousVelocity;
            }

            const std::size_t lookbackStart =
                asOfIndex > 10U ? asOfIndex - 10U : 0U;
            double priorHigh = member.bars[lookbackStart].high;
            double recentLow = member.bars[lookbackStart].low;
            for (std::size_t index = lookbackStart;
                 index < asOfIndex;
                 ++index)
            {
                priorHigh = (std::max)(priorHigh, member.bars[index].high);
                recentLow = (std::min)(recentLow, member.bars[index].low);
            }
            if (priorHigh > recentLow + kEpsilon) {
                feature.pullbackRecovery = Clamp(
                    (current.close - recentLow) /
                        (priorHigh - recentLow),
                    0.0,
                    1.5);
            }
            feature.strengthDrawdown =
                priorHigh > kEpsilon
                    ? SafeReturn(current.close, priorHigh)
                    : 0.0;
            work.push_back(std::move(feature));
        }

        const std::vector<double> return1mValues =
            CollectEligible(work, [](const FeatureWork& item) {
                return item.return1m;
            });
        const std::vector<double> return5mValues =
            CollectEligible(work, [](const FeatureWork& item) {
                return item.return5m;
            });
        const std::vector<double> sessionValues =
            CollectEligible(work, [](const FeatureWork& item) {
                return item.sessionReturn;
            });
        const std::vector<double> turnoverValues =
            CollectEligible(work, [](const FeatureWork& item) {
                return item.turnoverVelocity;
            });
        const std::vector<double> accelerationValues =
            CollectEligible(work, [](const FeatureWork& item) {
                return item.turnoverAcceleration;
            });
        const std::vector<double> recoveryValues =
            CollectEligible(work, [](const FeatureWork& item) {
                return item.pullbackRecovery;
            });
        const std::vector<double> drawdownValues =
            CollectEligible(work, [](const FeatureWork& item) {
                return item.strengthDrawdown;
            });
        const std::vector<double> persistenceValues =
            CollectEligible(work, [](const FeatureWork& item) {
                return item.persistence;
            });

        int positiveCount = 0;
        for (FeatureWork& item : work) {
            if (!item.eligible) continue;
            item.return1mPct = PercentileOf(item.return1m, return1mValues);
            item.return5mPct = PercentileOf(item.return5m, return5mValues);
            item.sessionReturnPct =
                PercentileOf(item.sessionReturn, sessionValues);
            item.turnoverPct =
                PercentileOf(item.turnoverVelocity, turnoverValues);
            item.turnoverAccelerationPct =
                PercentileOf(
                    item.turnoverAcceleration,
                    accelerationValues);
            item.pullbackRecoveryPct =
                PercentileOf(item.pullbackRecovery, recoveryValues);
            item.strengthDrawdownPct =
                PercentileOf(item.strengthDrawdown, drawdownValues);
            item.persistencePct =
                PercentileOf(item.persistence, persistenceValues);

            if (snapshot.regime == TimeRegime::OpeningLeadership) {
                item.strength = 2.0 * Median({
                    item.return1mPct,
                    item.return5mPct,
                    item.sessionReturnPct,
                    item.turnoverPct,
                    item.turnoverAccelerationPct});
            }
            else {
                item.strength = 2.0 * Median({
                    item.return5mPct,
                    item.pullbackRecoveryPct,
                    item.turnoverAccelerationPct,
                    item.strengthDrawdownPct,
                    item.persistencePct});
            }
            if (item.sessionReturn > 0.0) ++positiveCount;
        }

        std::stable_sort(
            work.begin(),
            work.end(),
            [](const FeatureWork& left, const FeatureWork& right) {
                if (left.eligible != right.eligible) return left.eligible;
                if (std::abs(left.strength - right.strength) > kEpsilon) {
                    return left.strength > right.strength;
                }
                if (std::abs(
                        left.turnoverAccelerationPct -
                        right.turnoverAccelerationPct) > kEpsilon)
                {
                    return left.turnoverAccelerationPct >
                        right.turnoverAccelerationPct;
                }
                if (std::abs(left.turnoverPct - right.turnoverPct) > kEpsilon) {
                    return left.turnoverPct > right.turnoverPct;
                }
                return left.code < right.code;
            });

        const std::size_t eligibleCount = sessionValues.size();
        snapshot.breadthPositive = eligibleCount > 0U
            ? static_cast<double>(positiveCount) /
                static_cast<double>(eligibleCount)
            : 0.0;
        snapshot.medianSessionReturnPercent = Median(sessionValues);
        if (!work.empty()) {
            const std::size_t comparisonIndex =
                (std::min)(
                    work.size() - 1U,
                    static_cast<std::size_t>(topM));
            snapshot.leaderSeparation =
                work.front().strength - work[comparisonIndex].strength;
        }

        if (eligibleCount < static_cast<std::size_t>(topM)) {
            snapshot.noTradeReason = "평가 가능 종목 수 부족";
        }
        else if (work.empty() || work.front().strength < profile.minimumStrength) {
            snapshot.noTradeReason = "절대 상대강도 부족";
        }
        else if (
            snapshot.breadthPositive < profile.minimumBreadth &&
            work.front().strength < profile.riskOffMinimumStrength)
        {
            snapshot.noTradeReason = "약세 종목풀에서 리더 분리 부족";
        }
        else if (snapshot.leaderSeparation < profile.minimumSeparation) {
            snapshot.noTradeReason = "Top-M과 후순위 분리 부족";
        }
        snapshot.noTrade = !snapshot.noTradeReason.empty();

        snapshot.rows.reserve(work.size());
        int rank = 0;
        for (const FeatureWork& item : work) {
            RankRow row;
            row.memberIndex = item.memberIndex;
            row.code = item.code;
            row.name = item.name;
            row.market = item.market;
            row.eligible = item.eligible;
            row.strength = item.strength;
            row.return1mPercent = item.return1m;
            row.return5mPercent = item.return5m;
            row.sessionReturnPercent = item.sessionReturn;
            row.turnoverPercentile = item.turnoverPct;
            row.turnoverAccelerationPercentile =
                item.turnoverAccelerationPct;
            row.pullbackRecoveryPercentile =
                item.pullbackRecoveryPct;

            if (!item.eligible) {
                row.state = LeaderState::WarmingUp;
                snapshot.rows.push_back(std::move(row));
                continue;
            }

            ++rank;
            row.rank = rank;
            const auto previous = previousRanks_.find(item.code);
            row.previousRank =
                previous != previousRanks_.end() ? previous->second : rank;
            row.rankChange = row.previousRank - row.rank;
            const bool inTopM = row.rank <= topM;
            int& streak = topMStreaks_[item.code];
            streak = inTopM ? streak + 1 : 0;
            row.topMStreak = streak;

            double& peak = peakStrengths_[item.code];
            peak = (std::max)(peak, row.strength);
            row.strengthDrawdown = peak > kEpsilon
                ? (row.strength / peak - 1.0) * 100.0
                : 0.0;

            if (inTopM) {
                if (streak >= profile.persistentSnapshots) {
                    row.state = LeaderState::PersistentLeader;
                }
                else if (streak >= profile.confirmationSnapshots) {
                    row.state = LeaderState::ConfirmedLeader;
                }
                else {
                    row.state = LeaderState::Emerging;
                }
            }
            else if (row.previousRank <= topM && row.rank > topM) {
                row.state = LeaderState::Weakening;
            }
            else {
                row.state = LeaderState::Watch;
            }
            row.published =
                !snapshot.noTrade &&
                inTopM &&
                (row.state == LeaderState::ConfirmedLeader ||
                 row.state == LeaderState::PersistentLeader);
            previousRanks_[item.code] = row.rank;
            snapshot.rows.push_back(std::move(row));
        }

        for (const MemberSeries& member : members) {
            const Bar* bar = TryBar(member, asOfIndex);
            if (bar != nullptr) {
                snapshot.asOf = (std::max)(snapshot.asOf, bar->closeTimestampMs);
            }
        }
        return snapshot;
    }

    BacktestResult RunTopMBacktest(
        const std::vector<MemberSeries>& members,
        int topM,
        const ScoringProfile& profile)
    {
        BacktestResult result;
        if (members.empty()) return result;

        std::size_t maximumBars = 0U;
        for (const MemberSeries& member : members) {
            maximumBars = (std::max)(maximumBars, member.bars.size());
        }
        if (maximumBars <
            static_cast<std::size_t>(profile.minimumHistoryBars))
        {
            return result;
        }

        struct Position final
        {
            std::string code;
            std::string name;
            std::size_t memberIndex = 0U;
            std::size_t entryIndex = 0U;
            double entryPrice = 0.0;
        };

        RankingEngine engine;
        std::unordered_map<std::string, Position> positions;
        for (std::size_t index =
                 static_cast<std::size_t>(profile.minimumHistoryBars - 1);
             index < maximumBars;
             ++index)
        {
            RankingSnapshot snapshot =
                engine.Evaluate(members, index, topM, profile);
            const bool finalSnapshot = index + 1U == maximumBars;

            std::unordered_map<std::string, const RankRow*> rows;
            for (const RankRow& row : snapshot.rows) {
                if (row.eligible) rows[row.code] = &row;
            }

            for (auto iterator = positions.begin();
                 iterator != positions.end();)
            {
                const auto row = rows.find(iterator->first);
                const bool exit =
                    finalSnapshot ||
                    row == rows.end() ||
                    !row->second->published ||
                    row->second->rank > topM + profile.exitRankBuffer;
                if (!exit) {
                    ++iterator;
                    continue;
                }

                const Position& position = iterator->second;
                const MemberSeries& member = members[position.memberIndex];
                const std::size_t exitIndex =
                    (std::min)(index, member.bars.size() - 1U);
                const double exitPrice = member.bars[exitIndex].close;
                Trade trade;
                trade.code = position.code;
                trade.name = position.name;
                trade.entryIndex = position.entryIndex;
                trade.exitIndex = exitIndex;
                trade.entryPrice = position.entryPrice;
                trade.exitPrice = exitPrice;
                trade.returnPercent =
                    SafeReturn(exitPrice, position.entryPrice);
                result.trades.push_back(std::move(trade));
                iterator = positions.erase(iterator);
            }

            if (!finalSnapshot) {
                for (const RankRow& row : snapshot.rows) {
                    if (!row.published || positions.count(row.code) != 0U) {
                        continue;
                    }
                    const double price = CurrentPrice(members, row, index);
                    if (price <= 0.0) continue;
                    Position position;
                    position.code = row.code;
                    position.name = row.name;
                    position.memberIndex = row.memberIndex;
                    position.entryIndex = index;
                    position.entryPrice = price;
                    positions.emplace(position.code, std::move(position));
                }
            }
            result.snapshots.push_back(std::move(snapshot));
        }

        if (!result.trades.empty()) {
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
        return result;
    }

    const char* TimeRegimeName(TimeRegime regime) noexcept
    {
        switch (regime) {
        case TimeRegime::OpeningLeadership:
            return "장초반 가격강도";
        case TimeRegime::LaterStructure:
            return "조정·재상승 구조";
        }
        return "Unknown";
    }

    const char* LeaderStateName(LeaderState state) noexcept
    {
        switch (state) {
        case LeaderState::WarmingUp:
            return "준비";
        case LeaderState::Watch:
            return "관찰";
        case LeaderState::Emerging:
            return "부상";
        case LeaderState::ConfirmedLeader:
            return "확인";
        case LeaderState::PersistentLeader:
            return "지속대장";
        case LeaderState::Weakening:
            return "약화";
        }
        return "Unknown";
    }
}
