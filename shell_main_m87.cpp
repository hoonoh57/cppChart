#include "imgui.h"

static void DrawTradingToolbarM87();

#define M87_JOIN_INNER(left, right) left##right
#define M87_JOIN(left, right) M87_JOIN_INNER(left, right)
#define M87_TOOLBAR_NAME_3 DrawTradingToolbarM85Legacy
#define M87_TOOLBAR_NAME_561 DrawTradingToolbarM85Legacy
#define M87_TOOLBAR_NAME_2381 DrawTradingToolbarM87
#define DrawTradingToolbarM85 M87_JOIN(M87_TOOLBAR_NAME_, __LINE__)
#include "shell_main_m86.cpp"
#undef DrawTradingToolbarM85
#undef M87_TOOLBAR_NAME_2381
#undef M87_TOOLBAR_NAME_561
#undef M87_TOOLBAR_NAME_3
#undef M87_JOIN
#undef M87_JOIN_INNER

namespace
{
    int g_m87OrderType = 0;
    int g_m87OrderPrice = 0;
    std::string g_m87OrderPriceSymbol;

    void SyncOrderPrice(
        const trading::app::MarketDataSnapshot& market,
        const std::string& selectedCode,
        bool quoteReady)
    {
        if (selectedCode == g_m87OrderPriceSymbol) return;
        g_m87OrderPriceSymbol = selectedCode;
        g_m87OrderPrice = quoteReady ? market.latestBar.close : 0;
    }

    bool SubmitQuickOrder(
        trading::StockOrderSide side,
        const trading::app::MarketDataSnapshot& market,
        const std::string& selectedCode,
        bool quoteReady)
    {
        if (selectedCode.empty() || !IsSelectableSymbolCode(selectedCode)) {
            g_orderLog.Add("REJECT", "주문 거부: 유효한 종목코드가 없습니다.");
            return false;
        }
        if (side == trading::StockOrderSide::Buy) {
            if (!CanSubmitEntryOrders() || !quoteReady) {
                g_orderLog.Add(
                    "REJECT",
                    "매수 거부: 계좌대조, 실제 시세, 진입 허용 상태를 확인하세요.");
                return false;
            }
        }
        else if (!CanSubmitBrokerOrders()) {
            g_orderLog.Add(
                "REJECT",
                "매도 거부: 계좌대조와 주문 가능 상태를 확인하세요.");
            return false;
        }

        const bool limitOrder = g_m87OrderType == 1;
        if (limitOrder && g_m87OrderPrice <= 0) {
            g_orderLog.Add("REJECT", "지정가 주문 거부: 주문가격을 입력하세요.");
            return false;
        }

        trading::SymbolCatalogEntry entry;
        const bool hasEntry = TryResolveSymbolEntry(selectedCode, entry);

        trading::OrderIntent intent;
        intent.code = selectedCode;
        intent.name = hasEntry && !entry.name.empty() ? entry.name : selectedCode;
        intent.side = side;
        intent.type = limitOrder
            ? trading::StockOrderType::Limit
            : trading::StockOrderType::Market;
        intent.quantity = (std::max)(1, g_orderQuantity);
        intent.limitPriceWon = limitOrder ? g_m87OrderPrice : 0;

        std::string error;
        if (!g_runtimeRunner || !g_runtimeRunner->SubmitOrder(intent, error)) {
            g_orderLog.Add(
                "REJECT",
                "%s 주문 거부: %s",
                side == trading::StockOrderSide::Buy ? "매수" : "매도",
                error.c_str());
            return false;
        }

        g_orderLog.Add(
            "ORDER",
            "%s 주문 전송 %s %d주 %s%s",
            side == trading::StockOrderSide::Buy ? "매수" : "매도",
            selectedCode.c_str(),
            intent.quantity,
            limitOrder ? "지정가 " : "시장가",
            limitOrder ? FormatInteger(g_m87OrderPrice).c_str() : "");
        return true;
    }

    void DrawToolbarMenuM87()
    {
        if (ImGui::Button("툴바")) {
            ImGui::OpenPopup("toolbar_visibility_menu_m87");
        }
        if (ImGui::BeginPopup("toolbar_visibility_menu_m87")) {
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

    void DrawInstrumentRowM87(
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

        trading::SymbolCatalogEntry entry;
        const bool hasEntry = TryResolveSymbolEntry(selectedCode, entry);
        ImGui::SameLine();
        if (hasEntry) {
            ImGui::Text("%s", entry.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", MarketDisplayName(entry.market));
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
        const double rate = market.latestBar.open != 0
            ? static_cast<double>(change) /
                static_cast<double>(market.latestBar.open) * 100.0
            : 0.0;
        const ImVec4 color = PriceDirectionColor(change);
        ImGui::TextColored(color, "%s원", FormatInteger(current).c_str());
        ImGui::SameLine();
        ImGui::TextColored(
            color,
            "현재봉 %s%s원 (%+.2f%%)",
            change > 0 ? "+" : "",
            FormatInteger(change).c_str(),
            rate);
        ImGui::SameLine();
        ImGui::TextDisabled(
            "O %s H %s L %s C %s V %s %zu봉",
            FormatInteger(market.latestBar.open).c_str(),
            FormatInteger(market.latestBar.high).c_str(),
            FormatInteger(market.latestBar.low).c_str(),
            FormatInteger(market.latestBar.close).c_str(),
            FormatInteger(static_cast<long long>(market.latestBar.volume)).c_str(),
            market.barCount);
    }

    void DrawTradingRowM87(
        const trading::app::MarketDataSnapshot& market,
        const trading::KiwoomRuntimeSnapshot& runtime,
        const std::string& selectedCode,
        bool quoteReady,
        bool socketUp,
        bool& liquidateAll)
    {
        SyncOrderPrice(market, selectedCode, quoteReady);

        ImGui::TextDisabled("매매");
        ImGui::SameLine();
        ImGui::TextDisabled("수량");
        ImGui::SameLine();
        if (ImGui::SmallButton("-##qty_m87")) {
            g_orderQuantity = (std::max)(1, g_orderQuantity - 1);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(58.0f);
        if (ImGui::InputInt("##quantity_m87", &g_orderQuantity, 0, 0)) {
            g_orderQuantity = (std::max)(1, g_orderQuantity);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+##qty_m87")) ++g_orderQuantity;

        ImGui::SameLine();
        const char* orderTypes[] = {"시장가", "지정가"};
        ImGui::SetNextItemWidth(76.0f);
        ImGui::Combo(
            "##order_type_m87",
            &g_m87OrderType,
            orderTypes,
            IM_ARRAYSIZE(orderTypes));

        ImGui::SameLine();
        ImGui::TextDisabled("가격");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(92.0f);
        if (ImGui::InputInt("##order_price_m87", &g_m87OrderPrice, 0, 0)) {
            g_m87OrderPrice = (std::max)(0, g_m87OrderPrice);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("현재가##price_m87") && quoteReady) {
            g_m87OrderPrice = market.latestBar.close;
        }

        ImGui::SameLine();
        const bool canBuy = CanSubmitEntryOrders() && quoteReady;
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(0.68f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(0.86f, 0.18f, 0.18f, 1.0f));
        if (!canBuy) ImGui::BeginDisabled();
        if (ImGui::Button("매수")) {
            SubmitQuickOrder(
                trading::StockOrderSide::Buy,
                market,
                selectedCode,
                quoteReady);
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
        const bool canSell = CanSubmitBrokerOrders() && hasPosition;
        if (!canSell) ImGui::BeginDisabled();
        if (ImGui::Button("매도")) {
            SubmitQuickOrder(
                trading::StockOrderSide::Sell,
                market,
                selectedCode,
                quoteReady);
        }
        ImGui::SameLine();
        if (ImGui::Button("전량매도")) {
            g_commandBus.Push(Cmd::LiquidatePosition, selectedCode);
        }
        if (!canSell) ImGui::EndDisabled();
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        if (hasPosition) {
            ImGui::TextDisabled(
                "보유 %d주 평단 %.0f 손익 %+.0f (%+.2f%%)",
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
            if (ImGui::Button("관망")) {
                g_commandBus.Push(Cmd::ArmStrategy);
            }
            if (!canActivate) ImGui::EndDisabled();
        }
        else if (ImGui::Button("진입허용")) {
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
            socketUp ? "WS 연결" : "WS 끊김");
        ImGui::SameLine();
        ImGui::TextColored(
            runtime.orderSubmissionAllowed
                ? ImVec4(0.35f, 0.95f, 0.45f, 1.0f)
                : ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
            runtime.orderSubmissionAllowed ? "주문가능" : "주문잠금");

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

    void DrawChartRowM87(
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
        if (ImGui::InputInt("##visible_bars_m87", &g_m85VisibleBars, 0, 0)) {
            g_m85VisibleBars = (std::max)(12, (std::min)(2000, g_m85VisibleBars));
        }
        ImGui::SameLine();
        if (ImGui::Button("봉 적용")) ApplyVisibleBarCount(false);

        ImGui::SameLine();
        ImGui::TextDisabled("| 기준일");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(105.0f);
        ImGui::InputText(
            "##anchor_date_m87",
            g_m85AnchorDate,
            sizeof(g_m85AnchorDate));

        const bool hasChartData = !g_mainRenderSurface.timeAxis.Empty();
        ImGui::SameLine();
        if (!hasChartData) ImGui::BeginDisabled();
        if (ImGui::SmallButton("<##date_m87")) {
            std::string error;
            if (!ShiftLoadedTradingDate(-1, error)) {
                g_log.Add("REJECT", "%s", error.c_str());
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("이동##date_m87")) {
            std::string error;
            if (!MoveViewportToDate(g_m85AnchorDate, error)) {
                g_log.Add("REJECT", "%s", error.c_str());
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("오늘/최신##date_m87")) {
            SetAnchorDateToLatest(market);
            ApplyVisibleBarCount(true);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(">##date_m87")) {
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
        if (ImGui::Button("재조회")) g_commandBus.Push(Cmd::ResetFeed);
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
                    g_log.Add(
                        "FAULT",
                        "추가 과거데이터 요청 실패: %s",
                        error.c_str());
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

static void DrawTradingToolbarM87()
{
    ImGui::PushID("compact_configurable_trading_toolbar");
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(5.0f, 2.0f));
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(4.0f, 2.0f));

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

    DrawToolbarMenuM87();
    if (g_m85ShowInstrumentRow) {
        ImGui::SameLine();
        DrawInstrumentRowM87(market, selectedCode, quoteReady);
    }

    bool liquidateAll = false;
    if (g_m85ShowTradingRow) {
        DrawTradingRowM87(
            market,
            runtime,
            selectedCode,
            quoteReady,
            socketUp,
            liquidateAll);
    }
    if (g_m85ShowChartRow) {
        DrawChartRowM87(market, selectedCode);
    }

    if (!g_m85ShowInstrumentRow &&
        !g_m85ShowTradingRow &&
        !g_m85ShowChartRow)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("툴바 행이 모두 숨겨졌습니다.");
    }

    ImGui::PopStyleVar(2);
    ImGui::PopID();

    if (liquidateAll) ImGui::OpenPopup("confirm_liquidate_all_m87");
    if (ImGui::BeginPopupModal(
            "confirm_liquidate_all_m87",
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
