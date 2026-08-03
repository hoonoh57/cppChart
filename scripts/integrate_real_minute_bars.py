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


def replace_between(text: str, start: str, end: str, replacement: str, label: str) -> str:
    start_index = text.find(start)
    if start_index < 0:
        raise RuntimeError(f"{label}: start marker not found")
    end_index = text.find(end, start_index + len(start))
    if end_index < 0:
        raise RuntimeError(f"{label}: end marker not found")
    return text[:start_index] + replacement + text[end_index:]


engine = read("core/kiwoom_runtime_engine.cpp")

engine_methods = r'''    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::RequestStockMinuteBars(
        const std::string& stockCode,
        int minuteUnit,
        const Continuation& continuation,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return RequestMinuteBarsLocked(
            MinuteBarInstrument::Stock,
            stockCode,
            minuteUnit,
            continuation,
            error);
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::RequestIndexMinuteBars(
        const std::string& indexCode,
        int minuteUnit,
        const Continuation& continuation,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return RequestMinuteBarsLocked(
            MinuteBarInstrument::Index,
            indexCode,
            minuteUnit,
            continuation,
            error);
    }

'''
engine = replace_once(
    engine,
    "    std::vector<KiwoomRuntimeAction>\n    KiwoomRuntimeEngine::SubmitOrder(\n",
    engine_methods + "    std::vector<KiwoomRuntimeAction>\n    KiwoomRuntimeEngine::SubmitOrder(\n",
    "insert minute-bar engine methods",
)

engine_helper = r'''    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::RequestMinuteBarsLocked(
        MinuteBarInstrument instrument,
        const std::string& code,
        int minuteUnit,
        const Continuation& continuation,
        std::string& error)
    {
        if (accessToken_.empty()) {
            error = "access token is not available for market data";
            return {};
        }

        RestRequest request;
        if (instrument == MinuteBarInstrument::Stock) {
            request = BuildStockMinuteBarsRestRequest(
                code,
                minuteUnit,
                accessToken_,
                true,
                continuation,
                error);
        }
        else {
            request = BuildIndexMinuteBarsRestRequest(
                code,
                minuteUnit,
                accessToken_,
                continuation,
                error);
        }

        if (!error.empty()) return {};

        KiwoomRuntimeAction action = MakeRestActionLocked(
            instrument == MinuteBarInstrument::Stock
                ? KiwoomRuntimeActionType::RequestStockMinuteBars
                : KiwoomRuntimeActionType::RequestIndexMinuteBars,
            std::move(request));
        action.marketInstrument = instrument;
        action.marketCode = code;
        action.minuteUnit = minuteUnit;
        action.continuation = continuation;
        error.clear();
        return { std::move(action) };
    }

'''
engine = replace_once(
    engine,
    "    KiwoomRuntimeAction KiwoomRuntimeEngine::MakeRestActionLocked(\n",
    engine_helper + "    KiwoomRuntimeAction KiwoomRuntimeEngine::MakeRestActionLocked(\n",
    "insert minute-bar engine helper",
)
write("core/kiwoom_runtime_engine.cpp", engine)

runner = read("platform/kiwoom_runtime_runner.cpp")
runner = replace_once(
    runner,
    "            case KiwoomRuntimeActionType::RequestAccountBalance:\n                return \"balance\";\n            case KiwoomRuntimeActionType::SubmitOrderHttp:\n",
    "            case KiwoomRuntimeActionType::RequestAccountBalance:\n"
    "                return \"balance\";\n"
    "            case KiwoomRuntimeActionType::RequestStockMinuteBars:\n"
    "                return \"stock-minute-bars\";\n"
    "            case KiwoomRuntimeActionType::RequestIndexMinuteBars:\n"
    "                return \"index-minute-bars\";\n"
    "            case KiwoomRuntimeActionType::SubmitOrderHttp:\n",
    "runner action names",
)

runner_methods = r'''    bool KiwoomRuntimeRunner::RequestStockMinuteBars(
        const std::string& stockCode,
        int minuteUnit,
        const Continuation& continuation,
        std::string& error)
    {
        if (!running_.load(std::memory_order_acquire)) {
            error = "Kiwoom runtime is not running";
            return false;
        }

        std::vector<KiwoomRuntimeAction> actions =
            engine_.RequestStockMinuteBars(
                stockCode,
                minuteUnit,
                continuation,
                error);
        if (actions.empty()) return false;
        Enqueue(std::move(actions));
        return true;
    }

    bool KiwoomRuntimeRunner::RequestIndexMinuteBars(
        const std::string& indexCode,
        int minuteUnit,
        const Continuation& continuation,
        std::string& error)
    {
        if (!running_.load(std::memory_order_acquire)) {
            error = "Kiwoom runtime is not running";
            return false;
        }

        std::vector<KiwoomRuntimeAction> actions =
            engine_.RequestIndexMinuteBars(
                indexCode,
                minuteUnit,
                continuation,
                error);
        if (actions.empty()) return false;
        Enqueue(std::move(actions));
        return true;
    }

'''
runner = replace_once(
    runner,
    "    bool KiwoomRuntimeRunner::SubmitOrder(\n",
    runner_methods + "    bool KiwoomRuntimeRunner::SubmitOrder(\n",
    "insert runner market methods",
)

runner = replace_once(
    runner,
    "        case KiwoomRuntimeActionType::RequestOpenOrders:\n        case KiwoomRuntimeActionType::RequestExecutions:\n        case KiwoomRuntimeActionType::RequestAccountBalance:\n        case KiwoomRuntimeActionType::SubmitOrderHttp: {\n",
    "        case KiwoomRuntimeActionType::RequestOpenOrders:\n"
    "        case KiwoomRuntimeActionType::RequestExecutions:\n"
    "        case KiwoomRuntimeActionType::RequestAccountBalance:\n"
    "        case KiwoomRuntimeActionType::RequestStockMinuteBars:\n"
    "        case KiwoomRuntimeActionType::RequestIndexMinuteBars:\n"
    "        case KiwoomRuntimeActionType::SubmitOrderHttp: {\n",
    "runner REST action group",
)

runner = replace_once(
    runner,
    "            if (action.type == KiwoomRuntimeActionType::RequestOpenOrders) {\n",
    "            if (\n"
    "                action.type == KiwoomRuntimeActionType::RequestStockMinuteBars ||\n"
    "                action.type == KiwoomRuntimeActionType::RequestIndexMinuteBars)\n"
    "            {\n"
    "                DeliverMinuteBars(action, response, continuation);\n"
    "            }\n"
    "            else if (action.type == KiwoomRuntimeActionType::RequestOpenOrders) {\n",
    "runner minute-bar response branch",
)

runner_delivery = r'''    void KiwoomRuntimeRunner::DeliverMinuteBars(
        const KiwoomRuntimeAction& action,
        const RuntimeTransportResponse& response,
        const Continuation& continuation)
    {
        MinuteBarsPage page;
        page.instrument = action.marketInstrument;
        page.code = action.marketCode;
        page.minuteUnit = action.minuteUnit;

        if (!response.transportOk) {
            page.result.error = response.error.empty()
                ? "minute-bar transport failed"
                : "minute-bar transport failed: " + response.error;
        }
        else if (response.statusCode < 200 || response.statusCode >= 300) {
            std::ostringstream message;
            message << "minute-bar request failed: HTTP "
                    << response.statusCode;
            if (!response.body.empty()) {
                message << "; response=" << response.body;
            }
            page.result.error = message.str();
        }
        else if (action.marketInstrument == MinuteBarInstrument::Stock) {
            page = ParseStockMinuteBarsResponse(
                action.marketCode,
                action.minuteUnit,
                response.body);
        }
        else {
            page = ParseIndexMinuteBarsResponse(
                action.marketCode,
                action.minuteUnit,
                response.body);
        }

        if (callbacks_.minuteBars) {
            callbacks_.minuteBars(page, continuation);
        }

        if (page.result.ok) {
            std::ostringstream message;
            message
                << (action.marketInstrument == MinuteBarInstrument::Stock
                    ? "stock" : "index")
                << " minute bars received: code="
                << action.marketCode
                << " unit="
                << action.minuteUnit
                << " rows="
                << page.bars.size();
            if (!continuation.nextKey.empty()) {
                message << " continuation=" << continuation.continueYn;
            }
            Log("DATA", message.str());
        }
        else {
            const std::string error = !page.result.error.empty()
                ? page.result.error
                : page.result.returnMessage;
            Log("FAULT", error.empty()
                ? "minute-bar response rejected"
                : error);
        }
    }

'''
runner = replace_once(
    runner,
    "    void KiwoomRuntimeRunner::Log(\n",
    runner_delivery + "    void KiwoomRuntimeRunner::Log(\n",
    "insert minute-bar delivery",
)
write("platform/kiwoom_runtime_runner.cpp", runner)

shell = read("shell_main.cpp")
shell = replace_once(shell, "#include <algorithm>\n", "#include <algorithm>\n#include <cmath>\n#include <limits>\n", "shell numeric includes")

shell = replace_once(
    shell,
    "static std::mutex g_marketDataMutex;\nstatic std::string g_marketDataError =\n",
    "struct MarketDataView final\n"
    "{\n"
    "    std::string code;\n"
    "    int minuteUnit = 1;\n"
    "    std::vector<trading::Bar> bars;\n"
    "    trading::Continuation continuation;\n"
    "};\n\n"
    "static std::mutex g_marketDataMutex;\n"
    "static MarketDataView g_marketDataView;\n"
    "static std::string g_marketDataError =\n",
    "shell market view state",
)

shell_helpers = r'''static MarketDataView MarketDataSnapshot()
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
    g_marketDataState.store(
        MarketDataState::Loading,
        std::memory_order_release);
    WakeFrames(60);
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
    WakeFrames(60);
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

'''
shell = replace_once(
    shell,
    "static const char* MarketDataStateLabel(MarketDataState state) noexcept\n",
    shell_helpers + "static const char* MarketDataStateLabel(MarketDataState state) noexcept\n",
    "shell market helpers",
)

shell = replace_once(
    shell,
    "static bool CanSubmitEntryOrders()\n{\n    return\n        CanSubmitBrokerOrders() &&\n        g_marketDataState.load(std::memory_order_acquire) ==\n            MarketDataState::Ready &&\n        !g_observeMode.load(std::memory_order_acquire);\n}\n",
    "static bool CanActivateEntries()\n"
    "{\n"
    "    return\n"
    "        CanSubmitBrokerOrders() &&\n"
    "        g_marketDataState.load(std::memory_order_acquire) ==\n"
    "            MarketDataState::Ready;\n"
    "}\n\n"
    "static bool CanSubmitEntryOrders()\n"
    "{\n"
    "    return\n"
    "        CanActivateEntries() &&\n"
    "        !g_observeMode.load(std::memory_order_acquire);\n"
    "}\n",
    "split entry activation gate",
)

shell = replace_once(
    shell,
    "    const char* timeFrames[] = {\n        \"1분\", \"3분\", \"5분\", \"10분\", \"30분\", \"일\"};\n",
    "    const char* timeFrames[] = {\n"
    "        \"1분\", \"3분\", \"5분\", \"10분\", \"15분\", \"30분\", \"60분\"};\n",
    "minute-only toolbar",
)

chart_functions = r'''static void DrawRealCandles(
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
        "%s | %d분 | 실제 ka10079 | %zu봉",
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

'''
shell = replace_between(
    shell,
    "static void DrawMarketDataPanel()",
    "static void DrawDashboard()",
    chart_functions + "static void DrawDashboard()",
    "replace real market panels",
)

dashboard = r'''static void DrawDashboard()
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

'''
shell = replace_between(
    shell,
    "static void DrawDashboard()",
    "static void DrawLogWindow(",
    dashboard + "static void DrawLogWindow(",
    "replace dashboard",
)

load_case = r'''        case Cmd::LoadSymbol: {
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
                g_log.Add("FAULT", "ka10079 요청 실패: %s", error.c_str());
            }
            else {
                g_log.Add(
                    "DATA",
                    "ka10079 실제 분봉 요청: %s %d분",
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

'''
shell = replace_between(
    shell,
    "        case Cmd::LoadSymbol: {",
    "        case Cmd::LiquidatePosition: {",
    load_case + "        case Cmd::LiquidatePosition: {",
    "replace load and buy command cases",
)

shell = replace_once(
    shell,
    "        case Cmd::ArmStrategy:\n            g_observeMode.store(true, std::memory_order_release);\n            g_log.Add(\n                \"REJECT\",\n                \"전략 가동 거부: 실제 시세 준비 전입니다.\");\n            break;\n",
    "        case Cmd::ArmStrategy:\n"
    "            if (!CanActivateEntries()) {\n"
    "                g_observeMode.store(true, std::memory_order_release);\n"
    "                g_log.Add(\"REJECT\", \"진입 허용 거부: 계좌대조와 실제 시세가 필요합니다.\");\n"
    "            }\n"
    "            else {\n"
    "                g_observeMode.store(false, std::memory_order_release);\n"
    "                g_log.Add(\"CMD\", \"실제 시세 기반 진입 허용\");\n"
    "            }\n"
    "            break;\n",
    "activate entry command",
)

shell = replace_once(
    shell,
    "        case Cmd::ResetFeed:\n            SetMarketDataError(\n                \"실제 시세 피드 재연결 기능이 아직 연결되지 않았습니다.\");\n            g_log.Add(\"DATA\", \"실시세 피드 리셋 실패\");\n            break;\n",
    "        case Cmd::ResetFeed: {\n"
    "            const MarketDataView snapshot = MarketDataSnapshot();\n"
    "            if (snapshot.code.empty()) {\n"
    "                SetMarketDataError(\"재조회할 실제 종목코드가 없습니다.\");\n"
    "                break;\n"
    "            }\n"
    "            BeginMarketDataRequest(snapshot.code, snapshot.minuteUnit);\n"
    "            std::string error;\n"
    "            if (!g_runtimeRunner || !g_runtimeRunner->RequestStockMinuteBars(\n"
    "                    snapshot.code, snapshot.minuteUnit, {}, error))\n"
    "            {\n"
    "                SetMarketDataError(error);\n"
    "            }\n"
    "            else {\n"
    "                g_log.Add(\"DATA\", \"ka10079 실제 분봉 재조회: %s\", snapshot.code.c_str());\n"
    "            }\n"
    "            break;\n"
    "        }\n",
    "real feed reset",
)

shell = replace_once(
    shell,
    "        callbacks.setObserveMode = [](bool enabled) {\n            g_observeMode.store(\n                enabled,\n                std::memory_order_release);\n        };\n",
    "        callbacks.setObserveMode = [](bool enabled) {\n"
    "            g_observeMode.store(\n"
    "                enabled,\n"
    "                std::memory_order_release);\n"
    "        };\n"
    "        callbacks.minuteBars = [](\n"
    "            const trading::MinuteBarsPage& page,\n"
    "            const trading::Continuation& continuation) {\n"
    "            ApplyMinuteBars(page, continuation);\n"
    "            if (page.result.ok) {\n"
    "                g_log.Add(\n"
    "                    \"DATA\",\n"
    "                    \"실제 분봉 적용 완료: %s %d분 %zu봉\",\n"
    "                    page.code.c_str(),\n"
    "                    page.minuteUnit,\n"
    "                    page.bars.size());\n"
    "            }\n"
    "            else {\n"
    "                const std::string error = !page.result.error.empty()\n"
    "                    ? page.result.error\n"
    "                    : page.result.returnMessage;\n"
    "                g_log.Add(\"FAULT\", \"실제 분봉 오류: %s\", error.c_str());\n"
    "            }\n"
    "        };\n",
    "minute-bars UI callback",
)

for forbidden in ("MakeMockData", "MockFeedThread", "std::mt19937", "ApplyLocalFill"):
    if forbidden in shell:
        raise RuntimeError(f"synthetic marker reappeared: {forbidden}")

for required in ("ka10079", "DrawRealCandles", "RequestStockMinuteBars", "ApplyMinuteBars"):
    if required not in shell:
        raise RuntimeError(f"required real minute-bar marker missing: {required}")

write("shell_main.cpp", shell)

build = read("build.bat")
build = replace_once(
    build,
    "   core\\kiwoom_protocol.cpp ^\n",
    "   core\\kiwoom_protocol.cpp ^\n   core\\kiwoom_market_data.cpp ^\n",
    "build market data module",
)
write("build.bat", build)

run_all = read("tests/run_all.bat")
market_test_block = r'''
cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\kiwoom_market_data_tests.cpp ^
  core\json_lite.cpp ^
  core\kiwoom_market_data.cpp ^
  /Fe:kiwoom_market_data_tests.exe
if errorlevel 1 exit /b 1
kiwoom_market_data_tests.exe
if errorlevel 1 exit /b 1

'''
run_all = replace_once(
    run_all,
    "cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^\n  tests\\runtime_config_tests.cpp ^\n",
    market_test_block + "cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^\n  tests\\runtime_config_tests.cpp ^\n",
    "add market-data tests",
)
run_all = run_all.replace(
    "  core\\kiwoom_protocol.cpp ^\n  core\\runtime_config.cpp ^\n  core\\kiwoom_session.cpp ^\n  core\\trading_state.cpp ^\n  core\\order_coordinator.cpp ^\n  core\\kiwoom_events.cpp ^\n  core\\kiwoom_gateway_core.cpp ^\n  core\\kiwoom_reconciliation.cpp ^\n  core\\safe_liquidation.cpp ^\n  core\\kiwoom_runtime_engine.cpp ^\n",
    "  core\\kiwoom_protocol.cpp ^\n  core\\kiwoom_market_data.cpp ^\n  core\\runtime_config.cpp ^\n  core\\kiwoom_session.cpp ^\n  core\\trading_state.cpp ^\n  core\\order_coordinator.cpp ^\n  core\\kiwoom_events.cpp ^\n  core\\kiwoom_gateway_core.cpp ^\n  core\\kiwoom_reconciliation.cpp ^\n  core\\safe_liquidation.cpp ^\n  core\\kiwoom_runtime_engine.cpp ^\n",
)
write("tests/run_all.bat", run_all)

market_tests = r'''#include "../core/json_lite.h"
#include "../core/kiwoom_market_data.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::exit(1);
    }

    void Check(bool condition, const char* message)
    {
        if (!condition) Fail(message);
    }

    void TestStockRequest()
    {
        std::string error;
        const trading::RestRequest request =
            trading::BuildStockMinuteBarsRestRequest(
                "005930",
                3,
                "token",
                true,
                {},
                error);

        Check(error.empty(), "stock minute request must build");
        Check(request.path == "/api/dostk/chart", "chart path mismatch");
        Check(request.apiId == "ka10079", "stock minute api-id mismatch");
        Check(request.headers.at("authorization") == "Bearer token",
              "authorization header mismatch");
        Check(request.body.find("\"stk_cd\":\"005930\"") != std::string::npos,
              "stock code body mismatch");
        Check(request.body.find("\"tic_scope\":\"3\"") != std::string::npos,
              "minute unit body mismatch");
        Check(request.body.find("\"upd_stkpc_tp\":\"1\"") != std::string::npos,
              "adjusted price body mismatch");

        trading::Continuation continuation;
        continuation.continueYn = "Y";
        continuation.nextKey = "next-1";
        const trading::RestRequest continued =
            trading::BuildStockMinuteBarsRestRequest(
                "005930", 1, "token", true, continuation, error);
        Check(continued.headers.at("cont-yn") == "Y",
              "continuation header mismatch");
        Check(continued.headers.at("next-key") == "next-1",
              "next-key header mismatch");
    }

    void TestStrictStockParsing()
    {
        const std::string json =
            "{"
            "\"return_code\":0,"
            "\"return_msg\":\"정상\","
            "\"stk_min_pole_chart_qry\":["
            "{\"cur_prc\":\"-70100\",\"trde_qty\":\"1,200\","
            "\"cntr_tm\":\"20260803090300\",\"open_pric\":\"70000\","
            "\"high_pric\":\"70200\",\"low_pric\":\"69900\"},"
            "{\"cur_prc\":\"+70000\",\"trde_qty\":\"900\","
            "\"cntr_tm\":\"20260803090000\",\"open_pric\":\"69800\","
            "\"high_pric\":\"70100\",\"low_pric\":\"69700\"}"
            "]}"
            ;

        const trading::MinuteBarsPage page =
            trading::ParseStockMinuteBarsResponse("005930", 3, json);
        Check(page.result.ok, "valid stock minute response must parse");
        Check(page.bars.size() == 2, "stock bar count mismatch");
        Check(page.bars[0].close == 70000, "signed close must normalize");
        Check(page.bars[1].volume == 1200, "comma volume must parse");
        Check(page.bars[1].closeTimestampMs - page.bars[0].closeTimestampMs == 180000,
              "KST timestamp ordering mismatch");
    }

    void TestIndexAndFailures()
    {
        std::string error;
        const trading::RestRequest request =
            trading::BuildIndexMinuteBarsRestRequest(
                "001", 1, "token", {}, error);
        Check(error.empty(), "index request must build");
        Check(request.apiId == "ka20004", "index minute api-id mismatch");
        Check(request.body.find("\"inds_cd\":\"001\"") != std::string::npos,
              "index code body mismatch");

        const trading::MinuteBarsPage indexPage =
            trading::ParseIndexMinuteBarsResponse(
                "001",
                1,
                "{\"return_code\":0,\"inds_min_pole_qry\":["
                "{\"cur_prc\":\"320000\",\"trde_qty\":\"10\","
                "\"cntr_tm\":\"20260803090100\",\"open_pric\":\"319900\","
                "\"high_pric\":\"320100\",\"low_pric\":\"319800\"}]}"
            );
        Check(indexPage.result.ok, "valid index minute response must parse");

        const trading::MinuteBarsPage missing =
            trading::ParseStockMinuteBarsResponse(
                "005930", 1, "{\"return_code\":0}");
        Check(!missing.result.ok, "missing array must fail");
        Check(missing.result.error.find("stk_min_pole_chart_qry") != std::string::npos,
              "missing array error must name the field");

        const trading::MinuteBarsPage broken =
            trading::ParseStockMinuteBarsResponse(
                "005930",
                1,
                "{\"return_code\":0,\"stk_min_pole_chart_qry\":["
                "{\"cur_prc\":\"100\",\"trde_qty\":\"1\","
                "\"cntr_tm\":\"20260803090100\",\"open_pric\":\"100\","
                "\"high_pric\":\"90\",\"low_pric\":\"80\"}]}"
            );
        Check(!broken.result.ok, "OHLC invariant violation must fail");

        const trading::RestRequest invalid =
            trading::BuildStockMinuteBarsRestRequest(
                "005930", 2, "token", true, {}, error);
        Check(!error.empty(), "unsupported minute unit must fail");
        Check(invalid.path.empty(), "invalid request must remain empty");
    }
}

int main()
{
    TestStockRequest();
    TestStrictStockParsing();
    TestIndexAndFailures();
    std::puts("[PASS] kiwoom_market_data_tests");
    return 0;
}
'''
write("tests/kiwoom_market_data_tests.cpp", market_tests)

verify = read("scripts/verify_real_data_only.ps1")
verify = replace_once(
    verify,
    "    '.\\core\\kiwoom_runtime_engine.cpp'\n)",
    "    '.\\core\\kiwoom_runtime_engine.cpp',\n"
    "    '.\\core\\kiwoom_market_data.h',\n"
    "    '.\\core\\kiwoom_market_data.cpp'\n"
    ")",
    "verify market files",
)
verify = replace_once(
    verify,
    "    'CanSubmitLiquidationOrders'\n)",
    "    'CanSubmitLiquidationOrders',\n"
    "    'RequestStockMinuteBars',\n"
    "    'ka10079'\n"
    ")",
    "verify minute bar markers",
)
write("scripts/verify_real_data_only.ps1", verify)

workflow = read(".github/workflows/windows-ci.yml")
workflow = replace_once(
    workflow,
    "            'CanSubmitLiquidationOrders'\n",
    "            'CanSubmitLiquidationOrders',\n"
    "            'RequestStockMinuteBars',\n"
    "            'ka10079'\n",
    "CI minute-bar markers",
)
write(".github/workflows/windows-ci.yml", workflow)

print("Integrated strict real Kiwoom minute-bar request, parser, UI and tests")
