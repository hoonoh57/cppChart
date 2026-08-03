#include "market_data_module.h"

#include <algorithm>
#include <utility>

namespace trading::app
{
    MarketDataModule::MarketDataModule()
        : error_(
            "실제 시세 백필과 실시간 체결 수신이 연결되지 않았습니다. "
            "합성 데이터는 제거되었으며 오류를 숨기지 않습니다.")
    {
    }

    bool MarketDataModule::SetLevel(
        FeatureLevel level,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        level_ = level;
        if (level == FeatureLevel::Off) {
            code_.clear();
            bars_.clear();
            continuation_ = {};
            state_ = MarketDataState::Disconnected;
            error_.clear();
            ++revision_;
            stockTradeTickCount_.store(0, std::memory_order_release);
            lastStockTradeTimestampMs_.store(0, std::memory_order_release);
            stockTradeSubscriptionRequested_.store(
                false,
                std::memory_order_release);
        }

        error.clear();
        return true;
    }

    FeatureLevel MarketDataModule::Level() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return level_;
    }

    bool MarketDataModule::BeginRequest(
        const std::string& code,
        int minuteUnit,
        std::string& error)
    {
        if (code.empty()) {
            error = "종목코드가 비어 있습니다.";
            return false;
        }
        if (minuteUnit <= 0) {
            error = "분봉 단위가 올바르지 않습니다.";
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (level_ == FeatureLevel::Off) {
            error = "시장 데이터 기능이 Off 상태입니다.";
            return false;
        }

        code_ = code;
        minuteUnit_ = minuteUnit;
        bars_.clear();
        continuation_ = {};
        error_.clear();
        state_ = MarketDataState::Loading;
        ++revision_;
        stockTradeTickCount_.store(0, std::memory_order_release);
        lastStockTradeTimestampMs_.store(0, std::memory_order_release);
        stockTradeSubscriptionRequested_.store(
            false,
            std::memory_order_release);
        error.clear();
        return true;
    }

    MarketDataApplyResult MarketDataModule::ApplyMinuteBars(
        const MinuteBarsPage& page,
        const Continuation& continuation)
    {
        MarketDataApplyResult result;

        if (!page.result.ok) {
            result.error = !page.result.error.empty()
                ? page.result.error
                : page.result.returnMessage;
            if (result.error.empty()) {
                result.error = "실제 분봉 응답을 해석하지 못했습니다.";
            }
            SetError(result.error);
            return result;
        }
        if (page.bars.empty()) {
            result.error = "실제 분봉 응답이 비어 있습니다.";
            SetError(result.error);
            return result;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (level_ == FeatureLevel::Off) {
            result.error = "시장 데이터 기능이 Off 상태입니다.";
            return result;
        }
        if (!code_.empty() && page.code != code_) {
            result.stale = true;
            result.error = "이전 종목의 분봉 응답을 무시했습니다.";
            return result;
        }

        code_ = page.code;
        minuteUnit_ = page.minuteUnit;
        bars_ = page.bars;
        continuation_ = continuation;
        error_.clear();
        state_ = MarketDataState::Ready;
        ++revision_;

        result.applied = true;
        result.code = code_;
        result.latestPriceWon = bars_.back().close;
        result.eventTimestampMs = bars_.back().closeTimestampMs;
        return result;
    }

    MarketDataApplyResult MarketDataModule::ApplyStockTradeTick(
        const StockTradeTick& tick)
    {
        MarketDataApplyResult result;

        std::lock_guard<std::mutex> lock(mutex_);
        if (level_ == FeatureLevel::Off) {
            result.error = "시장 데이터 기능이 Off 상태입니다.";
            return result;
        }
        if (code_.empty() || bars_.empty() || code_ != tick.code) {
            result.stale = true;
            result.error = "현재 선택 종목과 다른 실시간 체결입니다.";
            return result;
        }

        const EpochMillis sessionStart =
            KstSessionDateStart(bars_.back().closeTimestampMs);

        std::string mergeError;
        if (!MergeStockTradeIntoMinuteBars(
                bars_,
                minuteUnit_,
                sessionStart,
                tick,
                mergeError))
        {
            result.stale =
                mergeError.find("stale stock trade") != std::string::npos;
            result.error = mergeError;
            return result;
        }

        const int hour = tick.tradeTimeHhmmss / 10000;
        const int minute = (tick.tradeTimeHhmmss / 100) % 100;
        const int second = tick.tradeTimeHhmmss % 100;
        const EpochMillis eventTimestampMs =
            sessionStart +
            static_cast<EpochMillis>(hour) * 3600000LL +
            static_cast<EpochMillis>(minute) * 60000LL +
            static_cast<EpochMillis>(second) * 1000LL;

        ++revision_;
        stockTradeTickCount_.fetch_add(1, std::memory_order_acq_rel);
        lastStockTradeTimestampMs_.store(
            eventTimestampMs,
            std::memory_order_release);

        result.applied = true;
        result.code = code_;
        result.latestPriceWon = bars_.back().close;
        result.eventTimestampMs = eventTimestampMs;
        return result;
    }

    void MarketDataModule::SetError(const std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = error;
        state_ = MarketDataState::Error;
        ++revision_;
    }

    void MarketDataModule::SetStockTradeSubscriptionRequested(
        bool requested) noexcept
    {
        stockTradeSubscriptionRequested_.store(
            requested,
            std::memory_order_release);
    }

    bool MarketDataModule::TryGetLatestQuote(
        std::string& code,
        PriceWon& priceWon) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (
            level_ == FeatureLevel::Off ||
            state_ != MarketDataState::Ready ||
            code_.empty() ||
            bars_.empty())
        {
            return false;
        }

        code = code_;
        priceWon = bars_.back().close;
        return IsValidPrice(priceWon);
    }

    MarketDataSnapshot MarketDataModule::Snapshot() const
    {
        MarketDataSnapshot result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result.state = state_;
            result.level = level_;
            result.code = code_;
            result.minuteUnit = minuteUnit_;
            result.barCount = bars_.size();
            result.continuation = continuation_;
            result.error = error_;
            result.revision = revision_;
            result.retainedBytes =
                bars_.capacity() * sizeof(Bar) +
                code_.capacity() +
                error_.capacity() +
                continuation_.continueYn.capacity() +
                continuation_.nextKey.capacity();
            if (!bars_.empty()) {
                result.latestBar = bars_.back();
                result.hasLatestBar = true;
            }
        }

        result.stockTradeTickCount =
            stockTradeTickCount_.load(std::memory_order_acquire);
        result.lastStockTradeTimestampMs =
            lastStockTradeTimestampMs_.load(std::memory_order_acquire);
        result.stockTradeSubscriptionRequested =
            stockTradeSubscriptionRequested_.load(
                std::memory_order_acquire);
        return result;
    }

    std::vector<Bar> MarketDataModule::CopyVisibleBars(
        std::size_t maximumCount) const
    {
        if (maximumCount == 0) return {};

        std::lock_guard<std::mutex> lock(mutex_);
        if (level_ == FeatureLevel::Off || bars_.empty()) return {};

        const std::size_t count =
            (std::min)(maximumCount, bars_.size());
        const auto first = bars_.end() -
            static_cast<std::ptrdiff_t>(count);
        return std::vector<Bar>(first, bars_.end());
    }

    const char* MarketDataModule::StateName(
        MarketDataState state) noexcept
    {
        switch (state) {
        case MarketDataState::Loading: return "실시세 로딩";
        case MarketDataState::Ready: return "실시세 준비";
        case MarketDataState::Error: return "실시세 오류";
        default: return "실시세 미연결";
        }
    }

    EpochMillis MarketDataModule::KstSessionDateStart(
        EpochMillis timestampMs) noexcept
    {
        constexpr EpochMillis DayMs =
            24LL * 60LL * 60LL * 1000LL;
        constexpr EpochMillis KstOffsetMs =
            9LL * 60LL * 60LL * 1000LL;
        return
            ((timestampMs + KstOffsetMs) / DayMs) * DayMs -
            KstOffsetMs;
    }
}
