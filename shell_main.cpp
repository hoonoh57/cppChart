// ============================================================================
// Trading Shell — 실데이터 전용 런타임
// · 모든 조작은 CommandBus 경유
// · 모든 결함은 중앙 FaultPolicy 경유
// · 합성 시세, 합성 포지션, 합성 체결, 난수 피드를 생성하지 않는다
// · 실제 시세 연결 오류는 빈 차트와 명시적 오류로 노출한다
// ============================================================================
#include <windows.h>
#include <d3d11.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <memory>
#include <utility>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "core/command_bus.h"
#include "core/fault_policy.h"
#include "core/runtime_config.h"
#include "core/trading_state.h"
#include "core/order_coordinator.h"
#include "core/kiwoom_gateway_core.h"
#include "core/safe_liquidation.h"
#include "core/kiwoom_runtime_engine.h"
#include "platform/kiwoom_runtime_runner.h"
#include "platform/winhttp_kiwoom_transport.h"

// CPPCHART_SHARED_RUNTIME_INTEGRATED
// CPPCHART_UI_THREAD_DATA_HANDOFF
// CPPCHART_KIWOOM_RUNTIME_CONNECTED
// CPPCHART_REAL_DATA_ONLY

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static IDXGISwapChain* g_swapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static UINT g_resizeWidth = 0;
static UINT g_resizeHeight = 0;
static std::atomic<int> g_wakeFrames{4};

static void WakeFrames(int requested) noexcept
{
    int current = g_wakeFrames.load(std::memory_order_relaxed);
    while (
        current < requested &&
        !g_wakeFrames.compare_exchange_weak(
            current,
            requested,
            std::memory_order_release,
            std::memory_order_relaxed))
    {
    }
}

static bool ConsumeWakeFrame() noexcept
{
    int current = g_wakeFrames.load(std::memory_order_acquire);
    while (current > 0) {
        if (g_wakeFrames.compare_exchange_weak(
                current,
                current - 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            return true;
        }
    }
    return false;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND,
    UINT,
    WPARAM,
    LPARAM);

struct LogLine final
{
    char category[16];
    char message[320];
};

class LogRing final
{
public:
    LogRing()
    {
        buffer_.resize(4000);
    }

    void Add(const char* category, const char* format, ...)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        LogLine& line = buffer_[head_];
        std::snprintf(
            line.category,
            sizeof(line.category),
            "%s",
            category != nullptr ? category : "LOG");

        va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(
            line.message,
            sizeof(line.message),
            format,
            arguments);
        va_end(arguments);

        head_ = (head_ + 1) % buffer_.size();
        if (count_ < buffer_.size()) ++count_;
    }

    std::vector<LogLine> Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<LogLine> result;
        result.reserve(count_);
        const std::size_t start =
            (head_ + buffer_.size() - count_) % buffer_.size();
        for (std::size_t index = 0; index < count_; ++index) {
            result.push_back(
                buffer_[(start + index) % buffer_.size()]);
        }
        return result;
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = 0;
        count_ = 0;
    }

private:
    mutable std::mutex mutex_;
    std::vector<LogLine> buffer_;
    std::size_t head_ = 0;
    std::size_t count_ = 0;
};

static LogRing g_log;
static LogRing g_signalLog;
static LogRing g_orderLog;
static std::atomic<bool> g_observeMode{true};

static FaultPolicy g_faultPolicy(
    &g_observeMode,
    &g_wakeFrames,
    [](const char* category, const char* message) {
        g_log.Add(category, "%s", message);
    },
    4);

static CommandBus g_commandBus(&g_wakeFrames, 4);

static double NowSeconds()
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

static trading::EpochMillis SystemNowEpochMillis()
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

static trading::TradingState g_tradingState;
static trading::OrderCoordinator g_orderCoordinator(g_tradingState);
static trading::KiwoomGatewayCore g_gatewayCore(
    g_tradingState,
    g_orderCoordinator);
static trading::BrokerOpenOrderRegistry g_brokerOpenOrders;
static trading::KiwoomRuntimeEngine g_runtimeEngine(
    g_tradingState,
    g_orderCoordinator,
    g_gatewayCore,
    g_brokerOpenOrders);
static std::unique_ptr<trading::platform::KiwoomRuntimeRunner> g_runtimeRunner;
static trading::RuntimeConfig g_runtimeConfig;
static std::string g_runtimeConfigError;

static int g_orderQuantity = 1;
static char g_symbolInput[32] = "";
static int g_timeFrameIndex = 0;
static double g_bootMilliseconds = 0.0;

struct MarketDataView final
{
    std::string code;
    int minuteUnit = 1;
    std::vector<trading::Bar> bars;
    trading::Continuation continuation;
};

static std::mutex g_marketDataMutex;
static MarketDataView g_marketDataView;
static std::string g_marketDataError =
    "실제 시세 백필과 실시간 체결 수신이 연결되지 않았습니다. "
    "합성 데이터는 제거되었으며 오류를 숨기지 않습니다.";

enum class MarketDataState
{
    Disconnected,
    Loading,
    Ready,
    Error
};

static std::atomic<MarketDataState> g_marketDataState{
    MarketDataState::Error};

static std::atomic<std::uint64_t> g_stockTradeTickCount{0};
static std::atomic<trading::EpochMillis> g_lastStockTradeTimestampMs{0};
static std::atomic<bool> g_stockTradeSubscriptionRequested{false};

static void SetMarketDataError(const std::string& message)
{
    {
        std::lock_guard<std::mutex> lock(g_marketDataMutex);
        g_marketDataError = message;
    }
    g_marketDataState.store(
        MarketDataState::Error,
        std::memory_order_release);
    WakeFrames(4);
}

static std::string MarketDataErrorSnapshot()
{
    std::lock_guard<std::mutex> lock(g_marketDataMutex);
    return g_marketDataError;
}

static MarketDataView MarketDataSnapshot()
{
    std::lock_guard<std::mutex> lock(g_marketDataMutex);
    return g_marketDataView;
}

static void BeginMarketDataRequest(
    const std::string& code,
    int minuteUnit)
{
    {
        std::lock_guard<std::mutex> lock(g_marketDataMutex);
        g_marketDataView.code = code;
        g_marketDataView.minuteUnit = minuteUnit;
        g_marketDataView.bars.clear();
        g_marketDataView.continuation = {};
        g_marketDataError.clear();
    }
    g_stockTradeTickCount.store(0, std::memory_order_release);
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
    const trading::MinuteBarsPage& page,
    const trading::Continuation& continuation)
{
    if (!page.result.ok) {
        const std::string error = !page.result.error.empty()
            ? page.result.error
            : page.result.returnMessage;
        SetMarketDataError(error.empty()
            ? "실제 분봉 응답을 해석하지 못했습니다."
            : error);
        return;
    }

    if (page.bars.empty()) {
        SetMarketDataError("실제 분봉 응답이 비어 있습니다.");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_marketDataMutex);
        g_marketDataView.code = page.code;
        g_marketDataView.minuteUnit = page.minuteUnit;
        g_marketDataView.bars = page.bars;
        g_marketDataView.continuation = continuation;
        g_marketDataError.clear();
    }

    g_marketDataState.store(
        MarketDataState::Ready,
        std::memory_order_release);
    g_tradingState.UpdateCurrentPrice(
        page.code,
        page.bars.back().close);
    WakeFrames(4);
}

static int MinuteUnitFromSelection(int selection) noexcept
{
    static constexpr int units[] = { 1, 3, 5, 10, 15, 30, 60 };
    const int index = (std::max)(0, (std::min)(selection, 6));
    return units[index];
}

static bool TryGetLatestMarketQuote(
    std::string& code,
    trading::PriceWon& price)
{
    const MarketDataView snapshot = MarketDataSnapshot();
    if (snapshot.code.empty() || snapshot.bars.empty()) return false;
    code = snapshot.code;
    price = snapshot.bars.back().close;
    return trading::IsValidPrice(price);
}

static const char* MarketDataStateLabel(MarketDataState state) noexcept
{
    switch (state) {
    case MarketDataState::Loading:
        return "실시세 로딩";
    case MarketDataState::Ready:
        return "실시세 준비";
    case MarketDataState::Error:
        return "실시세 오류";
    default:
        return "실시세 미연결";
    }
}

static const char* KiwoomSessionStateLabel(
    trading::KiwoomSessionState state) noexcept
{
    switch (state) {
    case trading::KiwoomSessionState::TokenRequestPending:
        return "토큰";
    case trading::KiwoomSessionState::SocketConnectPending:
        return "WS 연결";
    case trading::KiwoomSessionState::LoginPending:
        return "로그인";
    case trading::KiwoomSessionState::RegistrationPending:
        return "실시간 등록";
    case trading::KiwoomSessionState::ReconciliationPending:
        return "계좌 대조";
    case trading::KiwoomSessionState::Ready:
        return "계좌 주문 가능";
    case trading::KiwoomSessionState::ReconnectWaiting:
        return "재연결 대기";
    case trading::KiwoomSessionState::Faulted:
        return "장애";
    case trading::KiwoomSessionState::ConfigurationError:
        return "설정 오류";
    default:
        return "정지";
    }
}

static trading::KiwoomRuntimeSnapshot RuntimeSnapshot()
{
    return g_runtimeRunner
        ? g_runtimeRunner->Snapshot()
        : trading::KiwoomRuntimeSnapshot{};
}

static bool CanSubmitBrokerOrders()
{
    return
        g_runtimeRunner &&
        g_runtimeRunner->Snapshot().orderSubmissionAllowed;
}

static bool CanActivateEntries()
{
    return
        CanSubmitBrokerOrders() &&
        g_marketDataState.load(std::memory_order_acquire) ==
            MarketDataState::Ready;
}

static bool CanSubmitEntryOrders()
{
    return
        CanActivateEntries() &&
        !g_observeMode.load(std::memory_order_acquire);
}

static bool CanSubmitLiquidationOrders()
{
    return CanSubmitBrokerOrders();
}

static bool FindPosition(
    const std::string& code,
    trading::PositionSnapshot& result)
{
    const std::vector<trading::PositionSnapshot> positions =
        g_tradingState.SnapshotPositions();
    for (const trading::PositionSnapshot& position : positions) {
        if (position.code == code) {
            result = position;
            return true;
        }
    }
    return false;
}

static void DrawToolbar()
{
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(6.0f, 4.0f));

    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputText(
        "##symbol",
        g_symbolInput,
        sizeof(g_symbolInput));
    ImGui::SameLine();

    const char* timeFrames[] = {
        "1분", "3분", "5분", "10분", "15분", "30분", "60분"};
    ImGui::SetNextItemWidth(90.0f);
    ImGui::Combo(
        "##timeframe",
        &g_timeFrameIndex,
        timeFrames,
        IM_ARRAYSIZE(timeFrames));
    ImGui::SameLine();

    if (ImGui::Button("실시세 조회")) {
        g_commandBus.Push(
            Cmd::LoadSymbol,
            g_symbolInput,
            g_timeFrameIndex);
    }

    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    ImGui::TextColored(
        ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
        "[KIWOOM MOCK]");
    ImGui::SameLine();

    const trading::KiwoomRuntimeSnapshot runtime = RuntimeSnapshot();
    const bool socketUp =
        runtime.sessionState ==
            trading::KiwoomSessionState::LoginPending ||
        runtime.sessionState ==
            trading::KiwoomSessionState::RegistrationPending ||
        runtime.sessionState ==
            trading::KiwoomSessionState::ReconciliationPending ||
        runtime.sessionState ==
            trading::KiwoomSessionState::Ready;

    ImGui::TextColored(
        socketUp
            ? ImVec4(0.30f, 0.90f, 0.40f, 1.0f)
            : ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
        socketUp ? "WS●" : "WS○");
    ImGui::SameLine();

    ImGui::TextColored(
        runtime.orderSubmissionAllowed
            ? ImVec4(0.35f, 0.95f, 0.45f, 1.0f)
            : ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
        "%s",
        KiwoomSessionStateLabel(runtime.sessionState));
    ImGui::SameLine();

    const MarketDataState marketState =
        g_marketDataState.load(std::memory_order_acquire);
    ImGui::TextColored(
        marketState == MarketDataState::Ready
            ? ImVec4(0.35f, 0.95f, 0.45f, 1.0f)
            : ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
        "| %s |",
        MarketDataStateLabel(marketState));
    ImGui::SameLine();
    const std::uint64_t realTimeTicks =
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

    if (g_observeMode.load(std::memory_order_acquire)) {
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(1.0f, 0.75f, 0.20f, 1.0f),
            "[관망 모드]");
    }

    const float buttonWidth = 130.0f;
    ImGui::SameLine(
        ImGui::GetWindowWidth() - buttonWidth - 16.0f);
    ImGui::PushStyleColor(
        ImGuiCol_Button,
        ImVec4(0.72f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImVec4(0.88f, 0.18f, 0.18f, 1.0f));

    const bool canLiquidate = CanSubmitLiquidationOrders();
    if (!canLiquidate) ImGui::BeginDisabled();
    const bool liquidateAll =
        ImGui::Button("전량청산", ImVec2(buttonWidth, 0.0f));
    if (!canLiquidate) ImGui::EndDisabled();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    if (liquidateAll) {
        ImGui::OpenPopup("confirm_liquidate_all");
    }

    if (ImGui::BeginPopupModal(
            "confirm_liquidate_all",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text(
            "키움 모의계좌의 보유 전 종목을 시장가로 청산합니다.");
        ImGui::Separator();
        if (ImGui::Button("청산 실행", ImVec2(120.0f, 0.0f))) {
            g_commandBus.Push(Cmd::LiquidateAll);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("취소", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void DrawRealCandles(
    const std::vector<trading::Bar>& bars,
    ImVec2 size)
{
    if (bars.empty() || size.x < 80.0f || size.y < 80.0f) return;

    const float volumeHeight = (std::max)(60.0f, size.y * 0.20f);
    const float priceHeight = size.y - volumeHeight - 8.0f;
    const int visibleCount = (std::max)(20, static_cast<int>(size.x / 7.0f));
    const int first = (std::max)(
        0,
        static_cast<int>(bars.size()) - visibleCount);

    trading::PriceWon minPrice = (std::numeric_limits<trading::PriceWon>::max)();
    trading::PriceWon maxPrice = 0;
    trading::Volume maxVolume = 1;
    for (int index = first; index < static_cast<int>(bars.size()); ++index) {
        minPrice = (std::min)(minPrice, bars[index].low);
        maxPrice = (std::max)(maxPrice, bars[index].high);
        maxVolume = (std::max)(maxVolume, bars[index].volume);
    }

    if (minPrice <= 0 || maxPrice <= minPrice) return;

    ImGui::InvisibleButton("##real_candles", size);
    const ImVec2 origin = ImGui::GetItemRectMin();
    const ImVec2 end = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, end, IM_COL32(15, 16, 20, 255));
    draw->AddRect(origin, end, IM_COL32(70, 72, 82, 255));

    for (int grid = 1; grid < 5; ++grid) {
        const float y = origin.y + priceHeight * grid / 5.0f;
        draw->AddLine(
            ImVec2(origin.x, y),
            ImVec2(end.x, y),
            IM_COL32(45, 47, 55, 255));
    }

    const float range = static_cast<float>(maxPrice - minPrice);
    const int count = static_cast<int>(bars.size()) - first;
    const float step = size.x / static_cast<float>((std::max)(1, count));
    const float bodyWidth = (std::max)(1.0f, step * 0.58f);

    const auto priceY = [&](trading::PriceWon price) {
        return origin.y +
            (static_cast<float>(maxPrice - price) / range) *
                priceHeight;
    };

    for (int local = 0; local < count; ++local) {
        const trading::Bar& bar = bars[first + local];
        const float x = origin.x + step * (local + 0.5f);
        const bool up = bar.close >= bar.open;
        const ImU32 color = up
            ? IM_COL32(235, 72, 72, 255)
            : IM_COL32(70, 130, 240, 255);

        draw->AddLine(
            ImVec2(x, priceY(bar.high)),
            ImVec2(x, priceY(bar.low)),
            color,
            1.0f);

        float openY = priceY(bar.open);
        float closeY = priceY(bar.close);
        if (std::fabs(openY - closeY) < 1.0f) closeY = openY + 1.0f;
        draw->AddRectFilled(
            ImVec2(x - bodyWidth * 0.5f, (std::min)(openY, closeY)),
            ImVec2(x + bodyWidth * 0.5f, (std::max)(openY, closeY)),
            color);

        const float volumeRatio = static_cast<float>(bar.volume) /
            static_cast<float>(maxVolume);
        const float volumeTop =
            origin.y + priceHeight + 8.0f +
            volumeHeight * (1.0f - volumeRatio);
        draw->AddRectFilled(
            ImVec2(x - bodyWidth * 0.5f, volumeTop),
            ImVec2(x + bodyWidth * 0.5f, end.y),
            color);
    }
}

static void DrawMarketDataPanel()
{
    ImGui::Begin("실제 시세");
    const MarketDataState state =
        g_marketDataState.load(std::memory_order_acquire);
    const MarketDataView snapshot = MarketDataSnapshot();

    if (state != MarketDataState::Ready || snapshot.bars.empty()) {
        ImGui::TextColored(
            state == MarketDataState::Loading
                ? ImVec4(0.95f, 0.72f, 0.25f, 1.0f)
                : ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
            "%s",
            MarketDataStateLabel(state));
        const std::string error = MarketDataErrorSnapshot();
        if (!error.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", error.c_str());
        }
        ImGui::End();
        return;
    }

    const trading::Bar& latest = snapshot.bars.back();
    ImGui::Text(
        "%s | %d분 | 실제 ka10080 | %zu봉",
        snapshot.code.c_str(),
        snapshot.minuteUnit,
        snapshot.bars.size());
    ImGui::SameLine();
    ImGui::Text(
        "O %d  H %d  L %d  C %d  V %lld",
        latest.open,
        latest.high,
        latest.low,
        latest.close,
        static_cast<long long>(latest.volume));

    if (
        snapshot.continuation.continueYn == "Y" ||
        snapshot.continuation.continueYn == "y")
    {
        ImGui::TextDisabled(
            "연속조회 가능: next-key가 수신되었습니다. 현재 화면은 검증된 첫 응답 페이지입니다.");
    }

    ImGui::Separator();
    DrawRealCandles(snapshot.bars, ImGui::GetContentRegionAvail());
    ImGui::End();
}

static void DrawSymbolPool()
{
    ImGui::Begin("종목풀");
    const MarketDataView snapshot = MarketDataSnapshot();
    if (snapshot.code.empty() || snapshot.bars.empty()) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
            "실제 시세 종목 없음");
    }
    else {
        ImGui::BulletText(
            "%s  %d분  %zu봉",
            snapshot.code.c_str(),
            snapshot.minuteUnit,
            snapshot.bars.size());
        ImGui::TextDisabled("합성 종목과 임의 점수는 생성하지 않습니다.");
    }
    ImGui::End();
}

static void DrawScanner()
{
    ImGui::Begin("스캐너");
    const MarketDataView snapshot = MarketDataSnapshot();
    if (snapshot.bars.empty()) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
            "실제 유니버스와 실제 시세가 없습니다.");
    }
    else {
        ImGui::Text("실제 분봉 수신: %s", snapshot.code.c_str());
        ImGui::TextWrapped(
            "현재 단계에서는 한 종목의 실제 분봉만 검증합니다. 실제 유니버스가 연결되기 전에는 베타·상관·시차·거래대금 순위를 만들지 않습니다.");
    }
    ImGui::End();
}

static void DrawDashboard()
{
    ImGui::Begin("대시보드");

    const MarketDataView market = MarketDataSnapshot();
    const bool quoteReady = !market.code.empty() && !market.bars.empty();
    const trading::PriceWon latestPrice = quoteReady
        ? market.bars.back().close
        : 0;

    if (quoteReady) {
        ImGui::Text(
            "선택: %s  실제현재가 %d원",
            market.code.c_str(),
            latestPrice);
    }
    else {
        ImGui::TextColored(
            ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
            "진입용 실제 현재가 없음");
    }

    ImGui::SameLine();
    ImGui::TextDisabled("| 주문수량");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::InputInt(
            "##order_quantity",
            &g_orderQuantity,
            1,
            10))
    {
        g_orderQuantity = (std::max)(1, g_orderQuantity);
    }

    ImGui::SameLine();
    const bool canEnter = CanSubmitEntryOrders() && quoteReady;
    if (!canEnter) ImGui::BeginDisabled();
    if (ImGui::Button("키움 모의매수")) {
        g_commandBus.Push(
            Cmd::MockBuy,
            market.code,
            g_orderQuantity);
    }
    if (!canEnter) ImGui::EndDisabled();

    ImGui::SameLine();
    const bool canLiquidate = CanSubmitLiquidationOrders();
    if (!canLiquidate) ImGui::BeginDisabled();
    if (ImGui::Button("선택 청산")) {
        g_commandBus.Push(Cmd::LiquidateSelected);
    }
    if (!canLiquidate) ImGui::EndDisabled();

    ImGui::SameLine();
    if (g_observeMode.load(std::memory_order_acquire)) {
        const bool canActivate = CanActivateEntries();
        if (!canActivate) ImGui::BeginDisabled();
        if (ImGui::Button("진입 허용")) {
            g_commandBus.Push(Cmd::ArmStrategy);
        }
        if (!canActivate) ImGui::EndDisabled();
    }
    else {
        if (ImGui::Button("관망 전환")) {
            g_commandBus.Push(Cmd::DisarmStrategy);
        }
    }

    const std::vector<trading::PositionSnapshot> positions =
        g_tradingState.SnapshotPositions();

    trading::MoneyWon totalCost = 0;
    trading::MoneyWon totalEvaluation = 0;
    for (const trading::PositionSnapshot& position : positions) {
        totalCost += position.costBasisWon;
        totalEvaluation += position.EvaluationWon();
    }

    const trading::MoneyWon unrealized = totalEvaluation - totalCost;
    const trading::MoneyWon realized = g_tradingState.RealizedPnlWon();
    const double rate = totalCost > 0
        ? static_cast<double>(unrealized) /
            static_cast<double>(totalCost) * 100.0
        : 0.0;

    ImGui::Separator();
    ImGui::Text(
        "키움 잔고 %zu종목   매입 %lld원   평가 %lld원",
        positions.size(),
        static_cast<long long>(totalCost),
        static_cast<long long>(totalEvaluation));
    ImGui::SameLine();
    ImGui::Text(
        "평가손익 %+.0f원 (%+.2f%%)   실현 %+.0f원",
        static_cast<double>(unrealized),
        rate,
        static_cast<double>(realized));

    if (positions.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("키움 계좌대조 결과 보유 포지션이 없습니다.");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable(
            "positions",
            8,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp))
    {
        const char* headers[] = {
            "선택", "종목", "수량", "평단", "현재가",
            "평가손익", "수익률", "청산"};
        for (const char* header : headers) {
            ImGui::TableSetupColumn(header);
        }
        ImGui::TableHeadersRow();

        for (const trading::PositionSnapshot& position : positions) {
            const trading::MoneyWon pnl = position.UnrealizedPnlWon();
            const double pnlRate = position.costBasisWon > 0
                ? static_cast<double>(pnl) /
                    static_cast<double>(position.costBasisWon) * 100.0
                : 0.0;

            ImGui::TableNextRow();
            ImGui::PushID(position.code.c_str());
            ImGui::TableNextColumn();
            bool selected = position.selected;
            if (ImGui::Checkbox("##selected", &selected)) {
                g_tradingState.SetSelected(position.code, selected);
            }
            ImGui::TableNextColumn();
            ImGui::Text("%s %s", position.code.c_str(), position.name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d", position.quantity);
            ImGui::TableNextColumn(); ImGui::Text("%.2f", position.AveragePriceWon());
            ImGui::TableNextColumn(); ImGui::Text("%d", position.currentPriceWon);
            ImGui::TableNextColumn(); ImGui::Text("%+.0f", static_cast<double>(pnl));
            ImGui::TableNextColumn(); ImGui::Text("%+.2f%%", pnlRate);
            ImGui::TableNextColumn();
            if (!canLiquidate) ImGui::BeginDisabled();
            if (ImGui::SmallButton("개별청산")) {
                g_commandBus.Push(Cmd::LiquidatePosition, position.code);
            }
            if (!canLiquidate) ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

static void DrawLogWindow(
    const char* title,
    LogRing& ring)
{
    ImGui::Begin(title);
    if (ImGui::Button("지우기")) ring.Clear();

    const std::vector<LogLine> lines = ring.Snapshot();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu 줄", lines.size());
    ImGui::Separator();
    ImGui::BeginChild(
        "body",
        ImVec2(0.0f, 0.0f),
        false,
        ImGuiWindowFlags_HorizontalScrollbar);

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(lines.size()));
    while (clipper.Step()) {
        for (
            int index = clipper.DisplayStart;
            index < clipper.DisplayEnd;
            ++index)
        {
            const LogLine& line =
                lines[static_cast<std::size_t>(index)];
            ImGui::TextDisabled("[%s]", line.category);
            ImGui::SameLine();
            ImGui::TextUnformatted(line.message);
        }
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

static void DrawFaultWindow()
{
    ImGui::Begin("결함");
    ImGui::TextDisabled(
        "실제 결함만 집계합니다. 인위적 결함 주입 기능은 제거했습니다.");

    if (ImGui::BeginTable(
            "faults",
            5,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg))
    {
        const char* headers[] = {
            "결함", "기본조치", "임계", "누적", "최근조치"};
        for (const char* header : headers) {
            ImGui::TableSetupColumn(header);
        }
        ImGui::TableHeadersRow();

        for (
            int index = 0;
            index < static_cast<int>(Fault::COUNT);
            ++index)
        {
            const Fault fault = static_cast<Fault>(index);
            const FaultRule& rule = g_faultPolicy.GetRule(fault);
            const FaultStat stat = g_faultPolicy.GetStat(fault);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(
                FaultPolicy::FaultName(fault));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(
                FaultPolicy::ActionName(rule.first));
            ImGui::TableNextColumn();
            ImGui::Text(
                "%d/%ds",
                rule.threshold,
                rule.windowSeconds);
            ImGui::TableNextColumn();
            ImGui::Text("%d", stat.total);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(
                stat.total > 0
                    ? FaultPolicy::ActionName(stat.last)
                    : "-");
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

static void DrainCommands()
{
    Command command;
    while (g_commandBus.Pop(command)) {
        switch (command.type) {
        case Cmd::LoadSymbol: {
            if (command.arg.empty()) {
                SetMarketDataError(
                    "종목코드가 비어 있어 실제 시세 조회를 시작할 수 없습니다.");
                g_log.Add("DATA", "실시세 조회 거부: 종목코드 없음");
                break;
            }
            if (!g_runtimeRunner || !g_runtimeRunner->IsRunning()) {
                SetMarketDataError("키움 런타임이 실행 중이 아닙니다.");
                g_log.Add("DATA", "실시세 조회 거부: 키움 런타임 정지");
                break;
            }

            const int minuteUnit = MinuteUnitFromSelection(command.i0);
            BeginMarketDataRequest(command.arg, minuteUnit);
            std::string error;
            if (!g_runtimeRunner->RequestStockMinuteBars(
                    command.arg,
                    minuteUnit,
                    {},
                    error))
            {
                SetMarketDataError(error);
                g_log.Add("FAULT", "ka10080 요청 실패: %s", error.c_str());
            }
            else {
                g_log.Add(
                    "DATA",
                    "ka10080 실제 분봉 요청: %s %d분",
                    command.arg.c_str(),
                    minuteUnit);
            }
            break;
        }

        case Cmd::MockBuy: {
            if (!CanSubmitEntryOrders()) {
                g_orderLog.Add(
                    "REJECT",
                    "매수 거부: 계좌대조·실제 시세·진입 허용 상태를 확인하세요.");
                break;
            }

            std::string quoteCode;
            trading::PriceWon quotePrice = 0;
            if (!TryGetLatestMarketQuote(quoteCode, quotePrice) ||
                quoteCode != command.arg)
            {
                g_orderLog.Add(
                    "REJECT",
                    "매수 거부: 선택 종목의 실제 현재가가 없습니다.");
                break;
            }

            trading::OrderIntent intent;
            intent.code = quoteCode;
            intent.name = quoteCode;
            intent.side = trading::StockOrderSide::Buy;
            intent.type = trading::StockOrderType::Market;
            intent.quantity = (std::max)(1, command.i0);

            std::string error;
            if (!g_runtimeRunner->SubmitOrder(intent, error)) {
                g_orderLog.Add("REJECT", "키움 모의매수 거부: %s", error.c_str());
            }
            else {
                g_orderLog.Add(
                    "ORDER",
                    "키움 모의매수 전송 %s %d주 시장가, 조회현재가=%d",
                    quoteCode.c_str(),
                    intent.quantity,
                    quotePrice);
            }
            break;
        }

        case Cmd::LiquidatePosition: {
            if (!CanSubmitLiquidationOrders()) {
                g_orderLog.Add(
                    "REJECT",
                    "개별청산 거부: 키움 계좌대조가 완료되지 않았습니다.");
                break;
            }

            trading::PositionSnapshot position;
            if (!FindPosition(command.arg, position)) {
                g_orderLog.Add(
                    "REJECT",
                    "개별청산 거부: 키움 보유 포지션 없음 %s",
                    command.arg.c_str());
                break;
            }

            trading::OrderIntent intent;
            intent.code = position.code;
            intent.name = position.name;
            intent.side = trading::StockOrderSide::Sell;
            intent.type = trading::StockOrderType::Market;
            intent.quantity = position.quantity;

            std::string error;
            if (
                !g_runtimeRunner ||
                !g_runtimeRunner->SubmitOrder(intent, error))
            {
                g_orderLog.Add(
                    "REJECT",
                    "개별청산 주문 거부: %s",
                    error.c_str());
            }
            else {
                g_orderLog.Add(
                    "ORDER",
                    "개별청산 주문 전송 %s %d주 시장가",
                    position.code.c_str(),
                    position.quantity);
            }
            break;
        }

        case Cmd::LiquidateSelected:
        case Cmd::LiquidateAll: {
            if (!CanSubmitLiquidationOrders()) {
                g_orderLog.Add(
                    "REJECT",
                    "청산 거부: 키움 계좌대조가 완료되지 않았습니다.");
                break;
            }

            const bool selectedOnly =
                command.type == Cmd::LiquidateSelected;
            std::string error;
            if (
                !g_runtimeRunner ||
                !g_runtimeRunner->SubmitLiquidation(
                    selectedOnly,
                    error))
            {
                g_orderLog.Add(
                    "REJECT",
                    "%s 주문 거부: %s",
                    selectedOnly ? "선택청산" : "전량청산",
                    error.c_str());
            }
            else {
                g_orderLog.Add(
                    "ORDER",
                    "%s 주문 전송",
                    selectedOnly ? "선택청산" : "전량청산");
            }
            break;
        }

        case Cmd::ArmStrategy:
            if (!CanActivateEntries()) {
                g_observeMode.store(true, std::memory_order_release);
                g_log.Add("REJECT", "진입 허용 거부: 계좌대조와 실제 시세가 필요합니다.");
            }
            else {
                g_observeMode.store(false, std::memory_order_release);
                g_log.Add("CMD", "실제 시세 기반 진입 허용");
            }
            break;

        case Cmd::DisarmStrategy:
            g_observeMode.store(true, std::memory_order_release);
            g_log.Add("CMD", "관망 전환");
            break;

        case Cmd::ResetFeed: {
            const MarketDataView snapshot = MarketDataSnapshot();
            if (snapshot.code.empty()) {
                SetMarketDataError("재조회할 실제 종목코드가 없습니다.");
                break;
            }
            BeginMarketDataRequest(snapshot.code, snapshot.minuteUnit);
            std::string error;
            if (!g_runtimeRunner || !g_runtimeRunner->RequestStockMinuteBars(
                    snapshot.code, snapshot.minuteUnit, {}, error))
            {
                SetMarketDataError(error);
            }
            else {
                g_log.Add("DATA", "ka10080 실제 분봉 재조회: %s", snapshot.code.c_str());
            }
            break;
        }

        default:
            g_log.Add("CMD", "미구현 커맨드");
            break;
        }
    }
}

static void BuildDefaultLayout(ImGuiID root)
{
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(
        root,
        ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(
        root,
        ImGui::GetMainViewport()->WorkSize);

    ImGuiID center = root;
    ImGuiID left = ImGui::DockBuilderSplitNode(
        center,
        ImGuiDir_Left,
        0.16f,
        nullptr,
        &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(
        center,
        ImGuiDir_Right,
        0.22f,
        nullptr,
        &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(
        center,
        ImGuiDir_Down,
        0.38f,
        nullptr,
        &center);
    ImGuiID bottomLogs = ImGui::DockBuilderSplitNode(
        bottom,
        ImGuiDir_Down,
        0.52f,
        nullptr,
        &bottom);

    ImGui::DockBuilderDockWindow("종목풀", left);
    ImGui::DockBuilderDockWindow("실제 시세", center);
    ImGui::DockBuilderDockWindow("스캐너", right);
    ImGui::DockBuilderDockWindow("대시보드", bottom);
    ImGui::DockBuilderDockWindow("로그", bottomLogs);
    ImGui::DockBuilderDockWindow("신호", bottomLogs);
    ImGui::DockBuilderDockWindow("주문/체결", bottomLogs);
    ImGui::DockBuilderDockWindow("결함", bottomLogs);
    ImGui::DockBuilderFinish(root);
}

static void CreateMainRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(
            g_swapChain->GetBuffer(
                0,
                IID_PPV_ARGS(&backBuffer))))
    {
        g_device->CreateRenderTargetView(
            backBuffer,
            nullptr,
            &g_mainRenderTargetView);
        backBuffer->Release();
    }
}

static bool CreateDeviceD3D(HWND window)
{
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2;
    description.BufferDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferDesc.RefreshRate.Numerator = 60;
    description.BufferDesc.RefreshRate.Denominator = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel{};
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0};

    const HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        levels,
        2,
        D3D11_SDK_VERSION,
        &description,
        &g_swapChain,
        &g_device,
        &featureLevel,
        &g_context);

    if (FAILED(result)) return false;
    CreateMainRenderTarget();
    return true;
}

static void CleanupDeviceD3D()
{
    if (g_mainRenderTargetView != nullptr) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
    if (g_swapChain != nullptr) {
        g_swapChain->Release();
        g_swapChain = nullptr;
    }
    if (g_context != nullptr) {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device != nullptr) {
        g_device->Release();
        g_device = nullptr;
    }
}

static LRESULT WINAPI WindowProcedure(
    HWND window,
    UINT message,
    WPARAM wordParameter,
    LPARAM longParameter)
{
    if (ImGui_ImplWin32_WndProcHandler(
            window,
            message,
            wordParameter,
            longParameter))
    {
        WakeFrames(4);
        return true;
    }

    switch (message) {
    case WM_SIZE:
        if (wordParameter != SIZE_MINIMIZED) {
            g_resizeWidth = LOWORD(longParameter);
            g_resizeHeight = HIWORD(longParameter);
            WakeFrames(4);
        }
        return 0;

    case WM_MOUSEMOVE:
    case WM_KEYDOWN:
    case WM_LBUTTONDOWN:
    case WM_MOUSEWHEEL:
        WakeFrames(4);
        break;

    case WM_SYSCOMMAND:
        if ((wordParameter & 0xfff0) == SC_KEYMENU) return 0;
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(
        window,
        message,
        wordParameter,
        longParameter);
}

static const ImWchar* GetKoreanRanges(ImGuiIO& io)
{
    static ImVector<ImWchar> ranges;
    if (ranges.Size == 0) {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        static const ImWchar korean[] = {
            0x3131, 0x318E,
            0xAC00, 0xD7A3,
            0x2010, 0x2027,
            0x3000, 0x303F,
            0xFF01, 0xFF60,
            0};
        builder.AddRanges(korean);
        builder.BuildRanges(&ranges);
    }
    return ranges.Data;
}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int)
{
    const double start = NowSeconds();
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW windowClass = {
        sizeof(windowClass),
        CS_CLASSDC,
        WindowProcedure,
        0,
        0,
        instance,
        nullptr,
        LoadCursorW(nullptr, IDC_ARROW),
        nullptr,
        nullptr,
        L"TradingShell",
        nullptr};

    RegisterClassExW(&windowClass);
    HWND window = CreateWindowW(
        windowClass.lpszClassName,
        L"Trading Shell — 실데이터 전용",
        WS_OVERLAPPEDWINDOW,
        60,
        40,
        1600,
        950,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!CreateDeviceD3D(window)) {
        CleanupDeviceD3D();
        UnregisterClassW(
            windowClass.lpszClassName,
            instance);
        return 1;
    }

    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |=
        ImGuiConfigFlags_DockingEnable |
        ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = "shell_layout.ini";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 2.0f;
    style.WindowPadding = ImVec2(6.0f, 6.0f);
    style.Colors[ImGuiCol_WindowBg] =
        ImVec4(0.10f, 0.10f, 0.12f, 1.0f);

    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(g_device, g_context);

    {
        ImFontConfig configuration;
        configuration.OversampleH = 2;
        configuration.OversampleV = 1;
        const char* candidates[] = {
            "C:\\Windows\\Fonts\\malgun.ttf",
            "C:\\Windows\\Fonts\\gulim.ttc"};
        bool loaded = false;
        for (const char* candidate : candidates) {
            if (GetFileAttributesA(candidate) ==
                INVALID_FILE_ATTRIBUTES)
            {
                continue;
            }
#if IMGUI_VERSION_NUM < 19200
            loaded = io.Fonts->AddFontFromFileTTF(
                candidate,
                16.0f,
                &configuration,
                GetKoreanRanges(io)) != nullptr;
#else
            loaded = io.Fonts->AddFontFromFileTTF(
                candidate,
                16.0f,
                &configuration) != nullptr;
#endif
            if (loaded) break;
        }
        if (!loaded) {
            g_log.Add(
                "SYS",
                "한글 폰트 로드 실패 — 기본 폰트 사용");
        }
    }

    ImGui_ImplDX11_CreateDeviceObjects();

    const trading::ConfigLoadResult configLoad =
        trading::LoadRuntimeConfig(".");
    if (configLoad.ok) {
        g_runtimeConfig = configLoad.config;
    }
    else {
        g_runtimeConfig = trading::RuntimeConfig{};
        g_runtimeConfigError = configLoad.error;
        g_observeMode.store(true, std::memory_order_release);
    }

    g_bootMilliseconds =
        (NowSeconds() - start) * 1000.0;
    g_log.Add(
        "SYS",
        "셸 기동 완료 (%.0fms): 실데이터 전용, 합성 시세/포지션/체결 없음",
        g_bootMilliseconds);
    g_log.Add(
        "DATA",
        "%s",
        MarketDataErrorSnapshot().c_str());

    if (!g_runtimeConfigError.empty()) {
        g_log.Add(
            "FAULT",
            "환경설정 오류로 주문·전략 잠금: %s",
            g_runtimeConfigError.c_str());
    }
    else {
        trading::platform::KiwoomRunnerCallbacks callbacks;
        callbacks.log = [](
            const char* category,
            const std::string& message) {
            if (
                std::strcmp(category, "ORDER") == 0 ||
                std::strcmp(category, "REJECT") == 0)
            {
                g_orderLog.Add(
                    category,
                    "%s",
                    message.c_str());
            }
            else {
                g_log.Add(
                    category,
                    "%s",
                    message.c_str());
            }
        };
        callbacks.wakeUi = [] {
            WakeFrames(4);
        };
        callbacks.setObserveMode = [](bool enabled) {
            g_observeMode.store(
                enabled,
                std::memory_order_release);
        };
        callbacks.minuteBars = [](
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

        g_runtimeRunner =
            std::make_unique<
                trading::platform::KiwoomRuntimeRunner>(
                g_runtimeEngine,
                std::make_unique<
                    trading::platform::WinHttpKiwoomTransport>(),
                std::move(callbacks));

        std::string runtimeError;
        if (!g_runtimeRunner->Start(
                g_runtimeConfig,
                runtimeError))
        {
            g_observeMode.store(true, std::memory_order_release);
            g_log.Add(
                "FAULT",
                "키움 모의투자 런타임 시작 실패: %s",
                runtimeError.c_str());
        }
        else {
            g_log.Add(
                "SYS",
                "키움 모의투자 연결 시작: 토큰 → WS → 00/04 → 계좌대조 → 선택종목 0B");
        }
    }

    bool resetLayout = true;
    bool running = true;

    while (running) {
        MSG message{};
        while (PeekMessageW(
            &message,
            nullptr,
            0,
            0,
            PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) running = false;
        }
        if (!running) break;

        if (g_resizeWidth != 0 && g_resizeHeight != 0) {
            if (g_mainRenderTargetView != nullptr) {
                g_mainRenderTargetView->Release();
                g_mainRenderTargetView = nullptr;
            }
            g_swapChain->ResizeBuffers(
                0,
                g_resizeWidth,
                g_resizeHeight,
                DXGI_FORMAT_UNKNOWN,
                0);
            g_resizeWidth = 0;
            g_resizeHeight = 0;
            CreateMainRenderTarget();
        }

        DrainCommands();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowRounding,
            0.0f);
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowBorderSize,
            0.0f);
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(4.0f, 4.0f));

        ImGui::Begin(
            "##host",
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus |
                ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar(3);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("파일")) {
                if (ImGui::MenuItem("레이아웃 초기화")) {
                    resetLayout = true;
                }
                if (ImGui::MenuItem("종료")) {
                    running = false;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("리셋")) {
                if (ImGui::MenuItem("실시세 피드")) {
                    g_commandBus.Push(Cmd::ResetFeed);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        DrawToolbar();
        ImGui::Separator();
        const ImGuiID dockSpace = ImGui::GetID("MainDock");
        if (resetLayout) {
            BuildDefaultLayout(dockSpace);
            resetLayout = false;
        }
        ImGui::DockSpace(
            dockSpace,
            ImVec2(0.0f, 0.0f),
            ImGuiDockNodeFlags_None);
        ImGui::End();

        DrawSymbolPool();
        DrawMarketDataPanel();
        DrawScanner();
        DrawDashboard();
        DrawLogWindow("로그", g_log);
        DrawLogWindow("신호", g_signalLog);
        DrawLogWindow("주문/체결", g_orderLog);
        DrawFaultWindow();

        ImGui::Render();
        const float clearColor[4] = {
            0.06f, 0.06f, 0.07f, 1.0f};
        g_context->OMSetRenderTargets(
            1,
            &g_mainRenderTargetView,
            nullptr);
        g_context->ClearRenderTargetView(
            g_mainRenderTargetView,
            clearColor);
        ImGui_ImplDX11_RenderDrawData(
            ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        const HRESULT present = g_swapChain->Present(1, 0);
        if (SUCCEEDED(present)) {
            RecordPresentedFrame();
        }
        if (
            present == DXGI_ERROR_DEVICE_REMOVED ||
            present == DXGI_ERROR_DEVICE_RESET)
        {
            g_faultPolicy.Raise(
                Fault::DeviceLost,
                "Present");
        }

        if (!ConsumeWakeFrame()) {
            MsgWaitForMultipleObjectsEx(
                0,
                nullptr,
                250,
                QS_ALLINPUT,
                MWMO_INPUTAVAILABLE);
        }
    }

    if (g_runtimeRunner) {
        g_runtimeRunner->Stop();
        g_runtimeRunner.reset();
    }

    ImGui_ImplDX11_InvalidateDeviceObjects();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(window);
    UnregisterClassW(
        windowClass.lpszClassName,
        instance);
    return 0;
}
