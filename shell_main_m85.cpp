#include <ctime>

static void DrawTradingToolbarM85();

#define M85_JOIN_INNER(left, right) left##right
#define M85_JOIN(left, right) M85_JOIN_INNER(left, right)
#define M85_TOOLBAR_NAME_19 DrawTradingToolbarLegacy
#define M85_TOOLBAR_NAME_562 DrawTradingToolbarLegacy
#define M85_TOOLBAR_NAME_2381 DrawTradingToolbarM85
#define DrawTradingToolbar M85_JOIN(M85_TOOLBAR_NAME_, __LINE__)
#include "shell_main_m82.cpp"
#undef DrawTradingToolbar
#undef M85_TOOLBAR_NAME_2381
#undef M85_TOOLBAR_NAME_562
#undef M85_TOOLBAR_NAME_19
#undef M85_JOIN
#undef M85_JOIN_INNER

namespace
{
    bool g_m85ShowInstrumentRow = true;
    bool g_m85ShowTradingRow = true;
    bool g_m85ShowChartRow = true;
    int g_m85VisibleBars = 120;
    char g_m85AnchorDate[16] = "";
    std::string g_m85DateSymbol;
    double g_m85LastAdditionalRequestAt = -1000.0;

    bool IsSelectableSymbolCode(const std::string& code) noexcept
    {
        return IsSixDigitCode(code) || IsNxtCode(code);
    }

    bool FormatTradingDate(
        trading::EpochMillis timestampMs,
        char* buffer,
        std::size_t bufferSize)
    {
        if (timestampMs <= 0 || buffer == nullptr || bufferSize < 11U) {
            return false;
        }
        const std::time_t seconds = static_cast<std::time_t>(timestampMs / 1000);
        std::tm local{};
#if defined(_WIN32)
        if (localtime_s(&local, &seconds) != 0) return false;
#else
        if (localtime_r(&seconds, &local) == nullptr) return false;
#endif
        std::snprintf(
            buffer,
            bufferSize,
            "%04d-%02d-%02d",
            local.tm_year + 1900,
            local.tm_mon + 1,
            local.tm_mday);
        return true;
    }

    void SetAnchorDateToLatest(
        const trading::app::MarketDataSnapshot& market)
    {
        trading::EpochMillis timestamp = 0;
        const std::vector<trading::EpochMillis>& timestamps =
            g_mainRenderSurface.timeAxis.Timestamps();
        if (!timestamps.empty()) timestamp = timestamps.back();
        else if (market.hasLatestBar) timestamp = market.latestBar.closeTimestampMs;

        char date[16]{};
        if (FormatTradingDate(timestamp, date, sizeof(date))) {
            std::snprintf(
                g_m85AnchorDate,
                sizeof(g_m85AnchorDate),
                "%s",
                date);
        }
    }

    void SyncAnchorDateSymbol(
        const trading::app::MarketDataSnapshot& market)
    {
        if (market.code.empty()) return;
        if (g_m85DateSymbol == market.code && g_m85AnchorDate[0] != '\0') {
            return;
        }
        g_m85DateSymbol = market.code;
        SetAnchorDateToLatest(market);
    }

    void ApplyVisibleBarCount(bool followLatest)
    {
        const trading::render::OrdinalTimeAxis& axis =
            g_mainRenderSurface.timeAxis;
        if (axis.Empty()) return;

        g_m85VisibleBars = (std::max)(12, (std::min)(2000, g_m85VisibleBars));
        const double minimum = axis.Minimum();
        const double maximum = axis.Maximum();
        const double available = (std::max)(0.0, maximum - minimum);
        const double span = (std::min)(
            available,
            static_cast<double>((std::max)(1, g_m85VisibleBars - 1)));

        double end = followLatest || !g_mainRenderSurface.viewport.initialized
            ? maximum
            : g_mainRenderSurface.viewport.visibleEnd;
        end = (std::max)(minimum, (std::min)(maximum, end));
        if (end - span < minimum) end = (std::min)(maximum, minimum + span);

        g_mainRenderSurface.viewport.visibleEnd = end;
        g_mainRenderSurface.viewport.visibleStart = (std::max)(minimum, end - span);
        g_mainRenderSurface.viewport.initialized = true;
        g_mainRenderSurface.viewport.autoScroll = followLatest;
        g_mainRenderSurface.defaultVisibleSpan = span;
        g_mainRenderSurface.dirty = true;
        WakeFrames(4);
    }

    bool MoveViewportToDate(
        const char* date,
        std::string& error)
    {
        if (date == nullptr || std::strlen(date) != 10U) {
            error = "기준일은 YYYY-MM-DD 형식이어야 합니다.";
            return false;
        }

        const trading::render::OrdinalTimeAxis& axis =
            g_mainRenderSurface.timeAxis;
        if (axis.Empty()) {
            error = "차트 데이터가 없어 기준일로 이동할 수 없습니다.";
            return false;
        }

        int targetIndex = -1;
        const std::vector<trading::EpochMillis>& timestamps = axis.Timestamps();
        for (std::size_t index = 0; index < timestamps.size(); ++index) {
            char candidate[16]{};
            if (FormatTradingDate(timestamps[index], candidate, sizeof(candidate)) &&
                std::strcmp(candidate, date) == 0)
            {
                targetIndex = static_cast<int>(index);
            }
        }
        if (targetIndex < 0) {
            error = "현재 수신된 차트 데이터에 해당 거래일이 없습니다.";
            return false;
        }

        g_m85VisibleBars = (std::max)(12, (std::min)(2000, g_m85VisibleBars));
        const double minimum = axis.Minimum();
        const double maximum = axis.Maximum();
        const double available = (std::max)(0.0, maximum - minimum);
        const double span = (std::min)(
            available,
            static_cast<double>((std::max)(1, g_m85VisibleBars - 1)));
        double end = static_cast<double>(targetIndex);
        if (end - span < minimum) end = (std::min)(maximum, minimum + span);

        g_mainRenderSurface.viewport.visibleEnd = end;
        g_mainRenderSurface.viewport.visibleStart = (std::max)(minimum, end - span);
        g_mainRenderSurface.viewport.initialized = true;
        g_mainRenderSurface.viewport.autoScroll = end >= maximum - 0.0001;
        g_mainRenderSurface.defaultVisibleSpan = span;
        g_mainRenderSurface.dirty = true;
        WakeFrames(4);
        error.clear();
        return true;
    }

    bool ShiftLoadedTradingDate(
        int direction,
        std::string& error)
    {
        const std::vector<trading::EpochMillis>& timestamps =
            g_mainRenderSurface.timeAxis.Timestamps();
        if (timestamps.empty()) {
            error = "차트 데이터가 없어 거래일을 이동할 수 없습니다.";
            return false;
        }

        std::vector<std::string> dates;
        dates.reserve(timestamps.size());
        for (trading::EpochMillis timestamp : timestamps) {
            char date[16]{};
            if (!FormatTradingDate(timestamp, date, sizeof(date))) continue;
            if (dates.empty() || dates.back() != date) dates.emplace_back(date);
        }
        if (dates.empty()) {
            error = "수신된 차트 데이터의 거래일을 해석하지 못했습니다.";
            return false;
        }

        int current = static_cast<int>(dates.size()) - 1;
        for (int index = 0; index < static_cast<int>(dates.size()); ++index) {
            if (dates[static_cast<std::size_t>(index)] == g_m85AnchorDate) {
                current = index;
                break;
            }
        }
        const int next = (std::max)(
            0,
            (std::min)(static_cast<int>(dates.size()) - 1, current + direction));
        std::snprintf(
            g_m85AnchorDate,
            sizeof(g_m85AnchorDate),
            "%s",
            dates[static_cast<std::size_t>(next)].c_str());
        return MoveViewportToDate(g_m85AnchorDate, error);
    }

    void DrawToolbarVisibilityMenu()
    {
        if (ImGui::Button("툴바 ▾")) {
            ImGui::OpenPopup("toolbar_visibility_menu");
        }
        if (ImGui::BeginPopup("toolbar_visibility_menu")) {
            ImGui::MenuItem("종목정보", nullptr, &g_m85ShowInstrumentRow);
            ImGui::MenuItem("매매정보", nullptr, &g_m85ShowTradingRow);
            ImGui::MenuItem("차트도구", nullptr, &g_m85ShowChartRow);
            ImGui::Separator();
            if (ImGui::MenuItem("모두 표시")) {
                g_m85ShowInstrumentRow = true;
                g_m85ShowTradingRow = true;
                g_m85ShowChartRow = true;
            }
            ImGui::EndPopup();
        }
    }

    void DrawInstrumentRow(
        const trading::app::MarketDataSnapshot& market,
        const std::string& selectedCode,
        bool quoteReady)
    {
        ImGui::TextDisabled("종목");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(112.0f);
        ImGui::M82InputText("##symbol", g_symbolInput, sizeof(g_symbolInput));
        ImGui::SameLine();

        const bool validCode = IsSelectableSymbolCode(selectedCode);
        if (!validCode) ImGui::BeginDisabled();
        if (ImGui::Button("차트 조회")) {
            g_commandBus.Push(Cmd::LoadSymbol, selectedCode, g_timeFrameIndex);
        }
        if (!validCode) ImGui::EndDisabled();

        trading::SymbolCatalogEntry selectedEntry;
        const bool hasSelectedEntry =
            TryResolveSymbolEntry(selectedCode, selectedEntry);
        ImGui::SameLine();
        if (hasSelectedEntry) {
            ImGui::Text("%s", selectedEntry.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", MarketDisplayName(selectedEntry.market));
        }
        else if (!selectedCode.empty()) {
            ImGui::TextDisabled("종목명 확인 중");
        }
        else {
            ImGui::TextDisabled("종목을 검색하거나 코드를 입력하세요");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (!quoteReady) {
            ImGui::TextDisabled(
                market.state == trading::app::MarketDataState::Loading
                    ? "실제 시세 조회 중"
                    : "선택 종목 실제 시세 없음");
            return;
        }

        const long long current = market.latestBar.close;
        const long long change = current - market.latestBar.open;
        const double changeRate = market.latestBar.open != 0
            ? static_cast<double>(change) /
                static_cast<double>(market.latestBar.open) * 100.0
            : 0.0;
        const ImVec4 directionColor = PriceDirectionColor(change);
        ImGui::TextColored(
            directionColor,
            "%s원",
            FormatInteger(current).c_str());
        ImGui::SameLine();
        ImGui::TextColored(
            directionColor,
            "현재봉 %s%s원 (%+.2f%%)",
            change > 0 ? "+" : "",
            FormatInteger(change).c_str(),
            changeRate);
        ImGui::SameLine();
        ImGui::TextDisabled(
            "O %s  H %s  L %s  C %s  V %s  %zu봉",
            FormatInteger(market.latestBar.open).c_str(),
            FormatInteger(market.latestBar.high).c_str(),
            FormatInteger(market.latestBar.low).c_str(),
            FormatInteger(market.latestBar.close).c_str(),
            FormatInteger(static_cast<long long>(market.latestBar.volume)).c_str(),
            market.barCount);
    }

    void DrawTradingRow(
        const trading::app::MarketDataSnapshot& market,
        const trading::KiwoomRuntimeSnapshot& runtime,
        const std::string& selectedCode,
        bool quoteReady,
        bool socketUp,
        bool& liquidateAll)
    {
        ImGui::TextDisabled("매매");
        ImGui::SameLine();
        ImGui::TextDisabled("수량");
        ImGui::SameLine();
        if (ImGui::SmallButton("-##qty")) {
            g_orderQuantity = (std::max)(1, g_orderQuantity - 1);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(58.0f);
        if (ImGui::InputInt("##toolbar_order_quantity", &g_orderQuantity, 0, 0)) {
            g_orderQuantity = (std::max)(1, g_orderQuantity);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+##qty")) ++g_orderQuantity;

        ImGui::SameLine();
        const bool canBuy = CanSubmitEntryOrders() && quoteReady;
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(0.68f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(0.86f, 0.18f, 0.18f, 1.0f));
        if (!canBuy) ImGui::BeginDisabled();
        if (ImGui::Button("시장가 매수")) {
            g_commandBus.Push(Cmd::MockBuy, market.code, g_orderQuantity);
        }
        if (!canBuy) ImGui::EndDisabled();
        ImGui::PopStyleColor(2);

        trading::PositionSnapshot position;
        const bool hasPosition =
            !selectedCode.empty() && FindPosition(selectedCode, position);
        ImGui::SameLine();
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(0.12f, 0.28f, 0.68f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(0.18f, 0.40f, 0.88f, 1.0f));
        const bool canSellAll = CanSubmitLiquidationOrders() && hasPosition;
        if (!canSellAll) ImGui::BeginDisabled();
        if (ImGui::Button("보유 전량매도")) {
            g_commandBus.Push(Cmd::LiquidatePosition, selectedCode);
        }
        if (!canSellAll) ImGui::EndDisabled();
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        if (hasPosition) {
            ImGui::TextDisabled(
                "보유 %d주  평단 %.0f  손익 %+.0f (%+.2f%%)",
                position.quantity,
                position.AveragePriceWon(),
                static_cast<double>(position.UnrealizedPnlWon()),
                position.costBasisWon > 0
                    ? static_cast<double>(position.UnrealizedPnlWon()) /
                        static_cast<double>(position.costBasisWon) * 100.0
                    : 0.0);
        }
        else {
            ImGui::TextDisabled("선택 종목 보유 없음");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        const bool observe = g_observeMode.load(std::memory_order_acquire);
        if (observe) {
            const bool canActivate = CanActivateEntries();
            if (!canActivate) ImGui::BeginDisabled();
            if (ImGui::Button("관망 ●")) {
                g_commandBus.Push(Cmd::ArmStrategy);
            }
            if (!canActivate) ImGui::EndDisabled();
        }
        else if (ImGui::Button("진입허용 ●")) {
            g_commandBus.Push(Cmd::DisarmStrategy);
        }

        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
            "모의");
        ImGui::SameLine();
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
            runtime.orderSubmissionAllowed ? "주문가능" : "주문잠금");

        if (ImGui::IsItemHovered()) {
            const trading::EpochMillis tradeAgeMs =
                market.lastStockTradeTimestampMs > 0
                ? (std::max)(
                    static_cast<trading::EpochMillis>(0),
                    SystemNowEpochMillis() - market.lastStockTradeTimestampMs)
                : 0;
            ImGui::BeginTooltip();
            ImGui::Text("세션: %s", KiwoomSessionStateLabel(runtime.sessionState));
            ImGui::Text(
                "시장데이터: %s",
                trading::app::MarketDataModule::StateName(market.state));
            ImGui::Text(
                "부팅 %.0fms / 렌더 %.1fHz",
                g_bootMilliseconds,
                g_renderRateHz);
            if (market.lastStockTradeTimestampMs > 0) {
                ImGui::Text(
                    "0B %llu건 / 최근 %lldms",
                    static_cast<unsigned long long>(market.stockTradeTickCount),
                    static_cast<long long>(tradeAgeMs));
            }
            ImGui::EndTooltip();
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(0.72f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(0.88f, 0.18f, 0.18f, 1.0f));
        const bool canLiquidateAll = CanSubmitLiquidationOrders();
        if (!canLiquidateAll) ImGui::BeginDisabled();
        liquidateAll = ImGui::Button("계좌 전량청산");
        if (!canLiquidateAll) ImGui::EndDisabled();
        ImGui::PopStyleColor(2);
    }

    void DrawChartRow(
        const trading::app::MarketDataSnapshot& market,
        const std::string& selectedCode)
    {
        SyncAnchorDateSymbol(market);

        ImGui::TextDisabled("차트");
        ImGui::SameLine();
        DrawTimeFrameButtons(selectedCode);

        ImGui::SameLine();
        ImGui::TextDisabled("| 표시");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(62.0f);
        if (ImGui::InputInt("##visible_bars", &g_m85VisibleBars, 0, 0)) {
            g_m85VisibleBars = (std::max)(12, (std::min)(2000, g_m85VisibleBars));
        }
        ImGui::SameLine();
        if (ImGui::Button("봉 적용")) {
            ApplyVisibleBarCount(false);
        }

        ImGui::SameLine();
        ImGui::TextDisabled("| 기준일");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(105.0f);
        ImGui::InputText(
            "##anchor_date",
            g_m85AnchorDate,
            sizeof(g_m85AnchorDate));
        ImGui::SameLine();
        const bool hasChartData = !g_mainRenderSurface.timeAxis.Empty();
        if (!hasChartData) ImGui::BeginDisabled();
        if (ImGui::SmallButton("◀")) {
            std::string error;
            if (!ShiftLoadedTradingDate(-1, error)) {
                g_log.Add("REJECT", "%s", error.c_str());
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("이동")) {
            std::string error;
            if (!MoveViewportToDate(g_m85AnchorDate, error)) {
                g_log.Add("REJECT", "%s", error.c_str());
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("오늘/최신")) {
            SetAnchorDateToLatest(market);
            ApplyVisibleBarCount(true);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("▶")) {
            std::string error;
            if (!ShiftLoadedTradingDate(1, error)) {
                g_log.Add("REJECT", "%s", error.c_str());
            }
        }
        if (!hasChartData) ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        const bool canReload = !market.code.empty();
        if (!canReload) ImGui::BeginDisabled();
        if (ImGui::Button("재조회")) {
            g_commandBus.Push(Cmd::ResetFeed);
        }
        if (!canReload) ImGui::EndDisabled();

        const bool hasMore =
            (market.continuation.continueYn == "Y" ||
             market.continuation.continueYn == "y") &&
            !market.continuation.nextKey.empty();
        ImGui::SameLine();
        if (!hasMore) ImGui::BeginDisabled();
        if (ImGui::Button("추가데이터")) {
            const double now = NowSeconds();
            if (now - g_m85LastAdditionalRequestAt < 1.1) {
                g_log.Add("REJECT", "추가 과거데이터 요청 간격을 기다리세요.");
            }
            else {
                g_m85LastAdditionalRequestAt = now;
                std::string error;
                if (!g_runtimeRunner ||
                    !g_runtimeRunner->RequestStockMinuteBars(
                        market.code,
                        market.minuteUnit,
                        market.continuation,
                        error))
                {
                    g_log.Add("FAULT", "추가 과거데이터 요청 실패: %s", error.c_str());
                }
                else {
                    g_log.Add(
                        "DATA",
                        "추가 과거데이터 요청: %s %d분 현재 %zu봉",
                        market.code.c_str(),
                        market.minuteUnit,
                        market.barCount);
                }
            }
        }
        if (!hasMore) ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled(
            "수신 %zu봉%s",
            market.barCount,
            hasMore ? " / 추가 가능" : "");
    }
}

static void DrawTradingToolbarM85()
{
    ImGui::PushID("configurable_trading_toolbar");
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(6.0f, 4.0f));

    const trading::app::MarketDataSnapshot market =
        g_marketDataModule.Snapshot();
    const trading::KiwoomRuntimeSnapshot runtime = RuntimeSnapshot();
    const std::string selectedCode = NormalizeCode(g_symbolInput);
    const bool quoteReady =
        market.state == trading::app::MarketDataState::Ready &&
        market.hasLatestBar &&
        !selectedCode.empty() &&
        market.code == selectedCode;
    const bool socketUp =
        runtime.sessionState == trading::KiwoomSessionState::LoginPending ||
        runtime.sessionState == trading::KiwoomSessionState::RegistrationPending ||
        runtime.sessionState == trading::KiwoomSessionState::ReconciliationPending ||
        runtime.sessionState == trading::KiwoomSessionState::Ready;

    DrawToolbarVisibilityMenu();

    if (g_m85ShowInstrumentRow) {
        ImGui::SameLine();
        DrawInstrumentRow(market, selectedCode, quoteReady);
    }

    bool liquidateAll = false;
    if (g_m85ShowTradingRow) {
        ImGui::NewLine();
        DrawTradingRow(
            market,
            runtime,
            selectedCode,
            quoteReady,
            socketUp,
            liquidateAll);
    }

    if (g_m85ShowChartRow) {
        ImGui::NewLine();
        DrawChartRow(market, selectedCode);
    }

    if (!g_m85ShowInstrumentRow &&
        !g_m85ShowTradingRow &&
        !g_m85ShowChartRow)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("툴바 행이 모두 숨겨졌습니다.");
    }

    ImGui::PopStyleVar();
    ImGui::PopID();

    if (liquidateAll) ImGui::OpenPopup("confirm_liquidate_all_m85");
    if (ImGui::BeginPopupModal(
            "confirm_liquidate_all_m85",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("키움 모의계좌의 보유 전 종목을 시장가로 청산합니다.");
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
