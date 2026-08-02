from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHELL = ROOT / "shell_main.cpp"
WINHTTP = ROOT / "platform" / "winhttp_transport.cpp"


def replace_exact(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


shell = SHELL.read_text(encoding="utf-8-sig")

if "CPPCHART_KIWOOM_RUNTIME_CONNECTED" not in shell:
    shell = replace_exact(
        shell,
        '#include <utility>\n',
        '#include <utility>\n#include <memory>\n',
        "memory include",
    )

    shell = replace_exact(
        shell,
        '#include "core/trading_state.h"\n',
        '#include "core/trading_state.h"\n'
        '#include "core/order_coordinator.h"\n'
        '#include "core/kiwoom_gateway_core.h"\n'
        '#include "core/safe_liquidation.h"\n'
        '#include "core/kiwoom_runtime_engine.h"\n'
        '#include "platform/kiwoom_runtime_runner.h"\n'
        '#include "platform/winhttp_kiwoom_transport.h"\n',
        "runtime includes",
    )

    shell = replace_exact(
        shell,
        '// CPPCHART_UI_THREAD_DATA_HANDOFF\n',
        '// CPPCHART_UI_THREAD_DATA_HANDOFF\n'
        '// CPPCHART_KIWOOM_RUNTIME_CONNECTED\n',
        "runtime marker",
    )

    shell = replace_exact(
        shell,
        '''static trading::TradingState g_tradingState;
static trading::RuntimeConfig g_runtimeConfig;
''',
        '''static trading::TradingState g_tradingState;
static trading::OrderCoordinator g_orderCoordinator(g_tradingState);
static trading::KiwoomGatewayCore g_kiwoomGatewayCore(
    g_tradingState,
    g_orderCoordinator);
static trading::BrokerOpenOrderRegistry g_brokerOpenOrders;
static trading::KiwoomRuntimeEngine g_kiwoomRuntimeEngine(
    g_tradingState,
    g_orderCoordinator,
    g_kiwoomGatewayCore,
    g_brokerOpenOrders);
static std::unique_ptr<trading::platform::KiwoomRuntimeRunner> g_kiwoomRunner;
static trading::RuntimeConfig g_runtimeConfig;
''',
        "runtime globals",
    )

    shell = replace_exact(
        shell,
        '''static const char* RuntimeModeLabel() noexcept
{
    return IsLocalMock() ? "LOCAL MOCK" : "KIWOOM MOCK";
}

''',
        '''static const char* RuntimeModeLabel() noexcept
{
    return IsLocalMock() ? "LOCAL MOCK" : "KIWOOM MOCK";
}

static const char* KiwoomSessionLabel(
    trading::KiwoomSessionState state) noexcept
{
    switch (state) {
    case trading::KiwoomSessionState::TokenRequestPending: return "토큰";
    case trading::KiwoomSessionState::SocketConnectPending: return "WS 연결";
    case trading::KiwoomSessionState::LoginPending: return "로그인";
    case trading::KiwoomSessionState::RegistrationPending: return "실시간 등록";
    case trading::KiwoomSessionState::ReconciliationPending: return "계좌 대조";
    case trading::KiwoomSessionState::Ready: return "주문 가능";
    case trading::KiwoomSessionState::ReconnectWaiting: return "재연결 대기";
    case trading::KiwoomSessionState::Faulted: return "장애";
    case trading::KiwoomSessionState::ConfigurationError: return "설정 오류";
    default: return "정지";
    }
}

static trading::KiwoomRuntimeSnapshot KiwoomSnapshot()
{
    return g_kiwoomRunner
        ? g_kiwoomRunner->Snapshot()
        : trading::KiwoomRuntimeSnapshot{};
}

static bool CanSubmitOrders()
{
    return
        IsLocalMock() ||
        (g_kiwoomRunner &&
         g_kiwoomRunner->Snapshot().orderSubmissionAllowed &&
         !g_observeMode.load(std::memory_order_acquire));
}

''',
        "runtime status helpers",
    )

    shell = replace_exact(
        shell,
        '''    const bool ws = localMock ? true : g_health.wsUp.load();
    ImGui::TextColored(ws ? ImVec4(0.3f, 0.9f, 0.4f, 1) : ImVec4(0.95f, 0.3f, 0.3f, 1), ws ? "WS●" : "WS○");
    ImGui::SameLine();
    ImGui::Text("지연 %dms   유량 %d/%d   구독 %d   부팅 %.0fms   %.1ffps",
        g_health.latencyMs.load(), g_health.rateUsed.load(), g_health.rateCap.load(),
        g_health.subCount.load(), g_health.bootMs.load(), ImGui::GetIO().Framerate);
''',
        '''    const trading::KiwoomRuntimeSnapshot runtime = KiwoomSnapshot();
    const bool ws = localMock ||
        runtime.sessionState == trading::KiwoomSessionState::LoginPending ||
        runtime.sessionState == trading::KiwoomSessionState::RegistrationPending ||
        runtime.sessionState == trading::KiwoomSessionState::ReconciliationPending ||
        runtime.sessionState == trading::KiwoomSessionState::Ready;
    ImGui::TextColored(
        ws ? ImVec4(0.3f, 0.9f, 0.4f, 1) : ImVec4(0.95f, 0.3f, 0.3f, 1),
        ws ? "WS●" : "WS○");
    ImGui::SameLine();
    if (!localMock) {
        ImGui::TextColored(
            runtime.orderSubmissionAllowed
                ? ImVec4(0.35f, 0.95f, 0.45f, 1.0f)
                : ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
            "%s | ",
            KiwoomSessionLabel(runtime.sessionState));
        ImGui::SameLine();
    }
    ImGui::Text("지연 %dms   유량 %d/%d   구독 %d   부팅 %.0fms   %.1ffps",
        g_health.latencyMs.load(), g_health.rateUsed.load(), g_health.rateCap.load(),
        g_health.subCount.load(), g_health.bootMs.load(), ImGui::GetIO().Framerate);
''',
        "toolbar runtime status",
    )

    shell = replace_exact(
        shell,
        '''    bool panic = ImGui::Button("전량청산", ImVec2(btnW, 0));
    ImGui::PopStyleColor(2);
''',
        '''    const bool canLiquidate = CanSubmitOrders();
    if (!canLiquidate) ImGui::BeginDisabled();
    bool panic = ImGui::Button("전량청산", ImVec2(btnW, 0));
    if (!canLiquidate) ImGui::EndDisabled();
    ImGui::PopStyleColor(2);
''',
        "panic readiness gate",
    )

    shell = replace_exact(
        shell,
        '''    ImGui::SameLine();
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
        if (ImGui::Button("선택 청산")) g_bus.Push(Cmd::LiquidateSelected);
    }
    else {
        ImGui::BeginDisabled();
        ImGui::Button("선택 청산 (연결 대기)");
        ImGui::EndDisabled();
    }
''',
        '''    ImGui::SameLine();
    const bool canSubmit = CanSubmitOrders();
    if (!canSubmit) ImGui::BeginDisabled();
    if (ImGui::Button(IsLocalMock() ? "모의매수" : "키움 모의매수")) {
        g_bus.Push(Cmd::MockBuy, selectedSeries.code, g_mockOrderQty);
    }
    if (!canSubmit) ImGui::EndDisabled();

    ImGui::SameLine();
    if (!canSubmit) ImGui::BeginDisabled();
    if (ImGui::Button("선택 청산")) g_bus.Push(Cmd::LiquidateSelected);
    if (!canSubmit) ImGui::EndDisabled();
''',
        "dashboard order readiness",
    )

    shell = replace_exact(
        shell,
        '''            if (IsLocalMock()) {
                if (ImGui::SmallButton("개별청산")) {
                    g_bus.Push(Cmd::LiquidatePosition, position.code);
                }
            }
            else {
                ImGui::BeginDisabled();
                ImGui::SmallButton("연결 대기");
                ImGui::EndDisabled();
            }
''',
        '''            const bool canClosePosition = CanSubmitOrders();
            if (!canClosePosition) ImGui::BeginDisabled();
            if (ImGui::SmallButton("개별청산")) {
                g_bus.Push(Cmd::LiquidatePosition, position.code);
            }
            if (!canClosePosition) ImGui::EndDisabled();
''',
        "individual liquidation readiness",
    )

    shell = replace_exact(
        shell,
        '''        case Cmd::MockBuy: {
            if (!IsLocalMock()) {
                g_orderLog.Add("REJECT", "키움 모의투자 연결 완료 전에는 주문할 수 없습니다.");
                break;
            }

            const trading::Quantity orderQuantity = (std::max)(1, command.i0);
            std::string name;
            trading::PriceWon price = 0;
''',
        '''        case Cmd::MockBuy: {
            const trading::Quantity orderQuantity = (std::max)(1, command.i0);
            std::string name;
            trading::PriceWon price = 0;
            if (!TryGetLatestQuote(command.arg, name, price)) {
                g_orderLog.Add(
                    "REJECT",
                    "매수 거부: 종목 데이터 없음 %s",
                    command.arg.c_str());
                break;
            }

            if (!IsLocalMock()) {
                trading::OrderIntent intent;
                intent.code = command.arg;
                intent.name = name;
                intent.side = trading::StockOrderSide::Buy;
                intent.type = trading::StockOrderType::Market;
                intent.quantity = orderQuantity;

                std::string error;
                if (!g_kiwoomRunner || !g_kiwoomRunner->SubmitOrder(intent, error)) {
                    g_orderLog.Add("REJECT", "키움 모의매수 거부: %s", error.c_str());
                }
                else {
                    g_orderLog.Add(
                        "ORDER",
                        "키움 모의매수 전송 %s %s %d주 시장가",
                        command.arg.c_str(),
                        name.c_str(),
                        orderQuantity);
                }
                break;
            }
''',
        "Kiwoom buy command",
    )

    duplicate_quote = '''            if (!TryGetLatestQuote(command.arg, name, price)) {
                g_orderLog.Add(
                    "REJECT",
                    "모의매수 거부: 종목 데이터 없음 %s",
                    command.arg.c_str());
                break;
            }

'''
    if shell.count(duplicate_quote) != 1:
        raise RuntimeError("local duplicate quote block was not found")
    shell = shell.replace(duplicate_quote, "", 1)

    shell = replace_exact(
        shell,
        '''        case Cmd::LiquidatePosition: {
            if (!IsLocalMock()) {
                g_orderLog.Add("REJECT", "키움 모의투자 연결 완료 전에는 청산할 수 없습니다.");
                break;
            }

            trading::PositionSnapshot position;
            if (!FindPosition(command.arg, position)) {
''',
        '''        case Cmd::LiquidatePosition: {
            trading::PositionSnapshot position;
            if (!FindPosition(command.arg, position)) {
''',
        "individual command prefix",
    )

    shell = replace_exact(
        shell,
        '''                break;
            }

            const trading::ApplyFillResult result = ApplyLocalFill(
                position.code,
''',
        '''                break;
            }

            if (!IsLocalMock()) {
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
                    g_orderLog.Add(
                        "ORDER",
                        "개별청산 주문 전송 %s %d주 시장가",
                        position.code.c_str(),
                        position.quantity);
                }
                break;
            }

            const trading::ApplyFillResult result = ApplyLocalFill(
                position.code,
''',
        "individual Kiwoom submission",
    )

    shell = replace_exact(
        shell,
        '''        case Cmd::LiquidateSelected:
        case Cmd::LiquidateAll: {
            if (!IsLocalMock()) {
                g_orderLog.Add("REJECT", "키움 모의투자 연결 완료 전에는 청산할 수 없습니다.");
                break;
            }

            const bool selectedOnly = command.type == Cmd::LiquidateSelected;
''',
        '''        case Cmd::LiquidateSelected:
        case Cmd::LiquidateAll: {
            const bool selectedOnly = command.type == Cmd::LiquidateSelected;
            if (!IsLocalMock()) {
                std::string error;
                if (!g_kiwoomRunner ||
                    !g_kiwoomRunner->SubmitLiquidation(selectedOnly, error))
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

''',
        "Kiwoom grouped liquidation",
    )

    shell = replace_exact(
        shell,
        '''    else if (!IsLocalMock()) {
        g_log.Add("SYS", "KIWOOM MOCK 선택됨: 연결 런타임 준비 전까지 주문 잠금");
    }

    std::thread feed(MockFeedThread);
''',
        '''    else if (!IsLocalMock()) {
        trading::platform::KiwoomRunnerCallbacks callbacks;
        callbacks.log = [](const char* category, const std::string& message) {
            if (
                strcmp(category, "ORDER") == 0 ||
                strcmp(category, "REJECT") == 0)
            {
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

        g_kiwoomRunner =
            std::make_unique<trading::platform::KiwoomRuntimeRunner>(
                g_kiwoomRuntimeEngine,
                std::make_unique<trading::platform::WinHttpKiwoomTransport>(),
                std::move(callbacks));

        std::string runtimeError;
        if (!g_kiwoomRunner->Start(g_runtimeConfig, runtimeError)) {
            g_observeMode = true;
            g_log.Add(
                "FAULT",
                "키움 모의투자 런타임 시작 실패: %s",
                runtimeError.c_str());
        }
        else {
            g_log.Add(
                "SYS",
                "키움 모의투자 연결 시작: 토큰 → WS → 00/04 → 계좌대조");
        }
    }

    std::thread feed(MockFeedThread);
''',
        "runtime startup",
    )

    shell = replace_exact(
        shell,
        '''    g_feedRun = false; feed.join();
    g_mainCanvas.Release(); for (auto& c : g_multi) c.Release();
''',
        '''    if (g_kiwoomRunner) {
        g_kiwoomRunner->Stop();
        g_kiwoomRunner.reset();
    }
    g_feedRun = false; feed.join();
    g_mainCanvas.Release(); for (auto& c : g_multi) c.Release();
''',
        "runtime shutdown",
    )

    SHELL.write_text("\ufeff" + shell, encoding="utf-8")
    print("Connected Kiwoom runtime to shell UI and CommandBus")
else:
    print("shell runtime is already connected")

winhttp = WINHTTP.read_text(encoding="utf-8-sig")
if "CPPCHART_CONTINUATION_HEADERS" not in winhttp:
    winhttp = replace_exact(
        winhttp,
        '''            response.statusCode = statusCode;

            for (;;) {
''',
        '''            response.statusCode = statusCode;

            // CPPCHART_CONTINUATION_HEADERS
            const auto readResponseHeader = [&](const wchar_t* headerName,
                                                const char* resultName) {
                DWORD bytes = 0;
                WinHttpQueryHeaders(
                    request.Get(),
                    WINHTTP_QUERY_CUSTOM,
                    const_cast<wchar_t*>(headerName),
                    WINHTTP_NO_OUTPUT_BUFFER,
                    &bytes,
                    WINHTTP_NO_HEADER_INDEX);

                if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) {
                    return;
                }

                std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1, L'\\0');
                if (WinHttpQueryHeaders(
                        request.Get(),
                        WINHTTP_QUERY_CUSTOM,
                        const_cast<wchar_t*>(headerName),
                        value.data(),
                        &bytes,
                        WINHTTP_NO_HEADER_INDEX))
                {
                    response.headers[resultName] = WideToUtf8(value.data());
                }
            };

            readResponseHeader(L"cont-yn", "cont-yn");
            readResponseHeader(L"next-key", "next-key");

            for (;;) {
''',
        "WinHTTP continuation headers",
    )
    WINHTTP.write_text("\ufeff" + winhttp, encoding="utf-8")
    print("Added WinHTTP continuation response headers")
else:
    print("WinHTTP continuation headers already integrated")
