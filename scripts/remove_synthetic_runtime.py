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


def replace_between(
    text: str,
    start: str,
    end: str,
    replacement: str,
    label: str,
) -> str:
    start_index = text.find(start)
    if start_index < 0:
        raise RuntimeError(f"{label}: start marker not found")
    end_index = text.find(end, start_index + len(start))
    if end_index < 0:
        raise RuntimeError(f"{label}: end marker not found")
    return text[:start_index] + replacement + text[end_index:]


shell = read("shell_main.cpp")

shell = shell.replace(
    "Trading Shell — UI 골격 (엔진 스텁 / 목데이터)",
    "Trading Shell — 실데이터 전용 런타임",
    1,
)
shell = shell.replace("#include <random>\n", "", 1)
shell = replace_once(
    shell,
    "// CPPCHART_KIWOOM_RUNTIME_CONNECTED\n",
    "// CPPCHART_KIWOOM_RUNTIME_CONNECTED\n"
    "// CPPCHART_REAL_DATA_ONLY\n",
    "real-data-only marker",
)

shell = replace_once(
    shell,
    "static std::atomic<std::uint64_t> g_localOrderSequence{ 1 };\n"
    "static int g_mockOrderQty = 1;\n"
    "static std::mutex g_dataMtx;\n"
    "static std::mutex g_paramMtx;\n"
    "static std::atomic<bool> g_marketDataDirty{ true };\n",
    "enum class MarketDataState\n"
    "{\n"
    "    Disconnected,\n"
    "    Loading,\n"
    "    Ready,\n"
    "    Error\n"
    "};\n\n"
    "static int g_orderQty = 1;\n"
    "static std::mutex g_dataMtx;\n"
    "static std::mutex g_paramMtx;\n"
    "static std::mutex g_marketDataErrorMtx;\n"
    "static std::atomic<MarketDataState> g_marketDataState{ MarketDataState::Disconnected };\n"
    "static std::string g_marketDataError;\n"
    "static std::atomic<bool> g_marketDataDirty{ true };\n",
    "replace synthetic state globals",
)

shell = replace_once(
    shell,
    "    std::atomic<bool> wsUp{ true };",
    "    std::atomic<bool> wsUp{ false };",
    "health default",
)

shell = replace_between(
    shell,
    "static bool IsLocalMock() noexcept",
    "static const char* KiwoomSessionLabel(",
    "static const char* RuntimeModeLabel() noexcept\n"
    "{\n"
    "    return \"KIWOOM MOCK\";\n"
    "}\n\n"
    "static const char* MarketDataStateLabel(MarketDataState state) noexcept\n"
    "{\n"
    "    switch (state) {\n"
    "    case MarketDataState::Loading: return \"실시세 로딩\";\n"
    "    case MarketDataState::Ready: return \"실시세 준비\";\n"
    "    case MarketDataState::Error: return \"실시세 오류\";\n"
    "    default: return \"실시세 미연결\";\n"
    "    }\n"
    "}\n\n"
    "static void SetMarketDataError(const std::string& message)\n"
    "{\n"
    "    {\n"
    "        std::lock_guard<std::mutex> lock(g_marketDataErrorMtx);\n"
    "        g_marketDataError = message;\n"
    "    }\n"
    "    g_marketDataState.store(MarketDataState::Error, std::memory_order_release);\n"
    "    g_marketDataDirty.store(true, std::memory_order_release);\n"
    "    WakeFrames(60);\n"
    "}\n\n"
    "static std::string MarketDataErrorSnapshot()\n"
    "{\n"
    "    std::lock_guard<std::mutex> lock(g_marketDataErrorMtx);\n"
    "    return g_marketDataError;\n"
    "}\n\n"
    "static const char* KiwoomSessionLabel(",
    "replace local mock mode helpers",
)

shell = replace_once(
    shell,
    "static bool CanSubmitOrders()\n"
    "{\n"
    "    return\n"
    "        IsLocalMock() ||\n"
    "        (g_kiwoomRunner &&\n"
    "         g_kiwoomRunner->Snapshot().orderSubmissionAllowed &&\n"
    "         !g_observeMode.load(std::memory_order_acquire));\n"
    "}\n",
    "static bool CanSubmitBrokerOrders()\n"
    "{\n"
    "    return\n"
    "        g_kiwoomRunner &&\n"
    "        g_kiwoomRunner->Snapshot().orderSubmissionAllowed;\n"
    "}\n\n"
    "static bool CanSubmitEntryOrders()\n"
    "{\n"
    "    return\n"
    "        CanSubmitBrokerOrders() &&\n"
    "        g_marketDataState.load(std::memory_order_acquire) == MarketDataState::Ready &&\n"
    "        !g_observeMode.load(std::memory_order_acquire);\n"
    "}\n\n"
    "static bool CanSubmitLiquidationOrders()\n"
    "{\n"
    "    return CanSubmitBrokerOrders();\n"
    "}\n",
    "fail-closed order gates",
)

shell = replace_between(
    shell,
    "static trading::EpochMillis UnixMillisNow() noexcept",
    "static bool TryGetLatestQuote(",
    "static bool TryGetLatestQuote(",
    "remove local order identity helpers",
)

shell = replace_between(
    shell,
    "static trading::ApplyFillResult ApplyLocalFill(",
    "static bool FindPosition(",
    "static bool FindPosition(",
    "remove local fill engine",
)

shell = replace_between(
    shell,
    "static void MakeMockData()",
    "// ─────────────────────────────── 차트 렌더러",
    "static void InitializeRealDataState()\n"
    "{\n"
    "    {\n"
    "        std::lock_guard<std::mutex> lock(g_dataMtx);\n"
    "        g_series.clear();\n"
    "    }\n"
    "    g_tradingState.Reset();\n"
    "    SetMarketDataError(\n"
    "        \"실제 시세 백필과 실시간 체결 수신이 아직 연결되지 않았습니다. \"\n"
    "        \"합성 데이터는 제거되었으며 오류를 숨기지 않습니다.\");\n"
    "}\n\n"
    "// ─────────────────────────────── 차트 렌더러",
    "remove synthetic bars and seeded positions",
)

shell = replace_once(
    shell,
    "    if (indexOverlay && !g_series.empty() && &s != &g_series[0]) {\n"
    "        const Series& ix = g_series[0];\n"
    "        int m = (int)ix.bars.size();",
    "    if (indexOverlay && !g_series.empty() && !g_series[0].bars.empty() && &s != &g_series[0]) {\n"
    "        const Series& ix = g_series[0];\n"
    "        int m = (int)ix.bars.size();",
    "safe real index overlay",
)
shell = replace_once(
    shell,
    "        float base = ix.bars[std::clamp(off, 0, m - 1)].c;\n"
    "        float sbase = s.bars[off].c;",
    "        float base = static_cast<float>(ix.bars[std::clamp(off, 0, m - 1)].c);\n"
    "        float sbase = static_cast<float>(s.bars[off].c);\n"
    "        if (base == 0.0f) return;",
    "safe overlay base",
)

shell = replace_once(
    shell,
    "static Canvas g_mainCanvas;  static View g_mainView;  static int g_mainSel = 1;\n"
    "static Canvas g_multi[6];    static View g_multiView[6]; static int g_multiSel[6] = { 1,2,3,4,5,6 };\n"
    "static bool  g_showMulti = true;\n"
    "static char  g_symbolInput[32] = \"097230\";\n"
    "static int   g_tfIndex = 0;\n"
    "static std::vector<int> g_targets = { 1,2,3 };",
    "static Canvas g_mainCanvas;  static View g_mainView;  static int g_mainSel = 0;\n"
    "static Canvas g_multi[6];    static View g_multiView[6]; static int g_multiSel[6] = { 0,0,0,0,0,0 };\n"
    "static bool  g_showMulti = true;\n"
    "static char  g_symbolInput[32] = \"\";\n"
    "static int   g_tfIndex = 0;\n"
    "static std::vector<int> g_targets;",
    "remove synthetic selections",
)

shell = replace_between(
    shell,
    "static void DrawToolbar()",
    "static void DrawSymbolPool()",
    r'''static void DrawToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
    ImGui::SetNextItemWidth(110);
    ImGui::InputText("##sym", g_symbolInput, sizeof(g_symbolInput));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    const char* tfs[] = { "1분","3분","5분","10분","30분","일" };
    ImGui::Combo("##tf", &g_tfIndex, tfs, IM_ARRAYSIZE(tfs));
    ImGui::SameLine();
    if (ImGui::Button("실시세 조회")) g_bus.Push(Cmd::LoadSymbol, g_symbolInput, g_tfIndex);
    ImGui::SameLine();
    if (ImGui::Button("매매 멀티차트")) g_bus.Push(Cmd::OpenMultiChart);
    ImGui::SameLine(); ImGui::TextUnformatted("|"); ImGui::SameLine();

    ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f), "[%s]", RuntimeModeLabel());
    ImGui::SameLine();

    const trading::KiwoomRuntimeSnapshot runtime = KiwoomSnapshot();
    const bool ws =
        runtime.sessionState == trading::KiwoomSessionState::LoginPending ||
        runtime.sessionState == trading::KiwoomSessionState::RegistrationPending ||
        runtime.sessionState == trading::KiwoomSessionState::ReconciliationPending ||
        runtime.sessionState == trading::KiwoomSessionState::Ready;
    ImGui::TextColored(
        ws ? ImVec4(0.3f, 0.9f, 0.4f, 1) : ImVec4(0.95f, 0.3f, 0.3f, 1),
        ws ? "WS●" : "WS○");
    ImGui::SameLine();
    ImGui::TextColored(
        runtime.orderSubmissionAllowed
            ? ImVec4(0.35f, 0.95f, 0.45f, 1.0f)
            : ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
        "%s",
        KiwoomSessionLabel(runtime.sessionState));
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
    ImGui::Text("구독 %d   부팅 %.0fms   %.1ffps",
        g_health.subCount.load(), g_health.bootMs.load(), ImGui::GetIO().Framerate);

    if (g_observeMode.load()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.75f, 0.2f, 1), "[관망 모드]");
    }

    float btnW = 130.f;
    ImGui::SameLine(ImGui::GetWindowWidth() - btnW - 16.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.12f, 0.12f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.18f, 0.18f, 1));
    const bool canLiquidate = CanSubmitLiquidationOrders();
    if (!canLiquidate) ImGui::BeginDisabled();
    bool panic = ImGui::Button("전량청산", ImVec2(btnW, 0));
    if (!canLiquidate) ImGui::EndDisabled();
    ImGui::PopStyleColor(2);
    if (panic || (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_L, false)))
        ImGui::OpenPopup("confirm_liq_all");
    ImGui::PopStyleVar();

    if (ImGui::BeginPopupModal("confirm_liq_all", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("키움 모의계좌 보유 전 종목을 시장가로 청산합니다. 진행할까요?");
        ImGui::Separator();
        if (ImGui::Button("청산 실행", ImVec2(120, 0))) { g_bus.Push(Cmd::LiquidateAll); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("취소", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void DrawSymbolPool()''',
    "real-data toolbar",
)

shell = replace_between(
    shell,
    "static void DrawSymbolPool()",
    "static void DrawMainChart()",
    r'''static void DrawSymbolPool() {
    ImGui::Begin("종목풀");
    if (g_series.empty()) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
            "실제 시세 종목이 없습니다.");
        const std::string error = MarketDataErrorSnapshot();
        if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
    }
    else if (ImGui::TreeNodeEx("실시세 종목", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < static_cast<int>(g_series.size()); ++i) {
            const bool selected = i == g_mainSel;
            char label[96];
            snprintf(label, sizeof(label), "%s  %s", g_series[i].code.c_str(), g_series[i].name.c_str());
            if (ImGui::Selectable(label, selected)) {
                g_mainSel = i;
                g_mainCanvas.dirty = true;
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("매매대상으로 승격")) {
                    g_bus.Push(Cmd::PromoteTarget, g_series[i].code, i);
                }
                ImGui::EndPopup();
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("매매대상", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int index : g_targets) {
            if (index >= 0 && index < static_cast<int>(g_series.size())) {
                ImGui::BulletText("%s %s", g_series[index].code.c_str(), g_series[index].name.c_str());
            }
        }
        ImGui::TreePop();
    }
    ImGui::End();
}

static void DrawMainChart()''',
    "empty-safe symbol pool",
)

shell = replace_between(
    shell,
    "static void DrawMainChart()",
    "static void DrawMultiChart()",
    r'''static void DrawMainChart() {
    ImGui::Begin("주력 차트");
    if (g_series.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "실제 시세 데이터가 없습니다.");
        const std::string error = MarketDataErrorSnapshot();
        if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
        ImGui::End();
        return;
    }

    const int selected = std::clamp(g_mainSel, 0, static_cast<int>(g_series.size()) - 1);
    Series& series = g_series[selected];
    if (series.bars.size() < 2) {
        ImGui::Text("%s %s", series.code.c_str(), series.name.c_str());
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "실제 분봉 백필 결과가 비어 있습니다.");
        ImGui::End();
        return;
    }

    ImGui::Text("%s %s | 실제 시세", series.code.c_str(), series.name.c_str());
    ImGui::SameLine(); ImGui::TextDisabled("(휠=확대, 드래그=이동, 더블클릭=최신)");
    ChartWidget(g_mainCanvas, series, g_mainView, ImGui::GetContentRegionAvail(), P_showVolume, P_showIndex);
    ImGui::End();
}

static void DrawMultiChart()''',
    "empty-safe main chart",
)

shell = replace_between(
    shell,
    "static void DrawMultiChart()",
    "static void DrawScanner()",
    r'''static void DrawMultiChart() {
    if (!g_showMulti) return;
    ImGui::Begin("매매 멀티차트", &g_showMulti);
    if (g_series.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "실제 시세 데이터가 없어 멀티차트를 그리지 않습니다.");
        ImGui::End();
        return;
    }

    ImVec2 area = ImGui::GetContentRegionAvail();
    const int cols = 2, rows = 3;
    ImVec2 cell((area.x - 8) / cols, (area.y - 8) / rows);
    int cellIndex = 0;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < cols; ++column, ++cellIndex) {
            if (column) ImGui::SameLine();
            ImGui::BeginChild(ImGui::GetID(cellIndex + 1000), cell, true);
            const int seriesIndex = std::clamp(
                g_multiSel[cellIndex],
                0,
                static_cast<int>(g_series.size()) - 1);
            Series& series = g_series[seriesIndex];
            ImGui::TextUnformatted(series.name.c_str());
            if (series.bars.size() >= 2) {
                ChartWidget(g_multi[cellIndex], series, g_multiView[cellIndex], ImGui::GetContentRegionAvail(), false, P_showIndex);
            }
            else {
                ImGui::TextDisabled("실제 분봉 없음");
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

static void DrawScanner()''',
    "empty-safe multi chart",
)

shell = replace_between(
    shell,
    "static void DrawScanner()",
    "static void DrawProperty()",
    r'''static void DrawScanner() {
    ImGui::Begin("스캐너");
    if (g_series.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "실제 시세 유니버스가 없습니다.");
        ImGui::TextWrapped("선별 결과를 만들지 않습니다. 실제 종목·지수 백필과 실시간 체결 연결 오류를 먼저 해결해야 합니다.");
        ImGui::End();
        return;
    }

    ImGui::Text("선별 조건: 베타≥%.2f 상관≥%.2f 시차≥%d분 대금≥%.0f억", P_minBeta, P_minCorr, P_minLagMin, P_minTurnover);
    ImGui::Separator();
    if (ImGui::BeginTable("scan", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        const char* headers[] = { "종목","점수","베타","상관","시차","대금(억)","" };
        for (const char* header : headers) ImGui::TableSetupColumn(header);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(g_series.size()); ++i) {
            Series& series = g_series[i];
            const bool pass = series.beta >= P_minBeta && series.corr >= P_minCorr &&
                series.lag >= P_minLagMin && series.turnover >= P_minTurnover;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(series.name.c_str(), i == g_mainSel, ImGuiSelectableFlags_SpanAllColumns)) {
                g_mainSel = i;
                g_mainCanvas.dirty = true;
            }
            ImGui::TableNextColumn(); ImGui::TextColored(pass ? ImVec4(0.4f, 1, 0.5f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1), "%.0f", series.score);
            ImGui::TableNextColumn(); ImGui::Text("%.2f", series.beta);
            ImGui::TableNextColumn(); ImGui::Text("%.2f", series.corr);
            ImGui::TableNextColumn(); ImGui::Text("%d", series.lag);
            ImGui::TableNextColumn(); ImGui::Text("%.0f", series.turnover);
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            if (ImGui::SmallButton("승격")) g_bus.Push(Cmd::PromoteTarget, series.code, i);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

static void DrawProperty()''',
    "empty-safe scanner",
)

shell = replace_between(
    shell,
    "static void DrawDashboard()",
    "static void DrawLogWindow",
    r'''static void DrawDashboard()
{
    ImGui::Begin("대시보드");

    std::string selectedCode;
    std::string selectedName;
    {
        std::lock_guard<std::mutex> dataLock(g_dataMtx);
        if (!g_series.empty()) {
            const int selected = std::clamp(g_mainSel, 0, static_cast<int>(g_series.size()) - 1);
            selectedCode = g_series[selected].code;
            selectedName = g_series[selected].name;
        }
    }

    if (selectedCode.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "매수 대상 실제 시세가 없습니다.");
        const std::string error = MarketDataErrorSnapshot();
        if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
    }
    else {
        ImGui::Text("선택: %s %s", selectedCode.c_str(), selectedName.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("| 주문수량");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("##order_qty", &g_orderQty, 1, 10)) {
            g_orderQty = (std::max)(1, g_orderQty);
        }
        ImGui::SameLine();
        const bool canEnter = CanSubmitEntryOrders();
        if (!canEnter) ImGui::BeginDisabled();
        if (ImGui::Button("키움 모의매수")) {
            g_bus.Push(Cmd::MockBuy, selectedCode, g_orderQty);
        }
        if (!canEnter) ImGui::EndDisabled();
    }

    ImGui::SameLine();
    const bool canLiquidate = CanSubmitLiquidationOrders();
    if (!canLiquidate) ImGui::BeginDisabled();
    if (ImGui::Button("선택 청산")) g_bus.Push(Cmd::LiquidateSelected);
    if (!canLiquidate) ImGui::EndDisabled();

    ImGui::SameLine();
    const bool canArm = CanSubmitEntryOrders();
    if (!canArm) ImGui::BeginDisabled();
    if (g_observeMode.load()) {
        if (ImGui::Button("전략 가동")) g_bus.Push(Cmd::ArmStrategy);
    }
    else {
        if (ImGui::Button("관망 전환")) g_bus.Push(Cmd::DisarmStrategy);
    }
    if (!canArm) ImGui::EndDisabled();

    const std::vector<trading::PositionSnapshot> positions =
        g_tradingState.SnapshotPositions();

    trading::MoneyWon totalBuy = 0;
    trading::MoneyWon totalEvaluation = 0;
    for (const trading::PositionSnapshot& position : positions) {
        totalBuy += position.costBasisWon;
        totalEvaluation += position.EvaluationWon();
    }

    const trading::MoneyWon unrealizedPnl = totalEvaluation - totalBuy;
    const double unrealizedRate = totalBuy > 0
        ? static_cast<double>(unrealizedPnl) / static_cast<double>(totalBuy) * 100.0
        : 0.0;
    const trading::MoneyWon realizedPnl = g_tradingState.RealizedPnlWon();
    const trading::MoneyWon totalPnl = unrealizedPnl + realizedPnl;

    ImGui::Separator();
    ImGui::Text("키움 잔고 %zu종목   매입 %lld원   평가 %lld원",
        positions.size(), static_cast<long long>(totalBuy), static_cast<long long>(totalEvaluation));
    ImGui::SameLine();
    ImGui::TextColored(
        unrealizedPnl >= 0 ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f) : ImVec4(0.35f, 0.60f, 1.00f, 1.0f),
        "평가손익 %+.0f원 (%+.2f%%)", static_cast<double>(unrealizedPnl), unrealizedRate);
    ImGui::SameLine();
    ImGui::Text("실현 %+.0f원   총손익 %+.0f원", static_cast<double>(realizedPnl), static_cast<double>(totalPnl));

    if (positions.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("키움 계좌대조 결과 보유 포지션이 없습니다.");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable("pos", 8,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        const char* headers[] = { "선택", "종목", "수량", "평단", "현재가", "평가손익", "수익률", "청산" };
        for (const char* header : headers) ImGui::TableSetupColumn(header);
        ImGui::TableHeadersRow();

        for (const trading::PositionSnapshot& position : positions) {
            const trading::MoneyWon pnl = position.UnrealizedPnlWon();
            const double rate = position.costBasisWon > 0
                ? static_cast<double>(pnl) / static_cast<double>(position.costBasisWon) * 100.0
                : 0.0;
            ImGui::TableNextRow();
            ImGui::PushID(position.code.c_str());
            ImGui::TableNextColumn();
            bool selected = position.selected;
            if (ImGui::Checkbox("##position_selected", &selected)) {
                g_tradingState.SetSelected(position.code, selected);
            }
            ImGui::TableNextColumn(); ImGui::Text("%s %s", position.code.c_str(), position.name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d", position.quantity);
            ImGui::TableNextColumn(); ImGui::Text("%.2f", position.AveragePriceWon());
            ImGui::TableNextColumn(); ImGui::Text("%d", position.currentPriceWon);
            ImGui::TableNextColumn(); ImGui::Text("%+.0f", static_cast<double>(pnl));
            ImGui::TableNextColumn(); ImGui::Text("%+.2f%%", rate);
            ImGui::TableNextColumn();
            if (!canLiquidate) ImGui::BeginDisabled();
            if (ImGui::SmallButton("개별청산")) {
                g_bus.Push(Cmd::LiquidatePosition, position.code);
            }
            if (!canLiquidate) ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

static void DrawLogWindow''',
    "real-account dashboard",
)

shell = replace_between(
    shell,
    "static void DrawFaultWindow()",
    "// ─────────────────────────────── 엔진 스텁",
    r'''static void DrawFaultWindow()
{
    ImGui::Begin("결함");
    ImGui::TextDisabled("중앙 정책표 — 실제 결함만 집계하며 인위적 결함 주입 기능은 제거했습니다.");

    if (ImGui::BeginTable("ft", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        const char* headers[] = { "결함", "기본조치", "임계", "누적", "최근조치" };
        for (const char* header : headers) ImGui::TableSetupColumn(header);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(Fault::COUNT); ++i) {
            const Fault fault = static_cast<Fault>(i);
            const FaultRule& rule = g_faultPolicy.GetRule(fault);
            const FaultStat stat = g_faultPolicy.GetStat(fault);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(FaultPolicy::FaultName(fault));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(FaultPolicy::ActionName(rule.first));
            ImGui::TableNextColumn(); ImGui::Text("%d/%ds", rule.threshold, rule.windowSeconds);
            ImGui::TableNextColumn(); ImGui::Text("%d", stat.total);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(stat.total > 0 ? FaultPolicy::ActionName(stat.last) : "-");
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// ─────────────────────────────── 실제 런타임 명령 ───────────────────────────''',
    "remove artificial fault injection",
)

shell = replace_between(
    shell,
    "static void DrainCommands()",
    "// ─────────────────────────────── 레이아웃",
    r'''static void DrainCommands()
{
    Command command;
    while (g_bus.Pop(command)) {
        switch (command.type) {
        case Cmd::LoadSymbol: {
            if (command.arg.empty()) {
                SetMarketDataError("종목코드가 비어 있어 실제 시세 조회를 시작할 수 없습니다.");
                g_log.Add("DATA", "실시세 조회 거부: 종목코드 없음");
                break;
            }
            const std::string message =
                "실제 분봉 조회 어댑터가 연결되지 않았습니다: " + command.arg;
            SetMarketDataError(message);
            g_log.Add("DATA", "%s", message.c_str());
            break;
        }

        case Cmd::PromoteTarget:
            if (command.i0 < 0 || command.i0 >= static_cast<int>(g_series.size())) {
                g_log.Add("DATA", "매매대상 승격 거부: 실제 시세 인덱스 오류");
                break;
            }
            if (std::find(g_targets.begin(), g_targets.end(), command.i0) == g_targets.end()) {
                g_targets.push_back(command.i0);
                if (g_targets.size() <= 6) {
                    g_multiSel[g_targets.size() - 1] = command.i0;
                }
            }
            g_health.subCount = static_cast<int>(g_targets.size());
            g_log.Add("CMD", "매매대상 승격: %s", command.arg.c_str());
            break;

        case Cmd::OpenMultiChart:
            g_showMulti = true;
            g_log.Add("CMD", "매매 멀티차트 열기");
            break;

        case Cmd::MockBuy: {
            if (!CanSubmitEntryOrders()) {
                g_orderLog.Add("REJECT", "매수 거부: 키움 계좌대조와 실제 시세 준비가 모두 필요합니다.");
                break;
            }
            std::string name;
            trading::PriceWon price = 0;
            if (!TryGetLatestQuote(command.arg, name, price)) {
                g_orderLog.Add("REJECT", "매수 거부: 실제 현재가 없음 %s", command.arg.c_str());
                break;
            }
            trading::OrderIntent intent;
            intent.code = command.arg;
            intent.name = name;
            intent.side = trading::StockOrderSide::Buy;
            intent.type = trading::StockOrderType::Market;
            intent.quantity = (std::max)(1, command.i0);
            std::string error;
            if (!g_kiwoomRunner || !g_kiwoomRunner->SubmitOrder(intent, error)) {
                g_orderLog.Add("REJECT", "키움 모의매수 거부: %s", error.c_str());
            }
            else {
                g_orderLog.Add("ORDER", "키움 모의매수 전송 %s %s %d주 시장가",
                    intent.code.c_str(), intent.name.c_str(), intent.quantity);
            }
            break;
        }

        case Cmd::LiquidatePosition: {
            if (!CanSubmitLiquidationOrders()) {
                g_orderLog.Add("REJECT", "개별청산 거부: 키움 계좌대조가 완료되지 않았습니다.");
                break;
            }
            trading::PositionSnapshot position;
            if (!FindPosition(command.arg, position)) {
                g_orderLog.Add("REJECT", "개별청산 거부: 키움 보유 포지션 없음 %s", command.arg.c_str());
                break;
            }
            trading::OrderIntent intent;
            intent.code = position.code;
            intent.name = position.name;
            intent.side = trading::StockOrderSide::Sell;
            intent.type = trading::StockOrderType::Market;
            intent.quantity = position.quantity;
            std::string error;
            if (!g_kiwoomRunner || !g_kiwoomRunner->SubmitOrder(intent, error)) {
                g_orderLog.Add("REJECT", "개별청산 주문 거부: %s", error.c_str());
            }
            else {
                g_orderLog.Add("ORDER", "개별청산 주문 전송 %s %d주 시장가",
                    position.code.c_str(), position.quantity);
            }
            break;
        }

        case Cmd::LiquidateSelected:
        case Cmd::LiquidateAll: {
            if (!CanSubmitLiquidationOrders()) {
                g_orderLog.Add("REJECT", "청산 거부: 키움 계좌대조가 완료되지 않았습니다.");
                break;
            }
            const bool selectedOnly = command.type == Cmd::LiquidateSelected;
            std::string error;
            if (!g_kiwoomRunner || !g_kiwoomRunner->SubmitLiquidation(selectedOnly, error)) {
                g_orderLog.Add("REJECT", "%s 주문 거부: %s",
                    selectedOnly ? "선택청산" : "전량청산", error.c_str());
            }
            else {
                g_orderLog.Add("ORDER", "%s 주문 전송",
                    selectedOnly ? "선택청산" : "전량청산");
            }
            break;
        }

        case Cmd::ArmStrategy:
            if (!CanSubmitEntryOrders()) {
                g_log.Add("REJECT", "전략 가동 거부: 실제 시세 준비와 키움 계좌대조가 필요합니다.");
                break;
            }
            g_observeMode = false;
            g_log.Add("CMD", "전략 가동");
            break;

        case Cmd::DisarmStrategy:
            g_observeMode = true;
            g_log.Add("CMD", "관망 전환");
            break;

        case Cmd::ResetFeed:
            SetMarketDataError("실제 시세 피드 재연결 기능이 아직 연결되지 않았습니다.");
            g_log.Add("DATA", "실시세 피드 리셋 요청 실패");
            break;

        default:
            g_log.Add("CMD", "미구현 커맨드");
            break;
        }
    }
}

// ─────────────────────────────── 레이아웃''',
    "remove synthetic feed and local execution",
)

shell = shell.replace(
    'CreateWindowW(wc.lpszClassName, L"Trading Shell — UI 골격",',
    'CreateWindowW(wc.lpszClassName, L"Trading Shell — 실데이터 전용",',
    1,
)

shell = replace_between(
    shell,
    "    RegisterParams();\n\n    const trading::ConfigLoadResult configLoad",
    "    bool firstLayout = true;",
    r'''    RegisterParams();

    const trading::ConfigLoadResult configLoad = trading::LoadRuntimeConfig(".");
    if (configLoad.ok) {
        g_runtimeConfig = configLoad.config;
    }
    else {
        g_runtimeConfig = trading::RuntimeConfig{};
        g_runtimeConfigError = configLoad.error;
        g_observeMode = true;
    }

    g_health.wsUp = false;
    InitializeRealDataState();
    g_targets.clear();
    g_health.subCount = 0;
    g_marketDataDirty.store(true, std::memory_order_release);
    g_health.bootMs = (NowSec() - t0) * 1000.0;
    g_log.Add("SYS", "셸 기동 완료 (%.0fms), 실데이터 전용, 합성 시세/포지션/체결 없음",
        g_health.bootMs.load());

    if (!g_runtimeConfigError.empty()) {
        g_log.Add("FAULT", "환경설정 오류로 주문·전략 잠금: %s", g_runtimeConfigError.c_str());
    }
    else {
        trading::platform::KiwoomRunnerCallbacks callbacks;
        callbacks.log = [](const char* category, const std::string& message) {
            if (strcmp(category, "ORDER") == 0 || strcmp(category, "REJECT") == 0) {
                g_orderLog.Add(category, "%s", message.c_str());
            }
            else {
                g_log.Add(category, "%s", message.c_str());
            }
        };
        callbacks.wakeUi = [] {
            g_marketDataDirty.store(true, std::memory_order_release);
            WakeFrames(60);
        };
        callbacks.setObserveMode = [](bool enabled) {
            g_observeMode.store(enabled, std::memory_order_release);
        };

        g_kiwoomRunner = std::make_unique<trading::platform::KiwoomRuntimeRunner>(
            g_kiwoomRuntimeEngine,
            std::make_unique<trading::platform::WinHttpKiwoomTransport>(),
            std::move(callbacks));

        std::string runtimeError;
        if (!g_kiwoomRunner->Start(g_runtimeConfig, runtimeError)) {
            g_observeMode = true;
            g_log.Add("FAULT", "키움 모의투자 런타임 시작 실패: %s", runtimeError.c_str());
        }
        else {
            g_log.Add("SYS", "키움 모의투자 연결 시작: 토큰 → WS → 00/04 → 계좌대조");
        }
    }

    bool firstLayout = true;''',
    "fail-closed startup",
)

shell = replace_once(
    shell,
    "    g_feedRun = false; feed.join();\n"
    "    g_mainCanvas.Release(); for (auto& c : g_multi) c.Release();",
    "    g_mainCanvas.Release(); for (auto& c : g_multi) c.Release();",
    "remove synthetic feed shutdown",
)

for forbidden in (
    "MakeMockData",
    "MockFeedThread",
    "std::mt19937",
    "normal_distribution",
    "ApplyLocalFill",
    "g_localOrderSequence",
    "RuntimeMode::LocalMock",
    "LOCAL MOCK",
    "효성중공업",
):
    if forbidden in shell:
        raise RuntimeError(f"synthetic production marker remains in shell_main.cpp: {forbidden}")

write("shell_main.cpp", shell)

runtime_h = read("core/runtime_config.h")
runtime_h = replace_once(
    runtime_h,
    "    enum class RuntimeMode\n"
    "    {\n"
    "        LocalMock,\n"
    "        KiwoomMock\n"
    "    };",
    "    enum class RuntimeMode\n"
    "    {\n"
    "        Unconfigured,\n"
    "        KiwoomMock\n"
    "    };",
    "runtime mode enum",
)
runtime_h = runtime_h.replace(
    "RuntimeMode mode = RuntimeMode::LocalMock;",
    "RuntimeMode mode = RuntimeMode::Unconfigured;",
    1,
)
write("core/runtime_config.h", runtime_h)

runtime_cpp = read("core/runtime_config.cpp")
runtime_cpp = replace_between(
    runtime_cpp,
    "        const std::string mode =\n",
    "        result.config.appKey =",
    "        const std::string mode =\n"
    "            FindValue(values, \"TRADING_MODE\");\n\n"
    "        if (mode != \"KIWOOM_MOCK\") {\n"
    "            result.error =\n"
    "                \"TRADING_MODE=KIWOOM_MOCK is required; synthetic LOCAL_MOCK was removed\";\n"
    "            return result;\n"
    "        }\n\n"
    "        result.config.mode = RuntimeMode::KiwoomMock;\n\n"
    "        result.config.appKey =",
    "require Kiwoom mode",
)
runtime_cpp = runtime_cpp.replace(
    "        if (\n"
    "            result.config.mode == RuntimeMode::LocalMock &&\n"
    "            result.config.HasKiwoomCredentials())\n"
    "        {\n"
    "            result.warnings.push_back(\n"
    "                \"Kiwoom credentials are present but LOCAL_MOCK is active\");\n"
    "        }\n\n",
    "",
    1,
)
if "RuntimeMode::LocalMock" in runtime_cpp or "LOCAL_MOCK is active" in runtime_cpp:
    raise RuntimeError("LOCAL_MOCK remains in runtime_config.cpp")
write("core/runtime_config.cpp", runtime_cpp)

session_cpp = read("core/kiwoom_session.cpp")
session_cpp = replace_once(
    session_cpp,
    "        if (config_.mode == RuntimeMode::LocalMock) {\n"
    "            state_ = KiwoomSessionState::Stopped;\n"
    "            return {};\n"
    "        }\n\n",
    "        if (config_.mode != RuntimeMode::KiwoomMock) {\n"
    "            state_ = KiwoomSessionState::ConfigurationError;\n"
    "            lastError_ = \"KIWOOM_MOCK runtime configuration is required\";\n"
    "            return {\n"
    "                MakeAction(\n"
    "                    KiwoomSessionActionType::EnterObserveMode,\n"
    "                    lastError_)\n"
    "            };\n"
    "        }\n\n",
    "remove local session bypass",
)
write("core/kiwoom_session.cpp", session_cpp)

config_tests = read("tests/runtime_config_tests.cpp")
start = config_tests.find("    void TestRuntimeConfig()")
end = config_tests.find("}\n\nint main()", start)
if start < 0 or end < 0:
    raise RuntimeError("runtime config test block not found")
new_test = r'''    void TestRuntimeConfig()
    {
        std::map<std::string, std::string> emptyValues;
        trading::ConfigLoadResult missingMode =
            trading::BuildRuntimeConfig(emptyValues, "missing.env");
        Check(!missingMode.ok,
              "missing TRADING_MODE must fail closed");
        Check(missingMode.config.mode == trading::RuntimeMode::Unconfigured,
              "missing mode must remain unconfigured");

        std::map<std::string, std::string> localValues;
        localValues["TRADING_MODE"] = "LOCAL_MOCK";
        trading::ConfigLoadResult removedLocal =
            trading::BuildRuntimeConfig(localValues, "local.env");
        Check(!removedLocal.ok,
              "removed LOCAL_MOCK configuration must be rejected");
        Check(removedLocal.error.find("removed") != std::string::npos,
              "LOCAL_MOCK rejection must explain removal");

        std::map<std::string, std::string> mockValues;
        mockValues["TRADING_MODE"] = "KIWOOM_MOCK";
        mockValues["KIWOOM_MOCK_APP_KEY"] = "mock-app";
        mockValues["KIWOOM_MOCK_SECRET_KEY"] = "mock-secret";
        mockValues["KIWOOM_ACCOUNT"] = "12345678";

        trading::ConfigLoadResult mock =
            trading::BuildRuntimeConfig(mockValues, "mock.env");
        Check(mock.ok, "KIWOOM_MOCK configuration must be valid");
        Check(mock.config.mode == trading::RuntimeMode::KiwoomMock,
              "KIWOOM_MOCK mode mismatch");
        Check(mock.config.appKey == "mock-app", "mock App Key mismatch");
        Check(mock.config.secretKey == "mock-secret", "mock App Secret mismatch");
        Check(mock.config.accountNumber == "12345678", "mock account mismatch");
        Check(mock.config.restBaseUrl == "https://mockapi.kiwoom.com",
              "default mock REST URL mismatch");
        Check(mock.config.webSocketUrl ==
                  "wss://mockapi.kiwoom.com:10000/api/dostk/websocket",
              "default mock WebSocket URL mismatch");

        mockValues.erase("KIWOOM_MOCK_SECRET_KEY");
        trading::ConfigLoadResult missingCredential =
            trading::BuildRuntimeConfig(mockValues);
        Check(!missingCredential.ok,
              "KIWOOM_MOCK without credentials must fail");
        Check(missingCredential.error.find("App Key") != std::string::npos,
              "missing credentials error mismatch");
    }
'''
config_tests = config_tests[:start] + new_test + config_tests[end:]
write("tests/runtime_config_tests.cpp", config_tests)

verify_script = r'''$ErrorActionPreference = 'Stop'

$productionFiles = @(
    '.\shell_main.cpp'
    '.\core\runtime_config.h'
    '.\core\runtime_config.cpp'
    '.\core\kiwoom_session.cpp'
)

$forbidden = @(
    'MakeMockData',
    'MockFeedThread',
    'std::mt19937',
    'normal_distribution',
    'ApplyLocalFill',
    'g_localOrderSequence',
    'RuntimeMode::LocalMock',
    'LOCAL MOCK',
    '효성중공업'
)

foreach ($file in $productionFiles) {
    $content = Get-Content $file -Raw
    foreach ($marker in $forbidden) {
        if ($content.Contains($marker)) {
            throw "Synthetic production marker '$marker' remains in $file"
        }
    }
}

$shell = Get-Content '.\shell_main.cpp' -Raw
$required = @(
    'CPPCHART_REAL_DATA_ONLY',
    'MarketDataState::Error',
    '실제 시세 백필과 실시간 체결 수신이 아직 연결되지 않았습니다',
    'CanSubmitEntryOrders',
    'CanSubmitLiquidationOrders'
)
foreach ($marker in $required) {
    if (-not $shell.Contains($marker)) {
        throw "Required real-data-only marker '$marker' is missing"
    }
}

Write-Host 'Production runtime contains no synthetic market data, positions, fills or feed.'
'''
write("scripts/verify_real_data_only.ps1", verify_script)

workflow = read(".github/workflows/windows-ci.yml")
if "Verify real-data-only production runtime" not in workflow:
    anchor = "      - name: Verify core dependency boundary\n        shell: pwsh\n        run: .\\scripts\\verify_core_boundary.ps1\n"
    insertion = anchor + "\n      - name: Verify real-data-only production runtime\n        shell: pwsh\n        run: .\\scripts\\verify_real_data_only.ps1\n"
    workflow = replace_once(
        workflow,
        anchor,
        insertion,
        "CI real-data-only gate",
    )
workflow = workflow.replace(
    "            'CPPCHART_KIWOOM_RUNTIME_CONNECTED',",
    "            'CPPCHART_KIWOOM_RUNTIME_CONNECTED',\n"
    "            'CPPCHART_REAL_DATA_ONLY',",
    1,
)
workflow = workflow.replace(
    "            'static int  g_wakeFrames'",
    "            'static int  g_wakeFrames',\n"
    "            'MakeMockData',\n"
    "            'MockFeedThread',\n"
    "            'std::mt19937',\n"
    "            'ApplyLocalFill',\n"
    "            'RuntimeMode::LocalMock',\n"
    "            'LOCAL MOCK'",
    1,
)
write(".github/workflows/windows-ci.yml", workflow)

for obsolete in (
    "scripts/migrate_shell_runtime.ps1",
    "scripts/migrate_shell_runtime.py",
    "scripts/harden_shell_thread_handoff.py",
    "scripts/integrate_kiwoom_runtime_shell.py",
    "scripts/fix_runtime_receiver.py",
):
    path = ROOT / obsolete
    if path.exists():
        path.unlink()

print("Removed synthetic production runtime and enabled fail-closed real-data-only mode")
