from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHELL = ROOT / "shell_main.cpp"
RUN_ALL = ROOT / "tests" / "run_all.bat"
WORKFLOW = ROOT / ".github" / "workflows" / "windows-ci.yml"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


def write(path: Path, text: str) -> None:
    path.write_text("\ufeff" + text, encoding="utf-8")


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
    first = text.find(start)
    if first < 0:
        raise RuntimeError(f"{label}: start marker not found")
    last = text.find(end, first + len(start))
    if last < 0:
        raise RuntimeError(f"{label}: end marker not found")
    return text[:first] + replacement + text[last:]


shell = read(SHELL)

shell = replace_once(
    shell,
    '#include "platform/winhttp_kiwoom_transport.h"\n',
    '#include "platform/winhttp_kiwoom_transport.h"\n'
    '#include "app/feature_registry.h"\n'
    '#include "app/market_data_module.h"\n'
    '#include "render/market_chart_builder.h"\n'
    '#include "ui/render_document_renderer.h"\n',
    "modular includes",
)

shell = replace_once(
    shell,
    '// CPPCHART_REAL_DATA_ONLY\n',
    '// CPPCHART_REAL_DATA_ONLY\n'
    '// CPPCHART_MAJOR_FEATURE_MODULES\n'
    '// CPPCHART_GENERIC_RENDER_DOCUMENT\n',
    "architecture markers",
)

shell = replace_between(
    shell,
    "struct MarketDataView final",
    "static int MinuteUnitFromSelection",
    '''static trading::app::FeatureRegistry g_featureRegistry;
static trading::app::MarketDataModule g_marketDataModule;
static trading::render::RenderDocument g_mainRenderDocument;
static trading::ui::RenderSurfaceState g_mainRenderSurface;
static std::size_t g_mainRenderVisibleLimit = 0;

static int MinuteUnitFromSelection''',
    "replace market-data globals and helpers",
)

shell = replace_between(
    shell,
    "static bool TryGetLatestMarketQuote(",
    "static const char* KiwoomSessionStateLabel(",
    '''static bool TryGetLatestMarketQuote(
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
    if (!g_featureRegistry.SetLevel(id, level, error)) return false;
    if (id == "market-data") {
        if (!g_marketDataModule.SetLevel(level, error)) return false;
    }
    return true;
}

static void RecordFeatureWork(
    const std::string& id,
    std::uint64_t elapsedMicros,
    std::size_t renderSeriesCount = 0,
    std::uint64_t mergedEvents = 0,
    std::uint64_t droppedEvents = 0)
{
    const trading::app::MarketDataSnapshot market =
        g_marketDataModule.Snapshot();
    std::string ignored;
    g_featureRegistry.RecordWork(
        id,
        elapsedMicros,
        0,
        market.retainedBytes,
        market.code.empty() ? 0 : 1,
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

static const char* KiwoomSessionStateLabel(''',
    "module helper functions",
)

shell = replace_once(
    shell,
    '''static bool CanSubmitBrokerOrders()
{
    return
        g_runtimeRunner &&
        g_runtimeRunner->Snapshot().orderSubmissionAllowed;
}
''',
    '''static bool CanSubmitBrokerOrders()
{
    return
        FeatureAtLeast("trading", trading::app::FeatureLevel::Active) &&
        g_runtimeRunner &&
        g_runtimeRunner->Snapshot().orderSubmissionAllowed;
}
''',
    "trading feature gate",
)

shell = replace_once(
    shell,
    '''static bool CanActivateEntries()
{
    return
        CanSubmitBrokerOrders() &&
        g_marketDataState.load(std::memory_order_acquire) ==
            MarketDataState::Ready;
}
''',
    '''static bool CanActivateEntries()
{
    return
        CanSubmitBrokerOrders() &&
        FeatureAtLeast("market-data", trading::app::FeatureLevel::Visible) &&
        g_marketDataModule.Snapshot().state ==
            trading::app::MarketDataState::Ready;
}
''',
    "market feature gate",
)

old_toolbar_status = '''    const MarketDataState marketState =
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
'''
new_toolbar_status = '''    const trading::app::MarketDataSnapshot market =
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
'''
shell = replace_once(
    shell,
    old_toolbar_status,
    new_toolbar_status,
    "toolbar module status",
)

shell = replace_between(
    shell,
    "static void DrawRealCandles(",
    "static void DrawSymbolPool()",
    '''static void DrawMarketDataPanel()
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
    const std::size_t visibleLimit = static_cast<std::size_t>((std::max)(
        20,
        static_cast<int>(available.x / 7.0f)));

    const double started = NowSeconds();
    if (
        g_mainRenderDocument.revision != snapshot.revision ||
        g_mainRenderVisibleLimit != visibleLimit)
    {
        const std::vector<trading::Bar> visibleBars =
            g_marketDataModule.CopyVisibleBars(visibleLimit);
        g_mainRenderDocument = trading::render::BuildMarketChartDocument(
            "main-market-chart",
            snapshot.code,
            snapshot.code,
            visibleBars,
            snapshot.revision);
        std::string renderError;
        if (!trading::render::ValidateRenderDocument(
                g_mainRenderDocument,
                renderError))
        {
            g_marketDataModule.SetError(
                "렌더 문서 검증 실패: " + renderError);
            g_log.Add("FAULT", "렌더 문서 검증 실패: %s", renderError.c_str());
            ImGui::End();
            return;
        }
        g_mainRenderVisibleLimit = visibleLimit;
        g_mainRenderSurface.dirty = true;
    }

    trading::ui::DrawRenderDocument(
        g_mainRenderDocument,
        available,
        g_mainRenderSurface);
    const std::uint64_t elapsedMicros = static_cast<std::uint64_t>(
        (NowSeconds() - started) * 1000000.0);
    RecordFeatureWork("chart-workspace", elapsedMicros, 2);
    ImGui::End();
}

static void DrawSymbolPool()''',
    "generic market chart panel",
)

shell = replace_between(
    shell,
    "static void DrawSymbolPool()",
    "static void DrawScanner()",
    '''static void DrawSymbolPool()
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

static void DrawScanner()''',
    "module symbol pool",
)

shell = replace_between(
    shell,
    "static void DrawScanner()",
    "static void DrawDashboard()",
    '''static void DrawScanner()
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

static void DrawDashboard()''',
    "module scanner",
)

shell = replace_once(
    shell,
    '''    const MarketDataView market = MarketDataSnapshot();
    const bool quoteReady = !market.code.empty() && !market.bars.empty();
    const trading::PriceWon latestPrice = quoteReady
        ? market.bars.back().close
        : 0;
''',
    '''    const trading::app::MarketDataSnapshot market =
        g_marketDataModule.Snapshot();
    const bool quoteReady =
        market.state == trading::app::MarketDataState::Ready &&
        market.hasLatestBar;
    const trading::PriceWon latestPrice = quoteReady
        ? market.latestBar.close
        : 0;
''',
    "dashboard market snapshot",
)

shell = replace_once(
    shell,
    '''                SetMarketDataError(
                    "종목코드가 비어 있어 실제 시세 조회를 시작할 수 없습니다.");
                g_log.Add("DATA", "실시세 조회 거부: 종목코드 없음");
                break;
''',
    '''                g_marketDataModule.SetError(
                    "종목코드가 비어 있어 실제 시세 조회를 시작할 수 없습니다.");
                g_log.Add("DATA", "실시세 조회 거부: 종목코드 없음");
                break;
''',
    "empty code error",
)

shell = replace_once(
    shell,
    '''                SetMarketDataError("키움 런타임이 실행 중이 아닙니다.");
                g_log.Add("DATA", "실시세 조회 거부: 키움 런타임 정지");
                break;
''',
    '''                g_marketDataModule.SetError("키움 런타임이 실행 중이 아닙니다.");
                g_log.Add("DATA", "실시세 조회 거부: 키움 런타임 정지");
                break;
''',
    "runtime stopped error",
)

shell = replace_once(
    shell,
    '''            const int minuteUnit = MinuteUnitFromSelection(command.i0);
            BeginMarketDataRequest(command.arg, minuteUnit);
            std::string error;
            if (!g_runtimeRunner->RequestStockMinuteBars(
''',
    '''            const int minuteUnit = MinuteUnitFromSelection(command.i0);
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
''',
    "begin module request",
)

shell = shell.replace(
    "                SetMarketDataError(error);",
    "                g_marketDataModule.SetError(error);",
)

old_reset = '''        case Cmd::ResetFeed: {
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
'''
new_reset = '''        case Cmd::ResetFeed: {
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
'''
shell = replace_once(shell, old_reset, new_reset, "reset module feed")

feature_window = r'''static void DrawFeatureWindow()
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
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::Combo(
                    "##level",
                    &selectedLevel,
                    levels,
                    IM_ARRAYSIZE(levels)))
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

'''
shell = replace_once(
    shell,
    "static void DrawFaultWindow()\n",
    feature_window + "static void DrawFaultWindow()\n",
    "feature diagnostics window",
)

shell = replace_once(
    shell,
    '    ImGui::DockBuilderDockWindow("결함", bottomLogs);\n',
    '    ImGui::DockBuilderDockWindow("결함", bottomLogs);\n'
    '    ImGui::DockBuilderDockWindow("기능/성능", right);\n',
    "dock feature window",
)

shell = replace_once(
    shell,
    '''    g_log.Add(
        "DATA",
        "%s",
        MarketDataErrorSnapshot().c_str());
''',
    '''    std::string featureError;
    if (!InitializeFeatureRegistry(featureError)) {
        g_runtimeConfigError = "기능 레지스트리 초기화 실패: " + featureError;
        g_observeMode.store(true, std::memory_order_release);
    }

    g_log.Add(
        "DATA",
        "%s",
        g_marketDataModule.Snapshot().error.c_str());
''',
    "initialize feature registry",
)

old_minute_callback = '''        callbacks.minuteBars = [](
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
'''
new_minute_callback = '''        callbacks.minuteBars = [](
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
'''
shell = replace_once(
    shell,
    old_minute_callback,
    new_minute_callback,
    "module minute callback",
)

shell = replace_once(
    shell,
    '''        callbacks.stockTrade = [](
            const trading::StockTradeTick& tick) {
            ApplyStockTradeTick(tick);
        };
''',
    '''        callbacks.stockTrade = [](
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
''',
    "module stock trade callback",
)

shell = replace_once(
    shell,
    '''        DrawSymbolPool();
        DrawMarketDataPanel();
        DrawScanner();
        DrawDashboard();
        DrawLogWindow("로그", g_log);
        DrawLogWindow("신호", g_signalLog);
        DrawLogWindow("주문/체결", g_orderLog);
        DrawFaultWindow();
''',
    '''        DrawSymbolPool();
        if (FeatureAtLeast(
                "chart-workspace",
                trading::app::FeatureLevel::Visible))
        {
            DrawMarketDataPanel();
        }
        DrawScanner();
        DrawDashboard();
        DrawLogWindow("로그", g_log);
        DrawLogWindow("신호", g_signalLog);
        DrawLogWindow("주문/체결", g_orderLog);
        DrawFaultWindow();
        DrawFeatureWindow();
''',
    "feature-aware window draw",
)

for forbidden in (
    "struct MarketDataView final",
    "static std::mutex g_marketDataMutex",
    "static void ApplyStockTradeTick(",
    "static void ApplyMinuteBars(",
    "static void DrawRealCandles(",
    "g_marketDataState",
    "g_stockTradeTickCount",
    "g_lastStockTradeTimestampMs",
    "g_stockTradeSubscriptionRequested",
    "MarketDataSnapshot()",
    "MarketDataErrorSnapshot()",
    "BeginMarketDataRequest(",
):
    if forbidden in shell:
        raise RuntimeError(f"legacy shell marker remains: {forbidden}")

write(SHELL, shell)

run_all = read(RUN_ALL)
new_tests = r'''
cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\feature_registry_tests.cpp ^
  app\feature_registry.cpp ^
  /Fe:feature_registry_tests.exe
if errorlevel 1 exit /b 1
feature_registry_tests.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\render_document_tests.cpp ^
  render\render_document.cpp ^
  /Fe:render_document_tests.exe
if errorlevel 1 exit /b 1
render_document_tests.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\market_data_module_tests.cpp ^
  core\json_lite.cpp ^
  core\kiwoom_market_data.cpp ^
  app\market_data_module.cpp ^
  /Fe:market_data_module_tests.exe
if errorlevel 1 exit /b 1
market_data_module_tests.exe
if errorlevel 1 exit /b 1

'''
anchor = "cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^\n  tests\\kiwoom_protocol_tests.cpp ^\n"
if anchor not in run_all:
    raise RuntimeError("run_all insertion anchor not found")
run_all = run_all.replace(anchor, new_tests + anchor, 1)
write(RUN_ALL, run_all)

workflow = read(WORKFLOW)
anchor = "      - name: Verify real-data-only production runtime\n        shell: pwsh\n        run: .\\scripts\\verify_real_data_only.ps1\n"
addition = anchor + "\n      - name: Verify modular architecture\n        shell: pwsh\n        run: .\\scripts\\verify_modular_architecture.ps1\n"
if "Verify modular architecture" not in workflow:
    workflow = replace_once(
        workflow,
        anchor,
        addition,
        "modular CI gate",
    )
write(WORKFLOW, workflow)

print("Applied major-feature module and generic renderer refactor")
