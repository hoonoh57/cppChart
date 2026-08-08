#include "market_data_module.h"

#include <algorithm>
#include <utility>

namespace trading::app
{
    namespace
    {
        bool IsVisibleLevel(FeatureLevel level) noexcept
        {
            return
                level == FeatureLevel::Visible ||
                level == FeatureLevel::Active;
        }

        std::size_t DynamicStringBytes(const std::string& value) noexcept
        {
            return value.empty() ? 0 : value.capacity();
        }
    }

    MarketDataModule::MarketDataModule()
        : completedBars_(EmptyCompletedBars()),
          error_(
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
            completedBars_ = EmptyCompletedBars();
            liveBar_ = {};
            hasLiveBar_ = false;
            continuation_ = {};
            state_ = MarketDataState::Disconnected;
            error_.clear();
            ++revision_;
            ++completedRevision_;
            ++liveRevision_;
            stockTradeTickCount_.store(0, std::memory_order_release);
            lastStockTradeTimestampMs_.store(0, std::memory_order_release);
            stockTradeSubscriptionRequested_.store(
                false,
                std::memory_order_release);
        }
        else if (level == FeatureLevel::Standby) {
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
        if (!IsVisibleLevel(level_)) {
            error =
                level_ == FeatureLevel::Off
                    ? "시장 데이터 기능이 Off 상태입니다."
                    : "시장 데이터 기능이 Standby 상태입니다.";
            return false;
        }

        code_ = code;
        minuteUnit_ = minuteUnit;
        completedBars_ = EmptyCompletedBars();
        liveBar_ = {};
        hasLiveBar_ = false;
        continuation_ = {};
        error_.clear();
        state_ = MarketDataState::Loading;
        ++revision_;
        ++completedRevision_;
        ++liveRevision_;
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

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!IsVisibleLevel(level_)) {
                result.stale = true;
                result.error =
                    "비활성 시장 데이터 기능의 분봉 응답을 무시했습니다.";
                return result;
            }
        }

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

        auto completed = std::make_shared<std::vector<Bar>>();
        if (page.bars.size() > 1) {
            completed->assign(page.bars.begin(), page.bars.end() - 1);
        }
        std::shared_ptr<const std::vector<Bar>> immutableCompleted =
            std::move(completed);
        const Bar live = page.bars.back();

        std::lock_guard<std::mutex> lock(mutex_);
        if (!IsVisibleLevel(level_)) {
            result.stale = true;
            result.error =
                "수신 중 기능 수준이 변경되어 분봉 응답을 무시했습니다.";
            return result;
        }
        if (!code_.empty() && page.code != code_) {
            result.stale = true;
            result.error = "이전 종목의 분봉 응답을 무시했습니다.";
            return result;
        }

        code_ = page.code;
        minuteUnit_ = page.minuteUnit;
        completedBars_ = std::move(immutableCompleted);
        liveBar_ = live;
        hasLiveBar_ = true;
        continuation_ = continuation;
        error_.clear();
        state_ = MarketDataState::Ready;
        ++revision_;
        ++completedRevision_;
        ++liveRevision_;

        result.applied = true;
        result.code = code_;
        result.latestPriceWon = liveBar_.close;
        result.eventTimestampMs = liveBar_.closeTimestampMs;
        return result;
    }

    MarketDataApplyResult MarketDataModule::ApplyStockTradeTick(
        const StockTradeTick& tick)
    {
        MarketDataApplyResult result;

        std::lock_guard<std::mutex> lock(mutex_);
        if (!IsVisibleLevel(level_)) {
            result.stale = true;
            result.error =
                "비활성 시장 데이터 기능의 실시간 체결을 무시했습니다.";
            return result;
        }
        if (code_.empty() || !hasLiveBar_ || code_ != tick.code) {
            result.stale = true;
            result.error = "현재 선택 종목과 다른 실시간 체결입니다.";
            return result;
        }

        const EpochMillis sessionStart =
            KstSessionDateStart(liveBar_.closeTimestampMs);

        std::vector<Bar> liveWindow;
        liveWindow.reserve(2);
        liveWindow.push_back(liveBar_);

        std::string mergeError;
        if (!MergeStockTradeIntoMinuteBars(
                liveWindow,
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

        if (liveWindow.size() == 1) {
            liveBar_ = liveWindow.front();
        }
        else if (liveWindow.size() == 2) {
            auto nextCompleted = std::make_shared<std::vector<Bar>>();
            if (completedBars_) {
                *nextCompleted = *completedBars_;
            }
            nextCompleted->push_back(liveWindow.front());
            completedBars_ = std::move(nextCompleted);
            liveBar_ = liveWindow.back();
            ++completedRevision_;
        }
        else {
            result.error =
                "실시간 체결 병합 결과가 2개 이상의 새 분봉을 생성했습니다.";
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
        ++liveRevision_;
        stockTradeTickCount_.fetch_add(1, std::memory_order_acq_rel);
        lastStockTradeTimestampMs_.store(
            eventTimestampMs,
            std::memory_order_release);

        result.applied = true;
        result.code = code_;
        result.latestPriceWon = liveBar_.close;
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
        std::lock_guard<std::mutex> lock(mutex_);
        stockTradeSubscriptionRequested_.store(
            requested && IsVisibleLevel(level_),
            std::memory_order_release);
    }

    bool MarketDataModule::TryGetLatestQuote(
        std::string& code,
        PriceWon& priceWon) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (
            !IsVisibleLevel(level_) ||
            state_ != MarketDataState::Ready ||
            code_.empty() ||
            !hasLiveBar_)
        {
            return false;
        }

        code = code_;
        priceWon = liveBar_.close;
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
            result.barCount =
                (completedBars_ ? completedBars_->size() : 0U) +
                (hasLiveBar_ ? 1U : 0U);
            result.continuation = continuation_;
            result.error = error_;
            result.revision = revision_;
            result.completedRevision = completedRevision_;
            result.liveRevision = liveRevision_;
            result.retainedBytes =
                (completedBars_ ?
                    completedBars_->capacity() * sizeof(Bar) : 0U) +
                (hasLiveBar_ ? sizeof(Bar) : 0U) +
                DynamicStringBytes(code_) +
                DynamicStringBytes(error_) +
                DynamicStringBytes(continuation_.continueYn) +
                DynamicStringBytes(continuation_.nextKey);
            if (hasLiveBar_) {
                result.latestBar = liveBar_;
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

    MarketDataSeriesSnapshot MarketDataModule::SeriesSnapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        MarketDataSeriesSnapshot result;
        result.state = state_;
        result.level = level_;
        result.code = code_;
        result.minuteUnit = minuteUnit_;
        result.completedBars = completedBars_;
        result.liveBar = liveBar_;
        result.hasLiveBar = hasLiveBar_;
        result.barCount =
            (completedBars_ ? completedBars_->size() : 0U) +
            (hasLiveBar_ ? 1U : 0U);
        result.revision = revision_;
        result.completedRevision = completedRevision_;
        result.liveRevision = liveRevision_;
        return result;
    }

    std::vector<Bar> MarketDataModule::CopyVisibleBars(
        std::size_t maximumCount) const
    {
        if (maximumCount == 0) return {};

        std::lock_guard<std::mutex> lock(mutex_);
        if (!IsVisibleLevel(level_) || !hasLiveBar_) return {};

        const std::size_t completedCount =
            completedBars_ ? completedBars_->size() : 0U;
        const std::size_t totalCount = completedCount + 1U;
        const std::size_t count =
            (std::min)(maximumCount, totalCount);
        const std::size_t completedToCopy =
            count > 0 ? count - 1U : 0U;

        std::vector<Bar> result;
        result.reserve(count);
        if (completedToCopy > 0 && completedBars_) {
            const auto first = completedBars_->end() -
                static_cast<std::ptrdiff_t>(completedToCopy);
            result.insert(result.end(), first, completedBars_->end());
        }
        result.push_back(liveBar_);
        return result;
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

    std::shared_ptr<const std::vector<Bar>>
    MarketDataModule::EmptyCompletedBars()
    {
        static const auto empty =
            std::make_shared<const std::vector<Bar>>();
        return empty;
    }
}
