#pragma once

#include "../core/kiwoom_market_data.h"
#include "../core/kiwoom_protocol.h"
#include "../core/market_types.h"
#include "feature_registry.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace trading::app
{
    enum class MarketDataState
    {
        Disconnected,
        Loading,
        Ready,
        Error
    };

    struct MarketDataSnapshot final
    {
        MarketDataState state = MarketDataState::Disconnected;
        FeatureLevel level = FeatureLevel::Off;
        std::string code;
        int minuteUnit = 1;
        std::size_t barCount = 0;
        Bar latestBar;
        bool hasLatestBar = false;
        Continuation continuation;
        std::string error;
        std::uint64_t revision = 0;
        std::uint64_t completedRevision = 0;
        std::uint64_t liveRevision = 0;
        std::uint64_t stockTradeTickCount = 0;
        EpochMillis lastStockTradeTimestampMs = 0;
        bool stockTradeSubscriptionRequested = false;
        std::size_t retainedBytes = 0;
    };

    struct MarketDataSeriesSnapshot final
    {
        MarketDataState state = MarketDataState::Disconnected;
        FeatureLevel level = FeatureLevel::Off;
        std::string code;
        int minuteUnit = 1;
        std::shared_ptr<const std::vector<Bar>> completedBars;
        Bar liveBar;
        bool hasLiveBar = false;
        std::size_t barCount = 0;
        std::uint64_t revision = 0;
        std::uint64_t completedRevision = 0;
        std::uint64_t liveRevision = 0;
    };

    struct MarketDataApplyResult final
    {
        bool applied = false;
        bool stale = false;
        std::string code;
        PriceWon latestPriceWon = 0;
        EpochMillis eventTimestampMs = 0;
        std::string error;
    };

    class MarketDataModule final
    {
    public:
        MarketDataModule();
        MarketDataModule(const MarketDataModule&) = delete;
        MarketDataModule& operator=(const MarketDataModule&) = delete;

        bool SetLevel(
            FeatureLevel level,
            std::string& error);

        FeatureLevel Level() const noexcept;

        bool BeginRequest(
            const std::string& code,
            int minuteUnit,
            std::string& error);

        MarketDataApplyResult ApplyMinuteBars(
            const MinuteBarsPage& page,
            const Continuation& continuation);

        MarketDataApplyResult ApplyStockTradeTick(
            const StockTradeTick& tick);

        void SetError(const std::string& error);

        void SetStockTradeSubscriptionRequested(bool requested) noexcept;

        bool TryGetLatestQuote(
            std::string& code,
            PriceWon& priceWon) const;

        MarketDataSnapshot Snapshot() const;

        MarketDataSeriesSnapshot SeriesSnapshot() const;

        std::vector<Bar> CopyVisibleBars(
            std::size_t maximumCount) const;

        static const char* StateName(MarketDataState state) noexcept;

    private:
        static EpochMillis KstSessionDateStart(
            EpochMillis timestampMs) noexcept;

        static std::shared_ptr<const std::vector<Bar>> EmptyCompletedBars();

        mutable std::mutex mutex_;
        MarketDataState state_ = MarketDataState::Error;
        FeatureLevel level_ = FeatureLevel::Visible;
        std::string code_;
        int minuteUnit_ = 1;
        std::shared_ptr<const std::vector<Bar>> completedBars_;
        Bar liveBar_;
        bool hasLiveBar_ = false;
        Continuation continuation_;
        std::string error_;
        std::uint64_t revision_ = 0;
        std::uint64_t completedRevision_ = 0;
        std::uint64_t liveRevision_ = 0;
        std::atomic<std::uint64_t> stockTradeTickCount_{ 0 };
        std::atomic<EpochMillis> lastStockTradeTimestampMs_{ 0 };
        std::atomic<bool> stockTradeSubscriptionRequested_{ false };
    };
}
