from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text("\ufeff" + text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


# ---------------------------------------------------------------------------
# core/kiwoom_market_data.h
# ---------------------------------------------------------------------------
path = "core/kiwoom_market_data.h"
text = read(path)
anchor = '''    struct MinuteBarsPage final
    {
        ProtocolResult result;
        MinuteBarInstrument instrument = MinuteBarInstrument::Stock;
        std::string code;
        int minuteUnit = 1;
        std::vector<Bar> bars;
    };

'''
addition = anchor + '''    struct StockTradeTick final
    {
        std::string code;
        PriceWon priceWon = 0;
        Volume tradeVolume = 0;
        Volume cumulativeVolume = 0;
        int tradeTimeHhmmss = 0;
    };

    struct StockTradeDecodeResult final
    {
        ProtocolResult result;
        StockTradeTick tick;
    };

'''
text = replace_once(text, anchor, addition, "market-data stock trade types")
anchor = '''    MinuteBarsPage ParseIndexMinuteBarsResponse(
        const std::string& indexCode,
        int minuteUnit,
        const std::string& json);
'''
addition = anchor + '''

    StockTradeDecodeResult DecodeStockTradeRecord(
        const RealTimeRecord& record);

    bool MergeStockTradeIntoMinuteBars(
        std::vector<Bar>& bars,
        int minuteUnit,
        EpochMillis sessionDateStartMs,
        const StockTradeTick& tick,
        std::string& error);
'''
text = replace_once(text, anchor, addition, "market-data stock trade declarations")
write(path, text)


# ---------------------------------------------------------------------------
# core/kiwoom_market_data.cpp
# ---------------------------------------------------------------------------
path = "core/kiwoom_market_data.cpp"
text = read(path)
anchor = '''    MinuteBarsPage ParseIndexMinuteBarsResponse(
        const std::string& indexCode,
        int minuteUnit,
        const std::string& json)
    {
        return ParseMinuteBarsResponse(
            MinuteBarInstrument::Index,
            indexCode,
            minuteUnit,
            "inds_min_pole_qry",
            json);
    }
}'''
addition = '''    MinuteBarsPage ParseIndexMinuteBarsResponse(
        const std::string& indexCode,
        int minuteUnit,
        const std::string& json)
    {
        return ParseMinuteBarsResponse(
            MinuteBarInstrument::Index,
            indexCode,
            minuteUnit,
            "inds_min_pole_qry",
            json);
    }

    StockTradeDecodeResult DecodeStockTradeRecord(
        const RealTimeRecord& record)
    {
        StockTradeDecodeResult decoded;
        if (record.type != "0B") {
            decoded.result.error = "real-time record is not stock trade type 0B";
            return decoded;
        }
        if (record.item.empty()) {
            decoded.result.error = "stock trade item code is missing";
            return decoded;
        }

        const auto readSigned = [&record](
            const char* key,
            std::int64_t& value) -> bool {
            const auto found = record.values.find(key);
            if (found == record.values.end()) return false;

            std::string normalized;
            normalized.reserve(found->second.size());
            for (char ch : found->second) {
                if (ch != ',' &&
                    std::isspace(static_cast<unsigned char>(ch)) == 0)
                {
                    normalized.push_back(ch);
                }
            }
            if (normalized.empty()) return false;

            try {
                std::size_t consumed = 0;
                const long long parsed =
                    std::stoll(normalized, &consumed, 10);
                if (consumed != normalized.size()) return false;
                value = static_cast<std::int64_t>(parsed);
                return true;
            }
            catch (...) {
                return false;
            }
        };

        std::int64_t timeValue = 0;
        std::int64_t priceValue = 0;
        std::int64_t volumeValue = 0;
        std::int64_t cumulativeValue = 0;

        if (!readSigned("20", timeValue)) {
            decoded.result.error = "stock trade time FID 20 is missing or invalid";
            return decoded;
        }
        if (!readSigned("10", priceValue)) {
            decoded.result.error = "stock trade price FID 10 is missing or invalid";
            return decoded;
        }
        if (!readSigned("15", volumeValue)) {
            decoded.result.error = "stock trade volume FID 15 is missing or invalid";
            return decoded;
        }

        const auto cumulative = record.values.find("13");
        if (cumulative != record.values.end() &&
            !readSigned("13", cumulativeValue))
        {
            decoded.result.error =
                "stock cumulative volume FID 13 is invalid";
            return decoded;
        }

        const std::int64_t absolutePrice =
            priceValue < 0 ? -priceValue : priceValue;
        const std::int64_t absoluteVolume =
            volumeValue < 0 ? -volumeValue : volumeValue;
        const std::int64_t absoluteCumulative =
            cumulativeValue < 0 ? -cumulativeValue : cumulativeValue;

        const int hhmmss = static_cast<int>(timeValue);
        const int hour = hhmmss / 10000;
        const int minute = (hhmmss / 100) % 100;
        const int second = hhmmss % 100;

        if (hour < 0 || hour > 23 ||
            minute < 0 || minute > 59 ||
            second < 0 || second > 59)
        {
            decoded.result.error = "stock trade time FID 20 is out of range";
            return decoded;
        }
        if (absolutePrice <= 0 ||
            absolutePrice > (std::numeric_limits<PriceWon>::max)())
        {
            decoded.result.error = "stock trade price is out of range";
            return decoded;
        }
        if (absoluteVolume <= 0) {
            decoded.result.error = "stock trade volume must be positive";
            return decoded;
        }

        decoded.tick.code = record.item;
        if (!decoded.tick.code.empty() &&
            decoded.tick.code.front() == 'A')
        {
            decoded.tick.code.erase(decoded.tick.code.begin());
        }
        decoded.tick.priceWon = static_cast<PriceWon>(absolutePrice);
        decoded.tick.tradeVolume = static_cast<Volume>(absoluteVolume);
        decoded.tick.cumulativeVolume =
            static_cast<Volume>(absoluteCumulative);
        decoded.tick.tradeTimeHhmmss = hhmmss;
        decoded.result.ok = true;
        decoded.result.returnCode = 0;
        return decoded;
    }

    bool MergeStockTradeIntoMinuteBars(
        std::vector<Bar>& bars,
        int minuteUnit,
        EpochMillis sessionDateStartMs,
        const StockTradeTick& tick,
        std::string& error)
    {
        if (bars.empty()) {
            error = "minute-bar backfill is required before stock trade merge";
            return false;
        }
        if (!IsSupportedMinuteUnit(minuteUnit)) {
            error = "unsupported minute unit for stock trade merge";
            return false;
        }
        if (sessionDateStartMs <= 0) {
            error = "session date start is required for stock trade merge";
            return false;
        }
        if (!IsValidPrice(tick.priceWon) || tick.tradeVolume <= 0) {
            error = "stock trade price and volume must be positive";
            return false;
        }

        const int hour = tick.tradeTimeHhmmss / 10000;
        const int minute = (tick.tradeTimeHhmmss / 100) % 100;
        const int second = tick.tradeTimeHhmmss % 100;
        if (hour < 0 || hour > 23 ||
            minute < 0 || minute > 59 ||
            second < 0 || second > 59)
        {
            error = "stock trade time is out of range";
            return false;
        }

        constexpr EpochMillis SecondMs = 1000;
        constexpr EpochMillis MinuteMs = 60 * SecondMs;
        const EpochMillis eventTimestampMs =
            sessionDateStartMs +
            static_cast<EpochMillis>(hour) * 60 * MinuteMs +
            static_cast<EpochMillis>(minute) * MinuteMs +
            static_cast<EpochMillis>(second) * SecondMs;
        const EpochMillis intervalMs =
            static_cast<EpochMillis>(minuteUnit) * MinuteMs;
        const EpochMillis bucketTimestampMs =
            sessionDateStartMs +
            ((eventTimestampMs - sessionDateStartMs) / intervalMs) *
                intervalMs;

        Bar& last = bars.back();
        if (bucketTimestampMs < last.closeTimestampMs) {
            error = "stale stock trade precedes the latest minute bar";
            return false;
        }

        if (bucketTimestampMs == last.closeTimestampMs) {
            last.high = (std::max)(last.high, tick.priceWon);
            last.low = (std::min)(last.low, tick.priceWon);
            last.close = tick.priceWon;
            last.volume += tick.tradeVolume;
            if (last.tickCount < (std::numeric_limits<TickCount>::max)()) {
                ++last.tickCount;
            }
        }
        else {
            Bar bar;
            bar.open = tick.priceWon;
            bar.high = tick.priceWon;
            bar.low = tick.priceWon;
            bar.close = tick.priceWon;
            bar.volume = tick.tradeVolume;
            bar.closeTimestampMs = bucketTimestampMs;
            bar.tickCount = 1;
            bars.push_back(bar);
        }

        error.clear();
        return true;
    }
}'''
text = replace_once(text, anchor, addition, "stock trade decoder and aggregator")
write(path, text)


# ---------------------------------------------------------------------------
# platform/kiwoom_runtime_runner.h
# ---------------------------------------------------------------------------
path = "platform/kiwoom_runtime_runner.h"
text = read(path)
anchor = '''        std::function<void(
            const MinuteBarsPage& page,
            const Continuation& continuation)> minuteBars;
'''
addition = anchor + '''        std::function<void(
            const StockTradeTick& tick)> stockTrade;
'''
text = replace_once(text, anchor, addition, "runner stock trade callback")
anchor = '''        bool RequestIndexMinuteBars(
            const std::string& indexCode,
            int minuteUnit,
            const Continuation& continuation,
            std::string& error);

'''
addition = anchor + '''        bool SubscribeStockTrades(
            const std::string& stockCode,
            std::string& error);

'''
text = replace_once(text, anchor, addition, "runner stock subscription API")
anchor = '''        void StartReceiver();

        void StopReceiver();
'''
addition = '''        void TryQueueStockTradeSubscription();

        void StartReceiver();

        void StopReceiver();
'''
text = replace_once(text, anchor, addition, "runner subscription helper")
anchor = '''        std::thread workerThread_;
        std::thread receiverThread_;
'''
addition = '''        std::thread workerThread_;
        std::thread receiverThread_;

        std::mutex subscriptionMutex_;
        std::string stockTradeCode_;
        bool stockTradeSubscriptionSent_ = false;
'''
text = replace_once(text, anchor, addition, "runner subscription state")
write(path, text)


# ---------------------------------------------------------------------------
# platform/kiwoom_runtime_runner.cpp
# ---------------------------------------------------------------------------
path = "platform/kiwoom_runtime_runner.cpp"
text = read(path)
anchor = '''        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queue_.clear();
            reconnectScheduled_ = false;
        }

        workerThread_ = std::thread(&KiwoomRuntimeRunner::WorkerLoop, this);
'''
addition = '''        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queue_.clear();
            reconnectScheduled_ = false;
        }
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            stockTradeCode_.clear();
            stockTradeSubscriptionSent_ = false;
        }

        workerThread_ = std::thread(&KiwoomRuntimeRunner::WorkerLoop, this);
'''
text = replace_once(text, anchor, addition, "runner start subscription reset")
anchor = '''        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queue_.clear();
            reconnectScheduled_ = false;
        }

        Log("SYS", "Kiwoom runtime stopped");
'''
addition = '''        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queue_.clear();
            reconnectScheduled_ = false;
        }
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            stockTradeCode_.clear();
            stockTradeSubscriptionSent_ = false;
        }

        Log("SYS", "Kiwoom runtime stopped");
'''
text = replace_once(text, anchor, addition, "runner stop subscription reset")
anchor = '''    bool KiwoomRuntimeRunner::SubmitOrder(
        const OrderIntent& intent,
        std::string& error)
'''
addition = '''    bool KiwoomRuntimeRunner::SubscribeStockTrades(
        const std::string& stockCode,
        std::string& error)
    {
        if (!running_.load(std::memory_order_acquire)) {
            error = "Kiwoom runtime is not running";
            return false;
        }
        if (stockCode.empty()) {
            error = "stock code is required for real-time subscription";
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            if (stockTradeCode_ != stockCode) {
                stockTradeCode_ = stockCode;
                stockTradeSubscriptionSent_ = false;
            }
        }

        TryQueueStockTradeSubscription();
        error.clear();
        return true;
    }

    bool KiwoomRuntimeRunner::SubmitOrder(
        const OrderIntent& intent,
        std::string& error)
'''
text = replace_once(text, anchor, addition, "runner stock subscription method")
anchor = '''            if (received.kind == RuntimeReceiveKind::Text) {
                Enqueue(engine_.OnWebSocketMessage(received.text));
                WakeUi();
                continue;
            }
'''
addition = '''            if (received.kind == RuntimeReceiveKind::Text) {
                const RealTimeEnvelope envelope =
                    ParseRealTimeEnvelope(received.text);
                if (envelope.result.ok) {
                    for (const RealTimeRecord& record : envelope.records) {
                        if (record.type != "0B") continue;

                        const StockTradeDecodeResult decoded =
                            DecodeStockTradeRecord(record);
                        if (!decoded.result.ok) {
                            Log(
                                "FAULT",
                                decoded.result.error.empty()
                                    ? "stock trade 0B decode failed"
                                    : decoded.result.error);
                            continue;
                        }
                        if (callbacks_.stockTrade) {
                            callbacks_.stockTrade(decoded.tick);
                        }
                    }
                }

                Enqueue(engine_.OnWebSocketMessage(received.text));
                TryQueueStockTradeSubscription();
                WakeUi();
                continue;
            }
'''
text = replace_once(text, anchor, addition, "runner 0B delivery")
anchor = '''            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                reconnectScheduled_ = false;
            }

            StartReceiver();
'''
addition = '''            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                reconnectScheduled_ = false;
            }
            {
                std::lock_guard<std::mutex> lock(subscriptionMutex_);
                stockTradeSubscriptionSent_ = false;
            }

            StartReceiver();
'''
text = replace_once(text, anchor, addition, "runner reconnect subscription reset")
anchor = '''        case KiwoomRuntimeActionType::EnterObserveMode:
            if (callbacks_.setObserveMode) {
                callbacks_.setObserveMode(true);
            }
            Log("FAULT", action.text.empty()
                ? "observe mode requested"
                : action.text);
            WakeUi();
            break;
        }
    }

    void KiwoomRuntimeRunner::StartReceiver()
'''
addition = '''        case KiwoomRuntimeActionType::EnterObserveMode:
            if (callbacks_.setObserveMode) {
                callbacks_.setObserveMode(true);
            }
            Log("FAULT", action.text.empty()
                ? "observe mode requested"
                : action.text);
            WakeUi();
            break;
        }

        TryQueueStockTradeSubscription();
    }

    void KiwoomRuntimeRunner::TryQueueStockTradeSubscription()
    {
        if (!running_.load(std::memory_order_acquire)) return;
        if (!engine_.Snapshot().orderSubmissionAllowed) return;
        if (!transport_->IsWebSocketConnected()) return;

        std::string code;
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            if (stockTradeCode_.empty() || stockTradeSubscriptionSent_) {
                return;
            }
            code = stockTradeCode_;
            stockTradeSubscriptionSent_ = true;
        }

        KiwoomRuntimeAction action;
        action.type = KiwoomRuntimeActionType::SendWebSocketText;
        action.text = BuildWebSocketRegistrationMessage(
            "2",
            false,
            { code },
            { "0B" });
        Enqueue({ std::move(action) });
        Log("WS", "stock trade 0B subscription queued: " + code);
    }

    void KiwoomRuntimeRunner::StartReceiver()
'''
text = replace_once(text, anchor, addition, "runner subscription queue helper")
write(path, text)


# ---------------------------------------------------------------------------
# shell_main.cpp
# ---------------------------------------------------------------------------
path = "shell_main.cpp"
text = read(path)
text = replace_once(
    text,
    '#include <cstdint>\n#include <memory>',
    '#include <cstdint>\n#include <chrono>\n#include <memory>',
    "shell chrono include",
)
text = replace_once(
    text,
    'static std::atomic<int> g_wakeFrames{60};',
    'static std::atomic<int> g_wakeFrames{4};',
    "shell initial wake frames",
)
text = replace_once(
    text,
    '''static FaultPolicy g_faultPolicy(
    &g_observeMode,
    &g_wakeFrames,
    [](const char* category, const char* message) {
        g_log.Add(category, "%s", message);
    });

static CommandBus g_commandBus(&g_wakeFrames);
''',
    '''static FaultPolicy g_faultPolicy(
    &g_observeMode,
    &g_wakeFrames,
    [](const char* category, const char* message) {
        g_log.Add(category, "%s", message);
    },
    4);

static CommandBus g_commandBus(&g_wakeFrames, 4);
''',
    "shell bounded wake configuration",
)
anchor = '''static double NowSeconds()
{
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value;
    }();

    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return
        static_cast<double>(counter.QuadPart) /
        static_cast<double>(frequency.QuadPart);
}

'''
addition = anchor + '''static trading::EpochMillis SystemNowEpochMillis()
{
    return static_cast<trading::EpochMillis>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

static double g_renderRateHz = 0.0;
static double g_renderRateWindowStart = 0.0;
static std::uint64_t g_renderRateFrameCount = 0;

static void RecordPresentedFrame()
{
    const double now = NowSeconds();
    if (g_renderRateWindowStart <= 0.0) {
        g_renderRateWindowStart = now;
    }
    ++g_renderRateFrameCount;

    const double elapsed = now - g_renderRateWindowStart;
    if (elapsed >= 1.0) {
        g_renderRateHz =
            static_cast<double>(g_renderRateFrameCount) / elapsed;
        g_renderRateFrameCount = 0;
        g_renderRateWindowStart = now;
    }
}

'''
text = replace_once(text, anchor, addition, "shell measured render rate")
anchor = '''static std::atomic<MarketDataState> g_marketDataState{
    MarketDataState::Error};

'''
addition = anchor + '''static std::atomic<std::uint64_t> g_stockTradeTickCount{0};
static std::atomic<trading::EpochMillis> g_lastStockTradeTimestampMs{0};
static std::atomic<bool> g_stockTradeSubscriptionRequested{false};

'''
text = replace_once(text, anchor, addition, "shell real-time status")
anchor = '''    g_marketDataState.store(
        MarketDataState::Loading,
        std::memory_order_release);
    WakeFrames(60);
}

static void ApplyMinuteBars(
'''
addition = '''    g_stockTradeTickCount.store(0, std::memory_order_release);
    g_lastStockTradeTimestampMs.store(0, std::memory_order_release);
    g_stockTradeSubscriptionRequested.store(false, std::memory_order_release);
    g_marketDataState.store(
        MarketDataState::Loading,
        std::memory_order_release);
    WakeFrames(4);
}

static trading::EpochMillis KstSessionDateStart(
    trading::EpochMillis timestampMs) noexcept
{
    constexpr trading::EpochMillis DayMs = 24LL * 60LL * 60LL * 1000LL;
    constexpr trading::EpochMillis KstOffsetMs = 9LL * 60LL * 60LL * 1000LL;
    return
        ((timestampMs + KstOffsetMs) / DayMs) * DayMs -
        KstOffsetMs;
}

static void ApplyStockTradeTick(
    const trading::StockTradeTick& tick)
{
    trading::PriceWon latestPrice = 0;
    std::string code;
    trading::EpochMillis eventTimestampMs = 0;

    {
        std::lock_guard<std::mutex> lock(g_marketDataMutex);
        if (
            g_marketDataView.code.empty() ||
            g_marketDataView.code != tick.code ||
            g_marketDataView.bars.empty())
        {
            return;
        }

        const trading::EpochMillis sessionStart =
            KstSessionDateStart(
                g_marketDataView.bars.back().closeTimestampMs);
        std::string error;
        if (!trading::MergeStockTradeIntoMinuteBars(
                g_marketDataView.bars,
                g_marketDataView.minuteUnit,
                sessionStart,
                tick,
                error))
        {
            if (error.find("stale stock trade") == std::string::npos) {
                g_log.Add("FAULT", "0B 분봉 병합 실패: %s", error.c_str());
            }
            return;
        }

        const int hour = tick.tradeTimeHhmmss / 10000;
        const int minute = (tick.tradeTimeHhmmss / 100) % 100;
        const int second = tick.tradeTimeHhmmss % 100;
        eventTimestampMs =
            sessionStart +
            static_cast<trading::EpochMillis>(hour) * 3600000LL +
            static_cast<trading::EpochMillis>(minute) * 60000LL +
            static_cast<trading::EpochMillis>(second) * 1000LL;
        code = g_marketDataView.code;
        latestPrice = g_marketDataView.bars.back().close;
    }

    g_stockTradeTickCount.fetch_add(1, std::memory_order_acq_rel);
    g_lastStockTradeTimestampMs.store(
        eventTimestampMs,
        std::memory_order_release);
    g_tradingState.UpdateCurrentPrice(code, latestPrice);
    WakeFrames(2);
}

static void ApplyMinuteBars(
'''
text = replace_once(text, anchor, addition, "shell 0B aggregation")
text = text.replace('WakeFrames(60);', 'WakeFrames(4);')
text = text.replace('ka10079', 'ka10080')
old_toolbar = '''    ImGui::Text(
        "부팅 %.0fms   %.1ffps",
        g_bootMilliseconds,
        ImGui::GetIO().Framerate);
'''
new_toolbar = '''    const std::uint64_t realTimeTicks =
        g_stockTradeTickCount.load(std::memory_order_acquire);
    const trading::EpochMillis lastTradeTimestamp =
        g_lastStockTradeTimestampMs.load(std::memory_order_acquire);
    const trading::EpochMillis tradeAgeMs = lastTradeTimestamp > 0
        ? (std::max)(
            static_cast<trading::EpochMillis>(0),
            SystemNowEpochMillis() - lastTradeTimestamp)
        : 0;

    if (lastTradeTimestamp > 0) {
        ImGui::Text(
            "부팅 %.0fms  렌더 %.1fHz  0B %llu건/%lldms",
            g_bootMilliseconds,
            g_renderRateHz,
            static_cast<unsigned long long>(realTimeTicks),
            static_cast<long long>(tradeAgeMs));
    }
    else {
        ImGui::Text(
            "부팅 %.0fms  렌더 %.1fHz  0B %s",
            g_bootMilliseconds,
            g_renderRateHz,
            g_stockTradeSubscriptionRequested.load(
                std::memory_order_acquire)
                ? "수신대기"
                : "미등록");
    }
'''
text = replace_once(text, old_toolbar, new_toolbar, "shell toolbar render/data rates")
anchor = '''        callbacks.minuteBars = [](
            const trading::MinuteBarsPage& page,
            const trading::Continuation& continuation) {
            ApplyMinuteBars(page, continuation);
            if (page.result.ok) {
                g_log.Add(
                    "DATA",
                    "실제 분봉 적용 완료: %s %d분 %zu봉",
                    page.code.c_str(),
                    page.minuteUnit,
                    page.bars.size());
            }
            else {
                const std::string error = !page.result.error.empty()
                    ? page.result.error
                    : page.result.returnMessage;
                g_log.Add("FAULT", "실제 분봉 오류: %s", error.c_str());
            }
        };

'''
addition = '''        callbacks.minuteBars = [](
            const trading::MinuteBarsPage& page,
            const trading::Continuation& continuation) {
            ApplyMinuteBars(page, continuation);
            if (page.result.ok) {
                g_log.Add(
                    "DATA",
                    "실제 분봉 적용 완료: %s %d분 %zu봉",
                    page.code.c_str(),
                    page.minuteUnit,
                    page.bars.size());

                std::string subscriptionError;
                if (
                    !g_runtimeRunner ||
                    !g_runtimeRunner->SubscribeStockTrades(
                        page.code,
                        subscriptionError))
                {
                    g_stockTradeSubscriptionRequested.store(
                        false,
                        std::memory_order_release);
                    g_log.Add(
                        "FAULT",
                        "0B 실시간 등록 실패: %s",
                        subscriptionError.c_str());
                }
                else {
                    g_stockTradeSubscriptionRequested.store(
                        true,
                        std::memory_order_release);
                    g_log.Add(
                        "WS",
                        "0B 실시간 등록 요청: %s",
                        page.code.c_str());
                }
            }
            else {
                const std::string error = !page.result.error.empty()
                    ? page.result.error
                    : page.result.returnMessage;
                g_log.Add("FAULT", "실제 분봉 오류: %s", error.c_str());
            }
        };
        callbacks.stockTrade = [](
            const trading::StockTradeTick& tick) {
            ApplyStockTradeTick(tick);
        };

'''
text = replace_once(text, anchor, addition, "shell runtime stock trade callback")
text = replace_once(
    text,
    '"키움 모의투자 연결 시작: 토큰 → WS → 00/04 → 계좌대조"',
    '"키움 모의투자 연결 시작: 토큰 → WS → 00/04 → 계좌대조 → 선택종목 0B"',
    "shell startup real-time flow",
)
text = replace_once(
    text,
    '''        const HRESULT present = g_swapChain->Present(1, 0);
        if (
            present == DXGI_ERROR_DEVICE_REMOVED ||
''',
    '''        const HRESULT present = g_swapChain->Present(1, 0);
        if (SUCCEEDED(present)) {
            RecordPresentedFrame();
        }
        if (
            present == DXGI_ERROR_DEVICE_REMOVED ||
''',
    "shell render rate observation",
)
text = replace_once(
    text,
    '''                60,
                QS_ALLINPUT,
''',
    '''                250,
                QS_ALLINPUT,
''',
    "shell idle wait",
)
write(path, text)


# ---------------------------------------------------------------------------
# tests/kiwoom_market_data_tests.cpp
# ---------------------------------------------------------------------------
path = "tests/kiwoom_market_data_tests.cpp"
text = read(path)
anchor = '''    void TestIndexAndFailures()
    {
'''
addition = '''    void TestStockTradeAggregation()
    {
        trading::RealTimeRecord record;
        record.type = "0B";
        record.item = "A000660";
        record.values["20"] = "123701";
        record.values["10"] = "+1584000";
        record.values["15"] = "-3";
        record.values["13"] = "552";

        const trading::StockTradeDecodeResult decoded =
            trading::DecodeStockTradeRecord(record);
        Check(decoded.result.ok, "valid 0B stock trade must decode");
        Check(decoded.tick.code == "000660", "0B code normalization mismatch");
        Check(decoded.tick.priceWon == 1584000, "0B price mismatch");
        Check(decoded.tick.tradeVolume == 3, "0B volume mismatch");
        Check(decoded.tick.tradeTimeHhmmss == 123701, "0B time mismatch");

        constexpr trading::EpochMillis SessionStart = 1785682800000LL;
        std::vector<trading::Bar> bars;
        trading::Bar current;
        current.open = 1582000;
        current.high = 1583000;
        current.low = 1581000;
        current.close = 1583000;
        current.volume = 549;
        current.closeTimestampMs = SessionStart + 12LL * 3600000LL + 37LL * 60000LL;
        bars.push_back(current);

        std::string error;
        Check(
            trading::MergeStockTradeIntoMinuteBars(
                bars, 1, SessionStart, decoded.tick, error),
            "0B trade must merge into current minute bar");
        Check(bars.size() == 1, "same-minute 0B trade must not append a bar");
        Check(bars.back().close == 1584000, "0B close update mismatch");
        Check(bars.back().high == 1584000, "0B high update mismatch");
        Check(bars.back().volume == 552, "0B volume accumulation mismatch");
        Check(bars.back().tickCount == 1, "0B tick count mismatch");

        trading::StockTradeTick next = decoded.tick;
        next.tradeTimeHhmmss = 123800;
        next.priceWon = 1585000;
        next.tradeVolume = 4;
        Check(
            trading::MergeStockTradeIntoMinuteBars(
                bars, 1, SessionStart, next, error),
            "next-minute 0B trade must append a bar");
        Check(bars.size() == 2, "next-minute 0B trade bar count mismatch");
        Check(bars.back().open == 1585000, "new 0B bar open mismatch");
        Check(bars.back().volume == 4, "new 0B bar volume mismatch");
    }

    void TestIndexAndFailures()
    {
'''
text = replace_once(text, anchor, addition, "market data 0B aggregation test")
text = replace_once(
    text,
    '''    TestStrictStockParsing();
    TestIndexAndFailures();
''',
    '''    TestStrictStockParsing();
    TestStockTradeAggregation();
    TestIndexAndFailures();
''',
    "market data test invocation",
)
write(path, text)


# ---------------------------------------------------------------------------
# tests/kiwoom_runtime_runner_tests.cpp
# ---------------------------------------------------------------------------
path = "tests/kiwoom_runtime_runner_tests.cpp"
text = read(path)
anchor = '''        bool SendWebSocketText(
            const std::string& text,
            std::string& error) override
        {
            if (!connected_.load(std::memory_order_acquire)) {
                error = "fake socket is disconnected";
                return false;
            }

            if (text.find("\\\"trnm\\\":\\\"LOGIN\\\"") != std::string::npos) {
'''
addition = '''        bool SendWebSocketText(
            const std::string& text,
            std::string& error) override
        {
            if (!connected_.load(std::memory_order_acquire)) {
                error = "fake socket is disconnected";
                return false;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                sentMessages_.push_back(text);
            }

            if (text.find("\\\"trnm\\\":\\\"LOGIN\\\"") != std::string::npos) {
'''
text = replace_once(text, anchor, addition, "runner fake sent message capture")
anchor = '''        int ConnectCount() const noexcept
        {
            return connectCount_.load(std::memory_order_relaxed);
        }

    private:
'''
addition = '''        int ConnectCount() const noexcept
        {
            return connectCount_.load(std::memory_order_relaxed);
        }

        int StockTradeRegistrationCount()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            int count = 0;
            for (const std::string& message : sentMessages_) {
                if (
                    message.find("\\\"type\\\":[\\\"0B\\\"]") !=
                        std::string::npos &&
                    message.find("000660") != std::string::npos)
                {
                    ++count;
                }
            }
            return count;
        }

        void PushStockTrade()
        {
            PushText(
                "{\\\"trnm\\\":\\\"REAL\\\",\\\"return_code\\\":0,\\\"data\\\":[{"
                "\\\"type\\\":\\\"0B\\\",\\\"item\\\":\\\"000660\\\","
                "\\\"name\\\":\\\"주식체결\\\",\\\"values\\\":{"
                "\\\"20\\\":\\\"123701\\\",\\\"10\\\":\\\"+1584000\\\","
                "\\\"15\\\":\\\"-3\\\",\\\"13\\\":\\\"552\\\"}}]}");
        }

    private:
'''
text = replace_once(text, anchor, addition, "runner fake stock trade controls")
anchor = '''        std::condition_variable condition_;
        std::deque<trading::platform::RuntimeReceiveResult> messages_;
'''
addition = '''        std::condition_variable condition_;
        std::deque<trading::platform::RuntimeReceiveResult> messages_;
        std::vector<std::string> sentMessages_;
'''
text = replace_once(text, anchor, addition, "runner fake sent message storage")
text = replace_once(
    text,
    '#include <thread>\n',
    '#include <thread>\n#include <vector>\n',
    "runner test vector include",
)
anchor = '''        std::atomic<int> logCount{ 0 };
        std::atomic<int> wakeCount{ 0 };
        std::atomic<bool> observe{ false };
'''
addition = '''        std::atomic<int> logCount{ 0 };
        std::atomic<int> wakeCount{ 0 };
        std::atomic<int> stockTradeCount{ 0 };
        std::atomic<bool> observe{ false };
'''
text = replace_once(text, anchor, addition, "runner test stock trade counter")
anchor = '''        callbacks.setObserveMode = [&](bool enabled) {
            observe.store(enabled, std::memory_order_release);
        };

'''
addition = '''        callbacks.setObserveMode = [&](bool enabled) {
            observe.store(enabled, std::memory_order_release);
        };
        callbacks.stockTrade = [&](const trading::StockTradeTick& tick) {
            if (tick.code == "000660" && tick.priceWon == 1584000) {
                stockTradeCount.fetch_add(1, std::memory_order_relaxed);
            }
        };

'''
text = replace_once(text, anchor, addition, "runner test stock trade callback")
anchor = '''        Check(state.SnapshotPositions().size() == 1,
              "runner reconciliation must install broker position");

        trading::OrderIntent buy;
'''
addition = '''        Check(state.SnapshotPositions().size() == 1,
              "runner reconciliation must install broker position");

        Check(runner.SubscribeStockTrades("000660", error),
              "ready runner must accept stock trade subscription");
        WaitUntil(
            [&] { return fake->StockTradeRegistrationCount() >= 1; },
            "runner did not send 0B stock trade registration");
        fake->PushStockTrade();
        WaitUntil(
            [&] { return stockTradeCount.load(std::memory_order_relaxed) == 1; },
            "runner did not decode and deliver 0B stock trade");

        trading::OrderIntent buy;
'''
text = replace_once(text, anchor, addition, "runner 0B subscription test")
anchor = '''        WaitUntil(
            [&] { return runner.Snapshot().orderSubmissionAllowed; },
            "runtime did not reconcile after reconnect");

        Check(!observe.load(std::memory_order_acquire),
'''
addition = '''        WaitUntil(
            [&] { return runner.Snapshot().orderSubmissionAllowed; },
            "runtime did not reconcile after reconnect");
        WaitUntil(
            [&] { return fake->StockTradeRegistrationCount() >= 2; },
            "runtime did not restore 0B subscription after reconnect");

        Check(!observe.load(std::memory_order_acquire),
'''
text = replace_once(text, anchor, addition, "runner 0B reconnect test")
write(path, text)


# ---------------------------------------------------------------------------
# .github/workflows/windows-ci.yml
# ---------------------------------------------------------------------------
path = ".github/workflows/windows-ci.yml"
text = read(path)
text = replace_once(
    text,
    '''            'DrawRealCandles'
''',
    '''            'DrawRealCandles',
            'ApplyStockTradeTick',
            'RecordPresentedFrame',
            '렌더 %.1fHz'
''',
    "CI shell real-time markers",
)
text = replace_once(
    text,
    '''            'stock minute bars require api-id ka10080'
''',
    '''            'stock minute bars require api-id ka10080',
            'DecodeStockTradeRecord',
            'MergeStockTradeIntoMinuteBars',
            'stock trade time FID 20'
''',
    "CI market 0B markers",
)
anchor = '''          if (-not $runner.Contains('DeliverMinuteBars')) {
            throw 'Minute-bar responses are not delivered to the UI callback'
          }

'''
addition = anchor + '''          if (-not $runner.Contains('SubscribeStockTrades') -or
              -not $runner.Contains('{ "0B" }')) {
            throw 'Selected stock 0B subscription is not integrated'
          }

'''
text = replace_once(text, anchor, addition, "CI 0B runner gate")
write(path, text)

print("Connected selected-stock 0B updates, minute aggregation, and measured render rate")
