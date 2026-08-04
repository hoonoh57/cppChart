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
#include <map>
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
#include "app/feature_registry.h"
#include "app/market_data_module.h"
#include "app/chart_workspace_module.h"
#include "app/indicator_module.h"
#include "app/indicator_render_adapter.h"
#include "app/default_indicator_render_plan.h"
#include "app/indicator_properties.h"
#include "app/indicator_workspace_coordinator.h"
#include "render/market_chart_builder.h"
#include "ui/render_document_renderer.h"

// CPPCHART_SHARED_RUNTIME_INTEGRATED
// CPPCHART_UI_THREAD_DATA_HANDOFF
// CPPCHART_KIWOOM_RUNTIME_CONNECTED
// CPPCHART_REAL_DATA_ONLY
// CPPCHART_MAJOR_FEATURE_MODULES
// CPPCHART_GENERIC_RENDER_DOCUMENT

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

static trading::app::FeatureRegistry g_featureRegistry;
static trading::app::MarketDataModule g_marketDataModule;
static trading::app::ChartWorkspaceModule g_chartWorkspaceModule;
static trading::app::IndicatorModule g_indicatorModule;
static trading::app::IndicatorRenderAdapter g_indicatorRenderAdapter;
static trading::ui::RenderSurfaceState g_mainRenderSurface;
static std::vector<trading::indicators::IndicatorSpec> g_indicatorSpecs;
static std::string g_selectedIndicatorId;
static std::string g_indicatorPropertyDraftId;
static std::map<std::string, double> g_indicatorPropertyDraft;
static bool g_indicatorPropertyDirty = false;
static bool g_focusIndicatorProperties = false;
static std::string g_indicatorPropertyError;

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
    return g_marketDataModule.TryGetLatestQuote(code, price);
}

static bool FeatureAtLeast(
    const std::string& id,
    trading::app::FeatureLevel minimum)
{
    trading::app::FeatureSnapshot snapshot;
    return
        g_featureRegistry.Get(id, snapshot) &&
        static_cast<int>(snapshot.level) >= static_cast<int>(minimum);
}

static bool SetFeatureLevel(
    const std::string& id,
    trading::app::FeatureLevel level,
    std::string& error)
{
    trading::app::FeatureSnapshot previousFeature;
    if (!g_featureRegistry.Get(id, previousFeature)) {
        error = "기능이 등록되어 있지 않습니다: " + id;
        return false;
    }

    const trading::app::MarketDataSnapshot previousMarket =
        g_marketDataModule.Snapshot();

    if (!g_featureRegistry.SetLevel(id, level, error)) return false;

    if (id == "market-data") {
        if (!g_marketDataModule.SetLevel(level, error)) {
            std::string rollbackError;
            g_featureRegistry.SetLevel(
                id,
                previousFeature.level,
                rollbackError);
            return false;
        }

        const bool enabled =
            level == trading::app::FeatureLevel::Visible ||
            level == trading::app::FeatureLevel::Active;

        if (!enabled) {
            if (g_runtimeRunner && !previousMarket.code.empty()) {
                std::string unsubscribeError;
                if (!g_runtimeRunner->UnsubscribeStockTrades(
                        previousMarket.code,
                        unsubscribeError))
                {
                    g_log.Add(
                        "FAULT",
                        "0B 실시간 해지 실패: %s",
                        unsubscribeError.c_str());
                    std::string healthError;
                    g_featureRegistry.SetHealth(
                        "market-data",
                        false,
                        unsubscribeError,
                        healthError);
                }
            }
            g_marketDataModule.SetStockTradeSubscriptionRequested(false);
        }
        else if (
            previousMarket.state == trading::app::MarketDataState::Ready &&
            !previousMarket.code.empty() &&
            g_runtimeRunner)
        {
            std::string subscribeError;
            if (!g_runtimeRunner->SubscribeStockTrades(
                    previousMarket.code,
                    subscribeError))
            {
                g_marketDataModule.SetStockTradeSubscriptionRequested(false);
                g_log.Add(
                    "FAULT",
                    "0B 실시간 재등록 실패: %s",
                    subscribeError.c_str());
                std::string healthError;
                g_featureRegistry.SetHealth(
                    "market-data",
                    false,
                    subscribeError,
                    healthError);
            }
            else {
                g_marketDataModule.SetStockTradeSubscriptionRequested(true);
                g_log.Add(
                    "WS",
                    "0B 실시간 재등록 요청: %s",
                    previousMarket.code.c_str());
            }
        }
    }
    else if (id == "indicators") {
        if (!g_indicatorModule.SetLevel(level, error)) {
            std::string rollbackError;
            g_featureRegistry.SetLevel(
                id,
                previousFeature.level,
                rollbackError);
            return false;
        }
        if (level == trading::app::FeatureLevel::Off) {
            g_indicatorRenderAdapter.ClearCache();
        }
    }
    else if (id == "chart-workspace") {
        if (!g_chartWorkspaceModule.SetLevel(level, error)) {
            std::string rollbackError;
            g_featureRegistry.SetLevel(
                id,
                previousFeature.level,
                rollbackError);
            return false;
        }
    }

    error.clear();
    return true;
}

static void RecordFeatureWork(
    const std::string& id,
    std::uint64_t elapsedMicros,
    std::size_t retainedBytes,
    std::size_t symbolCount,
    std::size_t renderSeriesCount,
    std::uint64_t mergedEvents = 0,
    std::uint64_t droppedEvents = 0)
{
    std::string ignored;
    g_featureRegistry.RecordWork(
        id,
        elapsedMicros,
        0,
        retainedBytes,
        symbolCount,
        renderSeriesCount,
        mergedEvents,
        droppedEvents,
        ignored);
}

static bool InitializeFeatureRegistry(std::string& error)
{
    if (!g_featureRegistry.Register(
            "market-data",
            "Market Data",
            trading::app::FeatureLevel::Visible,
            {},
            error)) return false;
    if (!g_featureRegistry.Register(
            "indicators",
            "Indicators",
            trading::app::FeatureLevel::Visible,
            { "market-data" },
            error)) return false;
    if (!g_featureRegistry.Register(
            "chart-workspace",
            "Chart Workspace",
            trading::app::FeatureLevel::Visible,
            { "market-data" },
            error)) return false;
    if (!g_featureRegistry.Register(
            "trading",
            "Trading / Account",
            trading::app::FeatureLevel::Active,
            {},
            error)) return false;
    if (!g_featureRegistry.Register(
            "diagnostics",
            "Diagnostics",
            trading::app::FeatureLevel::Visible,
            {},
            error)) return false;
    return true;
}

static bool InitializeIndicators(std::string& error)
{
    const std::vector<trading::indicators::IndicatorSpec> specs =
        trading::app::InitialIndicatorSpecs();
    if (!g_indicatorModule.Configure(specs, error)) return false;

    trading::app::IndicatorRenderPlan plan;
    if (!trading::app::BuildDefaultIndicatorRenderPlan(
            specs,
            plan,
            error))
    {
        return false;
    }
    if (!g_indicatorRenderAdapter.Configure(plan, error)) return false;
    if (!g_indicatorModule.SetLevel(
            trading::app::FeatureLevel::Visible,
            error))
    {
        return false;
    }

    g_indicatorSpecs = specs;
    g_indicatorPropertyDraftId.clear();
    g_indicatorPropertyDraft.clear();
    g_indicatorPropertyDirty = false;
    g_indicatorPropertyError.clear();
    error.clear();
    return true;
}

static const trading::indicators::IndicatorSpec*
FindIndicatorSpecById(const std::string& indicatorId)
{
    for (const trading::indicators::IndicatorSpec& spec :
         g_indicatorSpecs)
    {
        if (spec.id == indicatorId) return &spec;
    }
    return nullptr;
}

static void ResetIndicatorPropertyDraft(
    const trading::indicators::IndicatorSpec& spec)
{
    g_indicatorPropertyDraftId = spec.id;
    g_indicatorPropertyDraft = spec.parameters;
    g_indicatorPropertyDirty = false;
    g_indicatorPropertyError.clear();
}

static bool ApplyIndicatorConfiguration(
    const std::vector<trading::indicators::IndicatorSpec>& candidate,
    std::string& error)
{
    trading::app::IndicatorRenderPlan plan;
    if (!trading::app::BuildDefaultIndicatorRenderPlan(
            candidate,
            plan,
            error))
    {
        return false;
    }

    trading::app::IndicatorRenderAdapter validationAdapter;
    if (!validationAdapter.Configure(plan, error)) {
        return false;
    }
    if (!g_indicatorModule.Configure(candidate, error)) {
        return false;
    }
    if (!g_indicatorRenderAdapter.Configure(plan, error)) {
        return false;
    }

    g_indicatorSpecs = candidate;
    g_mainRenderSurface.dirty = true;
    std::string healthError;
    g_featureRegistry.SetHealth(
        "indicators",
        true,
        {},
        healthError);
    WakeFrames(6);
    error.clear();
    return true;
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
        FeatureAtLeast("trading", trading::app::FeatureLevel::Active) &&
        g_runtimeRunner &&
        g_runtimeRunner->Snapshot().orderSubmissionAllowed;
}

static bool CanActivateEntries()
{
    return
        CanSubmitBrokerOrders() &&
        FeatureAtLeast("market-data", trading::app::FeatureLevel::Visible) &&
        g_marketDataModule.Snapshot().state ==
            trading::app::MarketDataState::Ready;
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

    const trading::app::MarketDataSnapshot market =
        g_marketDataModule.Snapshot();
    ImGui::TextColored(
        market.state == trading::app::MarketDataState::Ready
            ? ImVec4(0.35f, 0.95f, 0.45f, 1.0f)
            : ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
        "| %s |",
        trading::app::MarketDataModule::StateName(market.state));
    ImGui::SameLine();
    const trading::EpochMillis tradeAgeMs =
        market.lastStockTradeTimestampMs > 0
        ? (std::max)(
            static_cast<trading::EpochMillis>(0),
            SystemNowEpochMillis() - market.lastStockTradeTimestampMs)
        : 0;

    if (market.lastStockTradeTimestampMs > 0) {
        ImGui::Text(
            "부팅 %.0fms  렌더 %.1fHz  0B %llu건/%lldms",
            g_bootMilliseconds,
            g_renderRateHz,
            static_cast<unsigned long long>(market.stockTradeTickCount),
            static_cast<long long>(tradeAgeMs));
    }
    else {
        ImGui::Text(
            "부팅 %.0fms  렌더 %.1fHz  0B %s",
            g_bootMilliseconds,
            g_renderRateHz,
            market.stockTradeSubscriptionRequested
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

static void DrawMarketDataPanel()
{
    ImGui::Begin("실제 시세");
    const trading::app::MarketDataSnapshot snapshot =
        g_marketDataModule.Snapshot();

    if (
        snapshot.state != trading::app::MarketDataState::Ready ||
        !snapshot.hasLatestBar)
    {
        ImGui::TextColored(
            snapshot.state == trading::app::MarketDataState::Loading
                ? ImVec4(0.95f, 0.72f, 0.25f, 1.0f)
                : ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
            "%s",
            trading::app::MarketDataModule::StateName(snapshot.state));
        if (!snapshot.error.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", snapshot.error.c_str());
        }
        ImGui::End();
        return;
    }

    const trading::Bar& latest = snapshot.latestBar;
    ImGui::Text(
        "%s | %d분 | 실제 ka10080 | %zu봉",
        snapshot.code.c_str(),
        snapshot.minuteUnit,
        snapshot.barCount);
    ImGui::SameLine();
    ImGui::Text(
        "O %d  H %d  L %d  C %d  V %lld  T %d",
        latest.open,
        latest.high,
        latest.low,
        latest.close,
        static_cast<long long>(latest.volume),
        latest.tickCount);

    if (
        snapshot.continuation.continueYn == "Y" ||
        snapshot.continuation.continueYn == "y")
    {
        ImGui::TextDisabled(
            "연속조회 가능: next-key가 수신되었습니다. 현재 화면은 검증된 첫 응답 페이지입니다.");
    }

    ImGui::Separator();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const trading::app::MarketDataSeriesSnapshot marketSeries =
        g_marketDataModule.SeriesSnapshot();

    trading::app::ChartMarketSource chartSource;
    chartSource.completedBars = marketSeries.completedBars;
    chartSource.liveBar = marketSeries.liveBar;
    chartSource.hasLiveBar = marketSeries.hasLiveBar;
    chartSource.barCount = marketSeries.barCount;
    chartSource.revision = marketSeries.revision;
    chartSource.completedRevision = marketSeries.completedRevision;
    chartSource.liveRevision = marketSeries.liveRevision;

    const double started = NowSeconds();
    bool useIndicators = false;
    trading::app::IndicatorModuleSnapshot indicatorSnapshot =
        g_indicatorModule.Snapshot();

    if (FeatureAtLeast(
            "indicators",
            trading::app::FeatureLevel::Visible))
    {
        const bool calculationNeeded =
            indicatorSnapshot.state !=
                trading::app::IndicatorModuleState::Ready ||
            indicatorSnapshot.sourceRevision != marketSeries.revision ||
            indicatorSnapshot.symbol != snapshot.code;

        if (calculationNeeded) {
            const std::uint64_t previousMergedEvents =
                indicatorSnapshot.metrics.mergedEventCount;
            const std::uint64_t previousDroppedEvents =
                indicatorSnapshot.metrics.droppedEventCount;
            trading::app::IndicatorMarketSource indicatorSource;
            indicatorSource.symbol = snapshot.code;
            indicatorSource.completedBars = marketSeries.completedBars;
            indicatorSource.liveBar = marketSeries.liveBar;
            indicatorSource.hasLiveBar = marketSeries.hasLiveBar;
            indicatorSource.revision = marketSeries.revision;
            indicatorSource.completedRevision =
                marketSeries.completedRevision;

            std::string indicatorError;
            if (!g_indicatorModule.Update(
                    indicatorSource,
                    indicatorError))
            {
                g_log.Add(
                    "FAULT",
                    "지표 계산 실패: %s",
                    indicatorError.c_str());
                std::string healthError;
                g_featureRegistry.SetHealth(
                    "indicators",
                    false,
                    indicatorError,
                    healthError);
            }
            else {
                indicatorSnapshot = g_indicatorModule.Snapshot();
                const trading::app::FeatureMetrics& metrics =
                    indicatorSnapshot.metrics;
                RecordFeatureWork(
                    "indicators",
                    metrics.lastProcessingMicros,
                    metrics.retainedBytes +
                        g_indicatorRenderAdapter.RetainedBytes(),
                    metrics.symbolCount,
                    metrics.renderSeriesCount,
                    metrics.mergedEventCount - previousMergedEvents,
                    metrics.droppedEventCount - previousDroppedEvents);
                std::string healthError;
                g_featureRegistry.SetHealth(
                    "indicators",
                    true,
                    {},
                    healthError);
                useIndicators = true;
            }
        }
        else {
            useIndicators = true;
        }
    }

    bool chartNeedsUpdate = false;
    if (useIndicators) {
        chartNeedsUpdate = g_chartWorkspaceModule.NeedsUpdate(
            marketSeries.revision,
            indicatorSnapshot,
            g_indicatorRenderAdapter);
    }
    else {
        chartNeedsUpdate =
            g_chartWorkspaceModule.NeedsUpdate(marketSeries.revision);
    }

    if (chartNeedsUpdate) {
        std::string chartError;
        const bool updated = useIndicators
            ? g_chartWorkspaceModule.UpdateMarketChart(
                "main-market-chart",
                snapshot.code,
                snapshot.code,
                chartSource,
                indicatorSnapshot,
                g_indicatorRenderAdapter,
                chartError)
            : g_chartWorkspaceModule.UpdateMarketChart(
                "main-market-chart",
                snapshot.code,
                snapshot.code,
                chartSource,
                chartError);

        if (!updated) {
            g_log.Add(
                "FAULT",
                "차트 워크스페이스 갱신 실패: %s",
                chartError.c_str());
            std::string healthError;
            g_featureRegistry.SetHealth(
                "chart-workspace",
                false,
                chartError,
                healthError);
            ImGui::End();
            return;
        }
        g_mainRenderSurface.dirty = true;
    }

    const trading::app::ChartWorkspaceSnapshot workspace =
        g_chartWorkspaceModule.Snapshot();
    if (workspace.document == nullptr) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
            "차트 렌더 문서가 없습니다.");
        ImGui::End();
        return;
    }

    trading::ui::DrawRenderDocument(
        *workspace.document,
        available,
        g_mainRenderSurface);
    if (g_mainRenderSurface.selectionChanged) {
        const trading::indicators::IndicatorSpec* selected =
            FindIndicatorSpecById(
                g_mainRenderSurface.selectedOwnerId);
        if (selected != nullptr) {
            g_selectedIndicatorId = selected->id;
            ResetIndicatorPropertyDraft(*selected);
            if (g_mainRenderSurface.selectionDoubleClicked) {
                g_focusIndicatorProperties = true;
            }
            WakeFrames(4);
        }
    }
    const std::uint64_t elapsedMicros = static_cast<std::uint64_t>(
        (NowSeconds() - started) * 1000000.0);
    RecordFeatureWork(
        "chart-workspace",
        elapsedMicros,
        workspace.retainedBytes,
        snapshot.code.empty() ? 0 : 1,
        workspace.seriesCount);
    std::string healthError;
    g_featureRegistry.SetHealth(
        "chart-workspace",
        workspace.state == trading::app::ChartWorkspaceState::Ready,
        workspace.error,
        healthError);
    ImGui::End();
}

static void DrawSymbolPool()
{
    ImGui::Begin("종목풀");
    const trading::app::MarketDataSnapshot snapshot =
        g_marketDataModule.Snapshot();
    if (snapshot.code.empty() || !snapshot.hasLatestBar) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
            "실제 시세 종목 없음");
    }
    else {
        ImGui::BulletText(
            "%s  %d분  %zu봉",
            snapshot.code.c_str(),
            snapshot.minuteUnit,
            snapshot.barCount);
        ImGui::TextDisabled("합성 종목과 임의 점수는 생성하지 않습니다.");
    }
    ImGui::End();
}

static void DrawScanner()
{
    ImGui::Begin("스캐너");
    const trading::app::MarketDataSnapshot snapshot =
        g_marketDataModule.Snapshot();
    if (!snapshot.hasLatestBar) {
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


static void DrawIndicatorPropertiesWindow()
{
    if (g_focusIndicatorProperties) {
        ImGui::SetNextWindowFocus();
    }

    ImGui::Begin("프로퍼티");
    g_focusIndicatorProperties = false;

    if (g_selectedIndicatorId.empty()) {
        ImGui::TextDisabled(
            "차트 패널 좌측 상단의 지표 범례를 클릭하면 선택됩니다.");
        ImGui::TextDisabled(
            "더블클릭하면 이 프로퍼티 탭이 즉시 활성화됩니다.");
        ImGui::End();
        return;
    }

    const trading::indicators::IndicatorSpec* spec =
        FindIndicatorSpecById(g_selectedIndicatorId);
    if (spec == nullptr) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
            "선택한 지표 구성을 찾을 수 없습니다.");
        ImGui::End();
        return;
    }

    trading::app::IndicatorPropertySnapshot properties;
    std::string descriptionError;
    if (!trading::app::DescribeIndicatorProperties(
            *spec,
            properties,
            descriptionError))
    {
        ImGui::TextColored(
            ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
            "%s",
            descriptionError.c_str());
        ImGui::End();
        return;
    }

    if (g_indicatorPropertyDraftId != spec->id) {
        ResetIndicatorPropertyDraft(*spec);
    }

    ImGui::Text(
        "%s",
        trading::app::IndicatorLegendLabel(*spec).c_str());
    ImGui::TextDisabled(
        "ID: %s  Type: %s",
        spec->id.c_str(),
        spec->type.c_str());
    ImGui::Separator();

    if (ImGui::BeginTable(
            "indicator_property_grid",
            2,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("속성");
        ImGui::TableSetupColumn("값");
        ImGui::TableHeadersRow();

        for (const trading::app::IndicatorParameterDescriptor& descriptor :
             properties.parameters)
        {
            ImGui::TableNextRow();
            ImGui::PushID(descriptor.key.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(descriptor.displayName.c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);

            double& draft =
                g_indicatorPropertyDraft[descriptor.key];
            bool changed = false;
            if (
                descriptor.kind ==
                trading::app::IndicatorParameterKind::Integer)
            {
                int value = static_cast<int>(std::llround(draft));
                const int step =
                    static_cast<int>(std::llround(descriptor.step));
                const int fastStep =
                    static_cast<int>(std::llround(descriptor.fastStep));
                if (ImGui::InputInt(
                        "##value",
                        &value,
                        step,
                        fastStep))
                {
                    draft = static_cast<double>(value);
                    changed = true;
                }
            }
            else {
                double value = draft;
                if (ImGui::InputDouble(
                        "##value",
                        &value,
                        descriptor.step,
                        descriptor.fastStep,
                        "%.4f"))
                {
                    draft = value;
                    changed = true;
                }
            }
            if (changed) {
                g_indicatorPropertyDirty = true;
                g_indicatorPropertyError.clear();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    const bool applyDisabled = !g_indicatorPropertyDirty;
    if (applyDisabled) ImGui::BeginDisabled();
    if (ImGui::Button("적용")) {
        const std::string selectedId = spec->id;
        std::vector<trading::indicators::IndicatorSpec> candidate =
            g_indicatorSpecs;
        std::string error;
        bool valid = true;
        for (const trading::app::IndicatorParameterDescriptor& descriptor :
             properties.parameters)
        {
            const auto found =
                g_indicatorPropertyDraft.find(descriptor.key);
            if (
                found == g_indicatorPropertyDraft.end() ||
                !trading::app::UpdateIndicatorParameter(
                    candidate,
                    selectedId,
                    descriptor.key,
                    found->second,
                    error))
            {
                valid = false;
                if (error.empty()) {
                    error =
                        "프로퍼티 초안 값이 없습니다: " +
                        descriptor.key;
                }
                break;
            }
        }

        if (valid && ApplyIndicatorConfiguration(candidate, error)) {
            const trading::indicators::IndicatorSpec* updated =
                FindIndicatorSpecById(selectedId);
            if (updated != nullptr) {
                ResetIndicatorPropertyDraft(*updated);
            }
            g_log.Add(
                "FEATURE",
                "지표 파라미터 적용: %s",
                selectedId.c_str());
        }
        else {
            g_indicatorPropertyError = error;
            g_log.Add(
                "REJECT",
                "지표 파라미터 적용 거부 %s: %s",
                selectedId.c_str(),
                error.c_str());
        }
    }
    if (applyDisabled) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("되돌리기")) {
        ResetIndicatorPropertyDraft(*spec);
    }

    if (!g_indicatorPropertyError.empty()) {
        ImGui::Separator();
        ImGui::TextColored(
            ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
            "%s",
            g_indicatorPropertyError.c_str());
    }

    ImGui::End();
}

static void DrawDashboard()
{
    ImGui::Begin("대시보드");

    const trading::app::MarketDataSnapshot market =
        g_marketDataModule.Snapshot();
    const bool quoteReady =
        market.state == trading::app::MarketDataState::Ready &&
        market.hasLatestBar;
    const trading::PriceWon latestPrice = quoteReady
        ? market.latestBar.close
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

static void DrawFeatureWindow()
{
    if (!FeatureAtLeast("diagnostics", trading::app::FeatureLevel::Visible)) {
        return;
    }

    ImGui::Begin("기능/성능");
    ImGui::TextDisabled(
        "주요 기능 단위만 실행 수준을 조절합니다. Off는 상류 작업까지 중지합니다.");

    const std::vector<trading::app::FeatureSnapshot> features =
        g_featureRegistry.SnapshotAll();
    if (ImGui::BeginTable(
            "feature_runtime",
            8,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp))
    {
        const char* headers[] = {
            "기능", "수준", "준비", "최근us", "최대us",
            "이벤트", "메모리", "오류" };
        for (const char* header : headers) {
            ImGui::TableSetupColumn(header);
        }
        ImGui::TableHeadersRow();

        const char* levels[] = { "Off", "Standby", "Visible", "Active" };
        for (const trading::app::FeatureSnapshot& feature : features) {
            ImGui::TableNextRow();
            ImGui::PushID(feature.id.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(feature.displayName.c_str());
            ImGui::TableNextColumn();
            int selectedLevel = static_cast<int>(feature.level);
            const bool pinnedDiagnostics = feature.id == "diagnostics";
            if (pinnedDiagnostics) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(90.0f);
            const bool levelChanged = ImGui::Combo(
                "##level",
                &selectedLevel,
                levels,
                IM_ARRAYSIZE(levels));
            if (pinnedDiagnostics) ImGui::EndDisabled();
            if (levelChanged)
            {
                std::string error;
                if (!SetFeatureLevel(
                        feature.id,
                        static_cast<trading::app::FeatureLevel>(selectedLevel),
                        error))
                {
                    g_log.Add(
                        "REJECT",
                        "기능 수준 변경 거부 %s: %s",
                        feature.id.c_str(),
                        error.c_str());
                }
                else {
                    g_log.Add(
                        "FEATURE",
                        "%s -> %s",
                        feature.id.c_str(),
                        trading::app::FeatureRegistry::LevelName(
                            static_cast<trading::app::FeatureLevel>(selectedLevel)));
                    WakeFrames(4);
                }
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(feature.ready ? "예" : "아니오");
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(
                feature.metrics.lastProcessingMicros));
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(
                feature.metrics.maxProcessingMicros));
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(
                feature.metrics.eventCount));
            ImGui::TableNextColumn();
            ImGui::Text("%zu", feature.metrics.retainedBytes);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", feature.lastError.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
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
                g_marketDataModule.SetError(
                    "종목코드가 비어 있어 실제 시세 조회를 시작할 수 없습니다.");
                g_log.Add("DATA", "실시세 조회 거부: 종목코드 없음");
                break;
            }
            if (!g_runtimeRunner || !g_runtimeRunner->IsRunning()) {
                g_marketDataModule.SetError("키움 런타임이 실행 중이 아닙니다.");
                g_log.Add("DATA", "실시세 조회 거부: 키움 런타임 정지");
                break;
            }

            const int minuteUnit = MinuteUnitFromSelection(command.i0);
            std::string error;
            if (!g_marketDataModule.BeginRequest(
                    command.arg,
                    minuteUnit,
                    error))
            {
                g_log.Add("DATA", "실시세 조회 거부: %s", error.c_str());
                break;
            }
            if (!g_runtimeRunner->RequestStockMinuteBars(
                    command.arg,
                    minuteUnit,
                    {},
                    error))
            {
                g_marketDataModule.SetError(error);
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
            const trading::app::MarketDataSnapshot snapshot =
                g_marketDataModule.Snapshot();
            if (snapshot.code.empty()) {
                g_marketDataModule.SetError("재조회할 실제 종목코드가 없습니다.");
                break;
            }
            std::string error;
            if (!g_marketDataModule.BeginRequest(
                    snapshot.code,
                    snapshot.minuteUnit,
                    error))
            {
                g_marketDataModule.SetError(error);
                break;
            }
            if (!g_runtimeRunner || !g_runtimeRunner->RequestStockMinuteBars(
                    snapshot.code, snapshot.minuteUnit, {}, error))
            {
                g_marketDataModule.SetError(error);
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
    ImGui::DockBuilderDockWindow("기능/성능", right);
    ImGui::DockBuilderDockWindow("프로퍼티", right);
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
    std::string featureError;
    if (!InitializeFeatureRegistry(featureError)) {
        g_runtimeConfigError = "기능 레지스트리 초기화 실패: " + featureError;
        g_observeMode.store(true, std::memory_order_release);
    }
    else {
        std::string indicatorError;
        if (!InitializeIndicators(indicatorError)) {
            std::string ignored;
            g_indicatorModule.SetLevel(
                trading::app::FeatureLevel::Off,
                ignored);
            g_indicatorRenderAdapter.Reset();
            g_featureRegistry.SetHealth(
                "indicators",
                false,
                indicatorError,
                ignored);
            g_featureRegistry.SetLevel(
                "indicators",
                trading::app::FeatureLevel::Off,
                ignored);
            g_log.Add(
                "FAULT",
                "지표 초기화 실패 — 시장 차트만 유지: %s",
                indicatorError.c_str());
        }
    }

    g_log.Add(
        "DATA",
        "%s",
        g_marketDataModule.Snapshot().error.c_str());

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
            const double started = NowSeconds();
            const trading::app::MarketDataApplyResult applied =
                g_marketDataModule.ApplyMinuteBars(page, continuation);
            const std::uint64_t elapsedMicros = static_cast<std::uint64_t>(
                (NowSeconds() - started) * 1000000.0);
            const trading::app::MarketDataSnapshot snapshot =
                g_marketDataModule.Snapshot();
            RecordFeatureWork(
                "market-data",
                elapsedMicros,
                snapshot.retainedBytes,
                snapshot.code.empty() ? 0 : 1,
                snapshot.hasLatestBar ? 2 : 0,
                0,
                applied.stale ? 1 : 0);

            std::string healthError;
            g_featureRegistry.SetHealth(
                "market-data",
                applied.applied,
                applied.error,
                healthError);

            if (applied.applied) {
                g_tradingState.UpdateCurrentPrice(
                    applied.code,
                    applied.latestPriceWon);
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
                    g_marketDataModule.SetStockTradeSubscriptionRequested(false);
                    g_log.Add(
                        "FAULT",
                        "0B 실시간 등록 실패: %s",
                        subscriptionError.c_str());
                }
                else {
                    g_marketDataModule.SetStockTradeSubscriptionRequested(true);
                    g_log.Add(
                        "WS",
                        "0B 실시간 등록 요청: %s",
                        page.code.c_str());
                }
            }
            else if (!applied.stale) {
                g_log.Add("FAULT", "실제 분봉 오류: %s", applied.error.c_str());
            }
            WakeFrames(4);
        };
        callbacks.stockTrade = [](
            const trading::StockTradeTick& tick) {
            const double started = NowSeconds();
            const trading::app::MarketDataApplyResult applied =
                g_marketDataModule.ApplyStockTradeTick(tick);
            const std::uint64_t elapsedMicros = static_cast<std::uint64_t>(
                (NowSeconds() - started) * 1000000.0);
            const trading::app::MarketDataSnapshot snapshot =
                g_marketDataModule.Snapshot();
            RecordFeatureWork(
                "market-data",
                elapsedMicros,
                snapshot.retainedBytes,
                snapshot.code.empty() ? 0 : 1,
                snapshot.hasLatestBar ? 2 : 0,
                0,
                applied.stale ? 1 : 0);

            if (applied.applied) {
                g_tradingState.UpdateCurrentPrice(
                    applied.code,
                    applied.latestPriceWon);
                WakeFrames(2);
            }
            else if (!applied.stale && !applied.error.empty()) {
                g_log.Add("FAULT", "0B 분봉 병합 실패: %s", applied.error.c_str());
            }
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
        if (FeatureAtLeast(
                "chart-workspace",
                trading::app::FeatureLevel::Visible))
        {
            DrawMarketDataPanel();
        }
        DrawScanner();
        DrawIndicatorPropertiesWindow();
        DrawDashboard();
        DrawLogWindow("로그", g_log);
        DrawLogWindow("신호", g_signalLog);
        DrawLogWindow("주문/체결", g_orderLog);
        DrawFaultWindow();
        DrawFeatureWindow();

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
