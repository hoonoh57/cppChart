[CmdletBinding()]
param(
    [string]$Path = (Join-Path $PSScriptRoot "..\shell_main.cpp")
)

$ErrorActionPreference = "Stop"
$Path = (Resolve-Path $Path).Path
$text = [IO.File]::ReadAllText($Path)

if ($text.Contains("CPPCHART_SHARED_RUNTIME_INTEGRATED")) {
    Write-Host "shell_main.cpp already uses the shared exact runtime"
    exit 0
}

function Replace-Exact {
    param(
        [string]$Source,
        [string]$Old,
        [string]$New,
        [string]$Label
    )

    if (-not $Source.Contains($Old)) {
        throw "Required source block was not found: $Label"
    }

    return $Source.Replace($Old, $New)
}

function Replace-RegexOne {
    param(
        [string]$Source,
        [string]$Pattern,
        [string]$Replacement,
        [string]$Label
    )

    $matches = [regex]::Matches(
        $Source,
        $Pattern,
        [Text.RegularExpressions.RegexOptions]::Singleline)

    if ($matches.Count -ne 1) {
        throw "Expected exactly one source block for ${Label}, found $($matches.Count)"
    }

    return [regex]::Replace(
        $Source,
        $Pattern,
        $Replacement,
        [Text.RegularExpressions.RegexOptions]::Singleline)
}

$text = Replace-Exact $text @'
#include <algorithm>
#include <cstdio>

#include "imgui.h"
'@ @'
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <cstdint>

#include "imgui.h"
'@ "standard includes"

$text = Replace-Exact $text @'
#include "core/command_bus.h"
#include "core/fault_policy.h"
'@ @'
#include "core/command_bus.h"
#include "core/fault_policy.h"
#include "core/market_types.h"
#include "core/runtime_config.h"
#include "core/trading_state.h"

// CPPCHART_SHARED_RUNTIME_INTEGRATED
'@ "shared runtime includes"

$text = Replace-Exact $text @'
static UINT g_resizeW = 0, g_resizeH = 0;
static int  g_wakeFrames = 60;          // 입력/데이터 변화 시 60프레임 활성
'@ @'
static UINT g_resizeW = 0, g_resizeH = 0;
static std::atomic<int> g_wakeFrames{ 60 }; // 입력/데이터 변화 시 활성 프레임

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
'@ "atomic UI wake counter"

$text = Replace-RegexOne $text 'struct LogRing \{.*?\n\};\nstatic LogRing g_log, g_signalLog, g_orderLog;' @'
struct LogRing {
    mutable std::mutex mtx;
    std::vector<LogLine> buf;
    size_t head = 0;
    size_t count = 0;

    LogRing() { buf.resize(4000); }

    void Add(const char* cat, const char* fmt, ...) {
        std::lock_guard<std::mutex> lock(mtx);
        LogLine& line = buf[head];
        snprintf(line.cat, sizeof(line.cat), "%s", cat);
        va_list arguments;
        va_start(arguments, fmt);
        vsnprintf(line.msg, sizeof(line.msg), fmt, arguments);
        va_end(arguments);
        head = (head + 1) % buf.size();
        if (count < buf.size()) ++count;
    }

    std::vector<LogLine> Snapshot() const {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<LogLine> result;
        result.reserve(count);
        const size_t start = (head + buf.size() - count) % buf.size();
        for (size_t index = 0; index < count; ++index) {
            result.push_back(buf[(start + index) % buf.size()]);
        }
        return result;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mtx);
        count = 0;
        head = 0;
    }
};
static LogRing g_log, g_signalLog, g_orderLog;
'@ "thread-safe log ring"

$text = Replace-RegexOne $text '// ─────────────────────────────── 목 데이터 ──────────────────────────────────.*?static std::mutex g_dataMtx;' @'
// ─────────────────────────────── 목 데이터 / 공용 매매 상태 ─────────────────
struct Bar {
    trading::PriceWon o = 0;
    trading::PriceWon h = 0;
    trading::PriceWon l = 0;
    trading::PriceWon c = 0;
    trading::Volume vol = 0;
    trading::EpochMillis tsClose = 0;
    trading::TickCount tickCount = 0;
};

struct Series {
    std::string code, name;
    std::vector<Bar> bars;
    float beta = 1.0f, corr = 0.5f, turnover = 10.f;
    int lag = 1;
    float score = 0;
};

static std::vector<Series> g_series;      // [0] = 지수
static trading::TradingState g_tradingState;
static trading::RuntimeConfig g_runtimeConfig;
static std::string g_runtimeConfigError;
static std::atomic<std::uint64_t> g_localOrderSequence{ 1 };
static int g_mockOrderQty = 1;
static std::mutex g_dataMtx;
'@ "exact market and position state"

$text = Replace-Exact $text @'
static Health g_health;

static void MakeMockData() {
'@ @'
static Health g_health;

static bool IsLocalMock() noexcept
{
    return g_runtimeConfig.mode == trading::RuntimeMode::LocalMock;
}

static const char* RuntimeModeLabel() noexcept
{
    return IsLocalMock() ? "LOCAL MOCK" : "KIWOOM MOCK";
}

static trading::EpochMillis UnixMillisNow() noexcept
{
    return static_cast<trading::EpochMillis>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

static std::string NextLocalOrderId(const char* prefix)
{
    const std::uint64_t sequence =
        g_localOrderSequence.fetch_add(1, std::memory_order_relaxed);
    return std::string(prefix) + "-" + std::to_string(sequence);
}

static bool TryGetLatestQuote(
    const std::string& code,
    std::string& name,
    trading::PriceWon& price)
{
    std::lock_guard<std::mutex> lock(g_dataMtx);
    for (const Series& series : g_series) {
        if (series.code == code && !series.bars.empty()) {
            name = series.name;
            price = series.bars.back().c;
            return trading::IsValidPrice(price);
        }
    }
    return false;
}

static trading::ApplyFillResult ApplyLocalFill(
    const std::string& code,
    const std::string& name,
    trading::OrderSide side,
    trading::Quantity quantity,
    trading::PriceWon price)
{
    const std::string orderId = NextLocalOrderId(
        side == trading::OrderSide::Buy ? "LOCAL-BUY" : "LOCAL-SELL");

    trading::CumulativeFill fill;
    fill.orderId = orderId;
    fill.executionId = orderId + "-EXEC";
    fill.code = code;
    fill.name = name;
    fill.side = side;
    fill.cumulativeQuantity = quantity;
    fill.fillPriceWon = price;
    fill.executionTimestampMs = UnixMillisNow();
    return g_tradingState.ApplyCumulativeFill(fill);
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

static void MakeMockData() {
'@ "shared runtime helpers"

$text = Replace-RegexOne $text 'static void MakeMockData\(\) \{.*?\n\}\n\n// ─────────────────────────────── 차트 렌더러' @'
static void MakeMockData() {
    static const char* names[][2] = {
        {"KOSPI","종합(KOSPI)"},{"097230","효성중공업"},{"090710","제주반도체"},
        {"403870","HPSP"},{"117730","삼양엔씨켐"},{"240810","원익IPS"},
        {"000660","SK하이닉스"},{"005930","삼성전자"},{"042700","한미반도체"},
        {"095340","ISC"},{"166090","하나머티리얼즈"},{"036930","주성엔지니어링"}
    };

    g_series.clear();
    g_tradingState.Reset();
    g_localOrderSequence.store(1, std::memory_order_relaxed);

    std::mt19937 rng(20260802);
    std::normal_distribution<double> normal(0.0, 1.0);
    const trading::EpochMillis firstClose = UnixMillisNow() - 2999LL * 60000LL;

    for (const auto& item : names) {
        Series series;
        series.code = item[0];
        series.name = item[1];
        trading::PriceWon price =
            static_cast<trading::PriceWon>(10000 + (rng() % 90000));
        series.bars.reserve(3000);

        for (int index = 0; index < 3000; ++index) {
            const double drift = std::sin(index * 0.004) * 0.0012;
            const trading::PriceWon open = price;
            const double next =
                static_cast<double>(price) *
                (1.0 + drift + normal(rng) * 0.0016);
            const trading::PriceWon close =
                (std::max)(1, static_cast<int>(std::llround(next)));
            const trading::PriceWon high =
                (std::max)(
                    open,
                    close) +
                static_cast<trading::PriceWon>(
                    std::llround(
                        (std::max)(open, close) *
                        std::fabs(normal(rng)) * 0.0008));
            const trading::PriceWon low =
                (std::max)(
                    1,
                    (std::min)(open, close) -
                    static_cast<trading::PriceWon>(
                        std::llround(
                            (std::min)(open, close) *
                            std::fabs(normal(rng)) * 0.0008)));

            Bar bar;
            bar.o = open;
            bar.h = high;
            bar.l = low;
            bar.c = close;
            bar.vol =
                static_cast<trading::Volume>(
                    1000 + std::llround(std::fabs(normal(rng)) * 4000.0));
            bar.tsClose = firstClose + static_cast<trading::EpochMillis>(index) * 60000LL;
            bar.tickCount = static_cast<trading::TickCount>(100 + rng() % 900);
            series.bars.push_back(bar);
            price = close;
        }

        series.beta = 0.7f + (rng() % 160) / 100.0f;
        series.corr = 0.35f + (rng() % 60) / 100.0f;
        series.lag = static_cast<int>(rng() % 4);
        series.turnover = 3.f + static_cast<float>(rng() % 400);
        series.score = series.beta * series.corr * 100.f;
        g_series.push_back(std::move(series));
    }

    if (!IsLocalMock()) return;

    struct SeedPosition {
        const char* code;
        trading::Quantity quantity;
        double basisFactor;
    };

    const SeedPosition seeds[] = {
        { "097230", 10, 0.985 },
        { "090710", 300, 1.012 },
        { "403870", 120, 0.997 }
    };

    for (const SeedPosition& seed : seeds) {
        std::string name;
        trading::PriceWon current = 0;
        if (!TryGetLatestQuote(seed.code, name, current)) continue;

        trading::PositionSnapshot position;
        position.code = seed.code;
        position.name = name;
        position.quantity = seed.quantity;
        position.currentPriceWon = current;
        const trading::PriceWon average =
            (std::max)(
                1,
                static_cast<int>(std::llround(
                    static_cast<double>(current) * seed.basisFactor)));
        position.costBasisWon =
            static_cast<trading::MoneyWon>(average) *
            static_cast<trading::MoneyWon>(position.quantity);

        std::string error;
        if (!g_tradingState.ReconcilePosition(position, error)) {
            g_log.Add("FAULT", "초기 포지션 구성 실패: %s", error.c_str());
        }
    }
}

// ─────────────────────────────── 차트 렌더러'@ "integer mock data"

$text = Replace-Exact $text @'
    float lo = 1e30f, hi = -1e30f, vmax = 1.f;
    for (int i = off; i < off + vis; ++i) {
        lo = (std::min)(lo, s.bars[i].l); hi = (std::max)(hi, s.bars[i].h);
        vmax = (std::max)(vmax, s.bars[i].vol);
    }
'@ @'
    float lo = 1e30f, hi = -1e30f, vmax = 1.f;
    for (int i = off; i < off + vis; ++i) {
        lo = (std::min)(lo, static_cast<float>(s.bars[i].l));
        hi = (std::max)(hi, static_cast<float>(s.bars[i].h));
        vmax = (std::max)(vmax, static_cast<float>(s.bars[i].vol));
    }
'@ "chart integer range conversion"

$text = Replace-Exact $text @'
    bool ws = g_health.wsUp.load();
    ImGui::TextColored(ws ? ImVec4(0.3f, 0.9f, 0.4f, 1) : ImVec4(0.95f, 0.3f, 0.3f, 1), ws ? "WS●" : "WS○");
'@ @'
    const bool localMock = IsLocalMock();
    ImGui::TextColored(
        localMock
            ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f)
            : ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
        "[%s]",
        RuntimeModeLabel());
    ImGui::SameLine();

    const bool ws = localMock ? true : g_health.wsUp.load();
    ImGui::TextColored(ws ? ImVec4(0.3f, 0.9f, 0.4f, 1) : ImVec4(0.95f, 0.3f, 0.3f, 1), ws ? "WS●" : "WS○");
'@ "runtime mode toolbar status"

$text = Replace-RegexOne $text 'static void DrawDashboard\(\)\n\{.*?\n\}\n\nstatic void DrawLogWindow' @'
static void DrawDashboard()
{
    ImGui::Begin("대시보드");

    std::lock_guard<std::mutex> dataLock(g_dataMtx);

    if (g_series.empty()) {
        ImGui::TextDisabled("종목 데이터가 없습니다.");
        ImGui::End();
        return;
    }

    const int selectedIndex =
        std::clamp(g_mainSel, 0, static_cast<int>(g_series.size()) - 1);
    const Series& selectedSeries = g_series[selectedIndex];

    ImGui::Text(
        "선택: %s %s",
        selectedSeries.code.c_str(),
        selectedSeries.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("| 주문수량");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);

    if (ImGui::InputInt("##mock_order_qty", &g_mockOrderQty, 1, 10)) {
        g_mockOrderQty = (std::max)(1, g_mockOrderQty);
    }

    ImGui::SameLine();
    if (IsLocalMock()) {
        if (ImGui::Button("모의매수")) {
            g_bus.Push(Cmd::MockBuy, selectedSeries.code, g_mockOrderQty);
        }
    }
    else {
        ImGui::BeginDisabled();
        ImGui::Button("키움매수 (연결 대기)");
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (IsLocalMock()) {
        if (ImGui::Button("선택 청산")) {
            g_bus.Push(Cmd::LiquidateSelected);
        }
    }
    else {
        ImGui::BeginDisabled();
        ImGui::Button("선택 청산 (연결 대기)");
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (g_observeMode.load()) {
        if (ImGui::Button("전략 가동")) g_bus.Push(Cmd::ArmStrategy);
    }
    else {
        if (ImGui::Button("관망 전환")) g_bus.Push(Cmd::DisarmStrategy);
    }

    const std::vector<trading::PositionSnapshot> positions =
        g_tradingState.SnapshotPositions();

    trading::MoneyWon totalBuy = 0;
    trading::MoneyWon totalEvaluation = 0;
    for (const trading::PositionSnapshot& position : positions) {
        totalBuy += position.costBasisWon;
        totalEvaluation += position.EvaluationWon();
    }

    const trading::MoneyWon unrealizedPnl =
        totalEvaluation - totalBuy;
    const double unrealizedRate =
        totalBuy > 0
            ? static_cast<double>(unrealizedPnl) /
                static_cast<double>(totalBuy) * 100.0
            : 0.0;
    const trading::MoneyWon realizedPnl =
        g_tradingState.RealizedPnlWon();
    const trading::MoneyWon totalPnl =
        unrealizedPnl + realizedPnl;

    ImGui::Separator();
    ImGui::Text(
        "보유 %zu종목   매입 %lld원   평가 %lld원",
        positions.size(),
        static_cast<long long>(totalBuy),
        static_cast<long long>(totalEvaluation));
    ImGui::SameLine();
    ImGui::TextColored(
        unrealizedPnl >= 0
            ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
            : ImVec4(0.35f, 0.60f, 1.00f, 1.0f),
        "평가손익 %+.0f원 (%+.2f%%)",
        static_cast<double>(unrealizedPnl),
        unrealizedRate);
    ImGui::SameLine();
    ImGui::TextColored(
        realizedPnl >= 0
            ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
            : ImVec4(0.35f, 0.60f, 1.00f, 1.0f),
        "실현 %+.0f원",
        static_cast<double>(realizedPnl));
    ImGui::SameLine();
    ImGui::TextColored(
        totalPnl >= 0
            ? ImVec4(0.95f, 0.70f, 0.25f, 1.0f)
            : ImVec4(0.40f, 0.65f, 1.00f, 1.0f),
        "총손익 %+.0f원",
        static_cast<double>(totalPnl));

    if (positions.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled(
            IsLocalMock()
                ? "보유 포지션이 없습니다. 종목을 선택한 뒤 모의매수를 실행하십시오."
                : "키움 잔고 대조가 완료되면 실제 모의계좌 포지션이 표시됩니다.");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable(
            "pos",
            8,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp))
    {
        const char* headers[] = {
            "선택", "종목", "수량", "평단", "현재가",
            "평가손익", "수익률", "청산"
        };
        for (const char* header : headers) ImGui::TableSetupColumn(header);
        ImGui::TableHeadersRow();

        for (const trading::PositionSnapshot& position : positions) {
            const trading::MoneyWon positionPnl = position.UnrealizedPnlWon();
            const double average = position.AveragePriceWon();
            const double positionRate =
                position.costBasisWon > 0
                    ? static_cast<double>(positionPnl) /
                        static_cast<double>(position.costBasisWon) * 100.0
                    : 0.0;

            ImGui::TableNextRow();
            ImGui::PushID(position.code.c_str());

            ImGui::TableNextColumn();
            bool selected = position.selected;
            if (ImGui::Checkbox("##position_selected", &selected)) {
                g_tradingState.SetSelected(position.code, selected);
            }

            ImGui::TableNextColumn();
            ImGui::Text("%s %s", position.code.c_str(), position.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", position.quantity);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", average);
            ImGui::TableNextColumn();
            ImGui::Text("%d", position.currentPriceWon);
            ImGui::TableNextColumn();
            ImGui::TextColored(
                positionPnl >= 0
                    ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
                    : ImVec4(0.35f, 0.60f, 1.00f, 1.0f),
                "%+.0f",
                static_cast<double>(positionPnl));
            ImGui::TableNextColumn();
            ImGui::TextColored(
                positionRate >= 0.0
                    ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
                    : ImVec4(0.35f, 0.60f, 1.00f, 1.0f),
                "%+.2f%%",
                positionRate);
            ImGui::TableNextColumn();

            if (IsLocalMock()) {
                if (ImGui::SmallButton("개별청산")) {
                    g_bus.Push(Cmd::LiquidatePosition, position.code);
                }
            }
            else {
                ImGui::BeginDisabled();
                ImGui::SmallButton("연결 대기");
                ImGui::EndDisabled();
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

static void DrawLogWindow'@ "exact dashboard state"

$text = Replace-RegexOne $text 'static void DrawLogWindow\(const char\* title, LogRing& ring\) \{.*?\n\}' @'
static void DrawLogWindow(const char* title, LogRing& ring) {
    ImGui::Begin(title);
    if (ImGui::Button("지우기")) ring.Clear();

    const std::vector<LogLine> lines = ring.Snapshot();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu 줄", lines.size());
    ImGui::Separator();
    ImGui::BeginChild(
        "body",
        ImVec2(0, 0),
        false,
        ImGuiWindowFlags_HorizontalScrollbar);

    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(lines.size()));
    while (clip.Step()) {
        for (int index = clip.DisplayStart; index < clip.DisplayEnd; ++index) {
            const LogLine& line = lines[static_cast<size_t>(index)];
            ImGui::TextDisabled("[%s]", line.cat);
            ImGui::SameLine();
            ImGui::TextUnformatted(line.msg);
        }
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) {
        ImGui::SetScrollHereY(1.f);
    }
    ImGui::EndChild();
    ImGui::End();
}'@ "log snapshot rendering"

$text = $text.Replace(
    '            const FaultStat& stat =`n                g_faultPolicy.GetStat(fault);',
    '            const FaultStat stat =`n                g_faultPolicy.GetStat(fault);')

$text = Replace-RegexOne $text '        case Cmd::MockBuy: \{.*?        case Cmd::ArmStrategy:' @'
        case Cmd::MockBuy: {
            if (!IsLocalMock()) {
                g_orderLog.Add("REJECT", "키움 모의투자 연결 완료 전에는 주문할 수 없습니다.");
                break;
            }

            const trading::Quantity orderQuantity =
                (std::max)(1, command.i0);
            std::string name;
            trading::PriceWon price = 0;

            if (!TryGetLatestQuote(command.arg, name, price)) {
                g_orderLog.Add(
                    "REJECT",
                    "모의매수 거부: 종목 데이터 없음 %s",
                    command.arg.c_str());
                break;
            }

            const trading::ApplyFillResult result = ApplyLocalFill(
                command.arg,
                name,
                trading::OrderSide::Buy,
                orderQuantity,
                price);

            trading::PositionSnapshot position;
            if (
                result.status != trading::ApplyFillStatus::Applied ||
                !FindPosition(command.arg, position))
            {
                g_orderLog.Add(
                    "REJECT",
                    "모의매수 실패: %s",
                    result.error.empty() ? "포지션 반영 실패" : result.error.c_str());
                break;
            }

            g_orderLog.Add(
                "FILL",
                "모의매수 %s %s %d주 @ %d | 보유 %d주 평단 %.2f",
                command.arg.c_str(),
                name.c_str(),
                orderQuantity,
                price,
                position.quantity,
                position.AveragePriceWon());
            g_log.Add(
                "TRADE",
                "모의매수 완료: %s %d주",
                command.arg.c_str(),
                orderQuantity);
            WakeFrames(60);
            break;
        }

        case Cmd::LiquidatePosition: {
            if (!IsLocalMock()) {
                g_orderLog.Add("REJECT", "키움 모의투자 연결 완료 전에는 청산할 수 없습니다.");
                break;
            }

            trading::PositionSnapshot position;
            if (!FindPosition(command.arg, position)) {
                g_orderLog.Add(
                    "REJECT",
                    "개별청산 거부: 보유 포지션 없음 %s",
                    command.arg.c_str());
                break;
            }

            const trading::ApplyFillResult result = ApplyLocalFill(
                position.code,
                position.name,
                trading::OrderSide::Sell,
                position.quantity,
                position.currentPriceWon);

            if (result.status != trading::ApplyFillStatus::Applied) {
                g_orderLog.Add(
                    "REJECT",
                    "개별청산 실패: %s",
                    result.error.c_str());
                break;
            }

            g_orderLog.Add(
                "FILL",
                "개별청산 %s %s %d주 @ %d | 실현손익 %+.0f원",
                position.code.c_str(),
                position.name.c_str(),
                position.quantity,
                position.currentPriceWon,
                static_cast<double>(result.realizedPnlWon));
            g_log.Add(
                "TRADE",
                "개별청산 완료: %s %d주",
                position.code.c_str(),
                position.quantity);
            WakeFrames(60);
            break;
        }

        case Cmd::LiquidateSelected:
        case Cmd::LiquidateAll: {
            if (!IsLocalMock()) {
                g_orderLog.Add("REJECT", "키움 모의투자 연결 완료 전에는 청산할 수 없습니다.");
                break;
            }

            const bool selectedOnly =
                command.type == Cmd::LiquidateSelected;
            const std::vector<trading::LiquidationOrder> plan =
                g_tradingState.BuildLiquidationPlan(selectedOnly);

            if (plan.empty()) {
                g_orderLog.Add(
                    "REJECT",
                    selectedOnly
                        ? "선택청산 거부: 선택된 포지션 없음"
                        : "전량청산 거부: 보유 포지션 없음");
                break;
            }

            size_t completed = 0;
            trading::MoneyWon totalRealized = 0;
            for (const trading::LiquidationOrder& order : plan) {
                trading::PositionSnapshot position;
                if (!FindPosition(order.code, position)) continue;

                const trading::ApplyFillResult result = ApplyLocalFill(
                    position.code,
                    position.name,
                    trading::OrderSide::Sell,
                    order.quantity,
                    position.currentPriceWon);
                if (result.status != trading::ApplyFillStatus::Applied) {
                    g_orderLog.Add(
                        "REJECT",
                        "청산 실패 %s: %s",
                        order.code.c_str(),
                        result.error.c_str());
                    continue;
                }

                ++completed;
                totalRealized += result.realizedPnlWon;
                g_orderLog.Add(
                    "FILL",
                    "%s %s %s %d주 @ %d | 실현손익 %+.0f원",
                    selectedOnly ? "선택청산" : "전량청산",
                    position.code.c_str(),
                    position.name.c_str(),
                    order.quantity,
                    position.currentPriceWon,
                    static_cast<double>(result.realizedPnlWon));
            }

            g_orderLog.Add(
                "ORDER",
                "%s 완료: %zu종목, 실현손익 %+.0f원",
                selectedOnly ? "선택청산" : "전량청산",
                completed,
                static_cast<double>(totalRealized));
            g_log.Add(
                "TRADE",
                "%s 완료: %zu종목",
                selectedOnly ? "선택청산" : "전량청산",
                completed);
            WakeFrames(60);
            break;
        }

        case Cmd::ArmStrategy:'@ "shared local execution path"

$text = Replace-RegexOne $text 'static std::atomic<bool> g_feedRun\{ true \};\nstatic void MockFeedThread\(\) \{.*?\n\}' @'
static std::atomic<bool> g_feedRun{ true };
static void MockFeedThread() {
    std::mt19937 rng(1234);
    std::normal_distribution<double> normal(0.0, 1.0);
    int tick = 0;

    while (g_feedRun.load()) {
        std::vector<std::pair<std::string, trading::PriceWon>> prices;
        {
            std::lock_guard<std::mutex> lock(g_dataMtx);
            prices.reserve(g_series.size());

            for (Series& series : g_series) {
                if (series.bars.empty()) continue;
                Bar& bar = series.bars.back();
                const double next =
                    static_cast<double>(bar.c) *
                    (1.0 + normal(rng) * 0.0006);
                bar.c = (std::max)(
                    1,
                    static_cast<int>(std::llround(next)));
                bar.h = (std::max)(bar.h, bar.c);
                bar.l = (std::min)(bar.l, bar.c);
                bar.vol += static_cast<trading::Volume>(
                    std::llround(std::fabs(normal(rng)) * 40.0));
                ++bar.tickCount;
                bar.tsClose = UnixMillisNow();
                prices.emplace_back(series.code, bar.c);
            }
        }

        if (IsLocalMock()) {
            for (const auto& entry : prices) {
                g_tradingState.UpdateCurrentPrice(entry.first, entry.second);
            }
        }

        g_mainCanvas.dirty = true;
        for (Canvas& canvas : g_multi) canvas.dirty = true;
        g_health.latencyMs = 8 + static_cast<int>(rng() % 20);
        g_health.rateUsed = static_cast<int>(rng() % 45);
        g_health.subCount = static_cast<int>(g_targets.size()) + 1;
        if (++tick % 12 == 0) {
            g_signalLog.Add(
                "SIG",
                "JMA(%d/%d) 교차 후보 감지 — 스텁",
                P_jmaFast,
                P_jmaMid);
        }
        WakeFrames(2);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
}'@ "integer mock feed"

$text = $text.Replace('g_wakeFrames = 60;', 'WakeFrames(60);')
$text = $text.Replace('g_wakeFrames = 30;', 'WakeFrames(30);')
$text = $text.Replace('g_wakeFrames = (std::max)(g_wakeFrames, 2);', 'WakeFrames(2);')

$text = Replace-Exact $text @'
    RegisterParams();
    MakeMockData();
    g_health.bootMs = (NowSec() - t0) * 1000.0;
    g_log.Add("SYS", "셸 기동 완료 (%.0fms). 데이터는 목(mock)이며 엔진은 스텁입니다.", g_health.bootMs.load());
'@ @'
    RegisterParams();

    const trading::ConfigLoadResult configLoad =
        trading::LoadRuntimeConfig(".");
    if (configLoad.ok) {
        g_runtimeConfig = configLoad.config;
    }
    else {
        g_runtimeConfig = trading::RuntimeConfig{};
        g_runtimeConfigError = configLoad.error;
        g_observeMode = true;
    }

    g_health.wsUp = IsLocalMock();
    MakeMockData();
    g_health.bootMs = (NowSec() - t0) * 1000.0;
    g_log.Add(
        "SYS",
        "셸 기동 완료 (%.0fms), 모드=%s, 정수 원화 공용 매매상태 사용",
        g_health.bootMs.load(),
        RuntimeModeLabel());

    if (!g_runtimeConfigError.empty()) {
        g_log.Add(
            "FAULT",
            "환경설정 오류로 LOCAL MOCK 관망 상태로 시작: %s",
            g_runtimeConfigError.c_str());
    }
    else if (!IsLocalMock()) {
        g_log.Add(
            "SYS",
            "KIWOOM MOCK 선택됨: 연결 런타임 준비 전까지 주문 잠금");
    }
'@ "runtime configuration startup"

$text = Replace-Exact $text @'
        if (g_wakeFrames > 0) --g_wakeFrames;
        else MsgWaitForMultipleObjectsEx(0, nullptr, 60, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
'@ @'
        if (!ConsumeWakeFrame()) {
            MsgWaitForMultipleObjectsEx(
                0,
                nullptr,
                60,
                QS_ALLINPUT,
                MWMO_INPUTAVAILABLE);
        }
'@ "atomic idle wake consumption"

$utf8Bom = New-Object System.Text.UTF8Encoding($true)
[IO.File]::WriteAllText($Path, $text, $utf8Bom)
Write-Host "Migrated shell_main.cpp to shared exact runtime"
