#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace trading::stock_pool
{
    using EpochMillis = std::int64_t;

    enum class TimeRegime
    {
        OpeningLeadership,
        LaterStructure
    };

    enum class LeaderState
    {
        WarmingUp,
        Watch,
        Emerging,
        ConfirmedLeader,
        PersistentLeader,
        Weakening
    };

    struct Bar final
    {
        EpochMillis closeTimestampMs = 0;
        double open = 0.0;
        double high = 0.0;
        double low = 0.0;
        double close = 0.0;
        double cumulativeTurnover = 0.0;
        double tradeIntensity = 0.0;

        // Populated only for real T<n> candles. Minute bars and fixtures leave
        // these at zero; callers must never synthesize tick participation from
        // volume.
        int tickCount = 0;
        double tickDurationSeconds = 0.0;
        double tickRatePerSecond = 0.0;

        // Exact per-bar market activity when the source provides it. These are
        // appended after the legacy fields so positional fixture initializers
        // retain their historical meaning. `volume` is the source bar volume,
        // `turnover` is this bar's traded value, and cumulativeTurnover remains
        // the session-running traded value used by older ranking code.
        double volume = 0.0;
        double turnover = 0.0;
    };

    struct MemberSeries final
    {
        std::string code;
        std::string name;
        std::string market;
        std::vector<Bar> bars;
    };

    struct ScoringProfile final
    {
        int openingEndMinute = 60;
        int minimumHistoryBars = 6;
        int confirmationSnapshots = 2;
        int persistentSnapshots = 5;
        int exitRankBuffer = 2;
        double minimumStrength = 130.0;
        double riskOffMinimumStrength = 170.0;
        double minimumSeparation = 2.0;
        double minimumBreadth = 0.25;
    };

    struct RankRow final
    {
        std::size_t memberIndex = 0;
        std::string code;
        std::string name;
        std::string market;
        int rank = 0;
        int previousRank = 0;
        int rankChange = 0;
        int topMStreak = 0;
        double strength = 0.0;
        double return1mPercent = 0.0;
        double return5mPercent = 0.0;
        double sessionReturnPercent = 0.0;
        double turnoverPercentile = 0.0;
        double turnoverAccelerationPercentile = 0.0;
        double pullbackRecoveryPercentile = 0.0;
        double strengthDrawdown = 0.0;
        LeaderState state = LeaderState::WarmingUp;
        bool eligible = false;
        bool published = false;
    };

    struct RankingSnapshot final
    {
        std::size_t asOfIndex = 0;
        EpochMillis asOf = 0;
        TimeRegime regime = TimeRegime::OpeningLeadership;
        double breadthPositive = 0.0;
        double medianSessionReturnPercent = 0.0;
        double leaderSeparation = 0.0;
        bool noTrade = true;
        std::string noTradeReason;
        std::vector<RankRow> rows;
    };

    struct Trade final
    {
        std::string code;
        std::string name;
        std::size_t entryIndex = 0;
        std::size_t exitIndex = 0;
        double entryPrice = 0.0;
        double exitPrice = 0.0;
        double returnPercent = 0.0;
    };

    struct BacktestResult final
    {
        std::vector<RankingSnapshot> snapshots;
        std::vector<Trade> trades;
        int winCount = 0;
        double winRatePercent = 0.0;
        double averageReturnPercent = 0.0;
        double bestReturnPercent = 0.0;
        double worstReturnPercent = 0.0;
    };

    class RankingEngine final
    {
    public:
        void Reset();

        RankingSnapshot Evaluate(
            const std::vector<MemberSeries>& members,
            std::size_t asOfIndex,
            int topM,
            const ScoringProfile& profile);

    private:
        std::unordered_map<std::string, int> previousRanks_;
        std::unordered_map<std::string, int> topMStreaks_;
        std::unordered_map<std::string, double> peakStrengths_;
    };

    BacktestResult RunTopMBacktest(
        const std::vector<MemberSeries>& members,
        int topM,
        const ScoringProfile& profile);

    const char* TimeRegimeName(TimeRegime regime) noexcept;
    const char* LeaderStateName(LeaderState state) noexcept;
}
