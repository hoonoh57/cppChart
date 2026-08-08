#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <cstddef>

namespace ImGui
{
    bool M82InputText(
        const char* label,
        char* buffer,
        std::size_t bufferSize,
        ImGuiInputTextFlags flags = 0,
        ImGuiInputTextCallback callback = nullptr,
        void* userData = nullptr);
}

static void DrawTradingToolbar();
static void DrawTradingMarketDataPanel();
static void DrawTradingDashboard();

#define M84_JOIN_INNER(left, right) left##right
#define M84_JOIN(left, right) M84_JOIN_INNER(left, right)

#define M84_DRAW_TOOLBAR_743() DrawToolbarOriginal()
#define M84_DRAW_TOOLBAR_2381() DrawTradingToolbar()
#define DrawToolbar() M84_JOIN(M84_DRAW_TOOLBAR_, __LINE__)()

#define M84_DRAW_MARKET_PANEL_890() DrawMarketDataPanelOriginal()
#define M84_DRAW_MARKET_PANEL_2399() DrawTradingMarketDataPanel()
#define DrawMarketDataPanel() M84_JOIN(M84_DRAW_MARKET_PANEL_, __LINE__)()

#define M84_DRAW_DASHBOARD_1165() DrawDashboardOriginal()
#define M84_DRAW_DASHBOARD_2419() DrawTradingDashboard()
#define DrawDashboard() M84_JOIN(M84_DRAW_DASHBOARD_, __LINE__)()

#define InputText M82InputText
#include "shell_main.cpp"
#undef InputText
#undef DrawDashboard
#undef DrawMarketDataPanel
#undef DrawToolbar
#undef M84_DRAW_DASHBOARD_2419
#undef M84_DRAW_DASHBOARD_1165
#undef M84_DRAW_MARKET_PANEL_2399
#undef M84_DRAW_MARKET_PANEL_890
#undef M84_DRAW_TOOLBAR_2381
#undef M84_DRAW_TOOLBAR_743
#undef M84_JOIN
#undef M84_JOIN_INNER

#include "core/symbol_master_cache.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    constexpr const char* kSymbolMasterCachePath =
        "data/kiwoom_symbol_master.json";

    std::vector<trading::SymbolCatalogEntry> g_m82ToolbarMatches;
    std::vector<trading::SymbolCatalogEntry> g_m82RecentSymbols;
    int g_m82ToolbarHighlight = -1;
    bool g_m82ToolbarPopupOpen = false;
    bool g_m82MasterInitialized = false;
    bool g_m82RefreshInFlight = false;
    std::size_t g_m82ObservedMasterCount = 0U;
    std::size_t g_m82SavedMasterCount = 0U;
    double g_m82MasterChangedAt = 0.0;
    double g_m82LastRefreshAttemptAt = -1000.0;
    std::string g_m82MasterStatus;
    std::string g_m82MasterError;

    bool IsSixDigitCode(const std::string& value) noexcept
    {
        return value.size() == 6U &&
            std::all_of(value.begin(), value.end(), [](unsigned char ch) {
                return ch >= '0' && ch <= '9';
            });
    }

    bool IsNxtCode(const std::string& value) noexcept
    {
        return value.size() == 9U &&
            std::all_of(value.begin(), value.begin() + 6, [](unsigned char ch) {
                return ch >= '0' && ch <= '9';
            }) &&
            value[6] == '_' &&
            (value[7] == 'A' || value[7] == 'a') &&
            (value[8] == 'L' || value[8] == 'l');
    }

    std::string NormalizeCode(std::string value)
    {
        if (IsNxtCode(value)) {
            value[7] = 'A';
            value[8] = 'L';
        }
        return value;
    }

    void MergeSymbolMaster(
        const std::vector<trading::SymbolCatalogEntry>& entries)
    {
        std::lock_guard<std::mutex> lock(g_symbolCatalogMutex);
        for (const trading::SymbolCatalogEntry& entry : entries) {
            const auto found = std::find_if(
                g_symbolCatalog.begin(),
                g_symbolCatalog.end(),
                [&](const trading::SymbolCatalogEntry& existing) {
                    return existing.code == entry.code;
                });
            if (found == g_symbolCatalog.end()) {
                g_symbolCatalog.push_back(entry);
            }
            else {
                *found = entry;
            }
        }
    }

    std::vector<trading::SymbolCatalogEntry> SymbolMasterSnapshot()
    {
        std::lock_guard<std::mutex> lock(g_symbolCatalogMutex);
        return g_symbolCatalog;
    }

    std::size_t CountMarket(
        const std::vector<trading::SymbolCatalogEntry>& entries,
        const char* market)
    {
        return static_cast<std::size_t>(std::count_if(
            entries.begin(),
            entries.end(),
            [&](const trading::SymbolCatalogEntry& entry) {
                return entry.market == market;
            }));
    }

    void EnsureSymbolMasterLoaded()
    {
        if (g_m82MasterInitialized) return;
        g_m82MasterInitialized = true;

        const trading::SymbolMasterCacheLoadResult cached =
            trading::SymbolMasterCache::Load(kSymbolMasterCachePath);
        if (cached.loaded) {
            MergeSymbolMaster(cached.entries);
            g_m82ObservedMasterCount = cached.entries.size();
            g_m82SavedMasterCount = cached.entries.size();
            const std::size_t kospi = CountMarket(cached.entries, "0");
            const std::size_t kosdaq = CountMarket(cached.entries, "10");
            g_m82MasterStatus = cached.fresh
                ? "종목 마스터 캐시 준비"
                : "종목 마스터 캐시 준비(갱신 필요)";
            g_log.Add("DATA", "KOSPI 종목 마스터 캐시 로드: %zu종목", kospi);
            g_log.Add("DATA", "KOSDAQ 종목 마스터 캐시 로드: %zu종목", kosdaq);
            g_log.Add(
                "DATA",
                "종목 마스터 캐시 로드 합계: %zu종목%s",
                cached.entries.size(),
                cached.fresh ? "" : " (stale)");
        }
        else {
            g_m82MasterStatus = "종목 마스터 최초 다운로드 대기";
            g_m82MasterError = cached.error;
            g_log.Add(
                "DATA",
                "종목 마스터 캐시 미사용: %s",
                cached.error.c_str());
        }
    }

    void RequestSymbolMasterRefresh()
    {
        if (g_m82RefreshInFlight || !g_runtimeRunner ||
            !g_runtimeRunner->IsRunning())
        {
            return;
        }

        const double now = NowSeconds();
        if (now - g_m82LastRefreshAttemptAt < 3.0) return;
        g_m82LastRefreshAttemptAt = now;

        trading::Continuation empty;
        std::string kospiError;
        std::string kosdaqError;
        const bool kospi = g_runtimeRunner->RequestSymbolCatalog(
            "0", empty, kospiError);
        const bool kosdaq = g_runtimeRunner->RequestSymbolCatalog(
            "10", empty, kosdaqError);

        if (!kospi || !kosdaq) {
            g_m82MasterError = !kospiError.empty() ? kospiError : kosdaqError;
            g_m82MasterStatus = "종목 마스터 REST 요청 재시도 대기";
            return;
        }

        g_m82RefreshInFlight = true;
        g_m82MasterChangedAt = now;
        g_m82MasterError.clear();
        g_m82MasterStatus = "KOSPI/KOSDAQ 종목 마스터 다운로드 중";
        g_log.Add("DATA", "KOSPI/KOSDAQ 종목 마스터 REST 다운로드 시작");
    }

    void PersistSymbolMasterWhenStable()
    {
        const std::vector<trading::SymbolCatalogEntry> snapshot =
            SymbolMasterSnapshot();
        const std::size_t count = snapshot.size();
        if (count != g_m82ObservedMasterCount) {
            g_m82ObservedMasterCount = count;
            g_m82MasterChangedAt = NowSeconds();
            return;
        }

        const std::size_t kospi = CountMarket(snapshot, "0");
        const std::size_t kosdaq = CountMarket(snapshot, "10");
        if (!g_m82RefreshInFlight || kospi == 0U || kosdaq == 0U ||
            count == g_m82SavedMasterCount ||
            g_m82MasterChangedAt <= 0.0 ||
            NowSeconds() - g_m82MasterChangedAt < 2.0)
        {
            return;
        }

        std::string error;
        if (!trading::SymbolMasterCache::SaveAtomic(
                kSymbolMasterCachePath,
                snapshot,
                error))
        {
            g_m82MasterError = error;
            g_m82MasterStatus = "종목 마스터 캐시 저장 실패";
            g_log.Add("FAULT", "%s", error.c_str());
            g_m82RefreshInFlight = false;
            g_m82LastRefreshAttemptAt = NowSeconds();
            return;
        }

        g_m82SavedMasterCount = count;
        g_m82MasterChangedAt = 0.0;
        g_m82RefreshInFlight = false;
        g_m82MasterError.clear();
        g_m82MasterStatus = "종목 마스터 준비";
        g_log.Add("DATA", "KOSPI 종목 마스터 수신: %zu종목", kospi);
        g_log.Add("DATA", "KOSDAQ 종목 마스터 수신: %zu종목", kosdaq);
        g_log.Add("DATA", "종목 마스터 캐시 저장 합계: %zu종목", count);
    }

    void AddRecent(const trading::SymbolCatalogEntry& entry)
    {
        if (!IsSixDigitCode(entry.code) && !IsNxtCode(entry.code)) return;
        g_m82RecentSymbols.erase(
            std::remove_if(
                g_m82RecentSymbols.begin(),
                g_m82RecentSymbols.end(),
                [&](const trading::SymbolCatalogEntry& existing) {
                    return existing.code == entry.code;
                }),
            g_m82RecentSymbols.end());
        g_m82RecentSymbols.insert(g_m82RecentSymbols.begin(), entry);
        if (g_m82RecentSymbols.size() > 12U) {
            g_m82RecentSymbols.resize(12U);
        }
    }

    void RefreshToolbarMatches(const char* query)
    {
        const std::vector<trading::SymbolCatalogEntry> master =
            SymbolMasterSnapshot();
        if (query == nullptr || query[0] == '\0') {
            g_m82ToolbarMatches = g_m82RecentSymbols;
        }
        else {
            g_m82ToolbarMatches =
                trading::SearchSymbolCatalog(master, query, 12U);
        }
        g_m82ToolbarPopupOpen = !g_m82ToolbarMatches.empty();
        g_m82ToolbarHighlight = g_m82ToolbarPopupOpen ? 0 : -1;
    }

    bool ApplyToolbarEntry(
        char* buffer,
        std::size_t bufferSize,
        const trading::SymbolCatalogEntry& source)
    {
        if (buffer == nullptr || bufferSize == 0U) return false;

        trading::SymbolCatalogEntry entry = source;
        entry.code = NormalizeCode(entry.code);
        if (!IsSixDigitCode(entry.code) && !IsNxtCode(entry.code)) {
            return false;
        }

        ImGui::ClearActiveID();
        std::snprintf(buffer, bufferSize, "%s", entry.code.c_str());
        AddRecent(entry);
        g_m82ToolbarPopupOpen = false;
        return true;
    }

    bool SelectToolbarMatch(
        char* buffer,
        std::size_t bufferSize,
        int index)
    {
        if (index < 0 ||
            index >= static_cast<int>(g_m82ToolbarMatches.size()))
        {
            return false;
        }

        return ApplyToolbarEntry(
            buffer,
            bufferSize,
            g_m82ToolbarMatches[static_cast<std::size_t>(index)]);
    }

    void RecordDirectCode(const char* buffer)
    {
        if (buffer == nullptr) return;
        const std::string normalized = NormalizeCode(buffer);
        if (!IsSixDigitCode(normalized) && !IsNxtCode(normalized)) return;

        trading::SymbolCatalogEntry entry;
        entry.code = normalized;
        entry.name = normalized;
        entry.market = IsNxtCode(normalized) ? "NXT" : "최근 조회";

        const std::vector<trading::SymbolCatalogEntry> master =
            SymbolMasterSnapshot();
        const auto found = std::find_if(
            master.begin(),
            master.end(),
            [&](const trading::SymbolCatalogEntry& candidate) {
                return candidate.code == normalized;
            });
        if (found != master.end()) entry = *found;
        AddRecent(entry);
    }

    bool TryResolveSymbolEntry(
        const std::string& selectedCode,
        trading::SymbolCatalogEntry& result)
    {
        if (selectedCode.empty()) return false;
        const std::string baseCode = IsNxtCode(selectedCode)
            ? selectedCode.substr(0U, 6U)
            : selectedCode;
        const std::vector<trading::SymbolCatalogEntry> master =
            SymbolMasterSnapshot();
        const auto found = std::find_if(
            master.begin(),
            master.end(),
            [&](const trading::SymbolCatalogEntry& entry) {
                return entry.code == selectedCode || entry.code == baseCode;
            });
        if (found == master.end()) return false;
        result = *found;
        return true;
    }

    const char* MarketDisplayName(const std::string& market) noexcept
    {
        if (market == "0" || market == "KOSPI") return "KOSPI";
        if (market == "10" || market == "KOSDAQ") return "KOSDAQ";
        if (market == "NXT") return "NXT";
        return market.empty() ? "" : market.c_str();
    }

    std::string FormatInteger(long long value)
    {
        std::string text = std::to_string(value);
        const std::size_t firstDigit =
            !text.empty() && text.front() == '-' ? 1U : 0U;
        std::size_t position = text.size();
        while (position > firstDigit + 3U) {
            position -= 3U;
            text.insert(position, ",");
        }
        return text;
    }

    ImVec4 PriceDirectionColor(long long change) noexcept
    {
        if (change > 0) return ImVec4(0.95f, 0.24f, 0.24f, 1.0f);
        if (change < 0) return ImVec4(0.28f, 0.55f, 1.0f, 1.0f);
        return ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
    }

    void DrawTimeFrameButtons(const std::string& code)
    {
        static const char* timeFrames[] = {
            "1분", "3분", "5분", "10분", "15분", "30분", "60분"};
        for (int index = 0; index < IM_ARRAYSIZE(timeFrames); ++index) {
            if (index > 0) ImGui::SameLine();
            const bool active = index == g_timeFrameIndex;
            if (active) {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImVec4(0.18f, 0.38f, 0.68f, 1.0f));
                ImGui::PushStyleColor(
                    ImGuiCol_ButtonHovered,
                    ImVec4(0.24f, 0.48f, 0.82f, 1.0f));
            }
            if (ImGui::Button(timeFrames[index])) {
                g_timeFrameIndex = index;
                if (!code.empty()) {
                    g_commandBus.Push(Cmd::LoadSymbol, code, index);
                }
            }
            if (active) ImGui::PopStyleColor(2);
        }
    }
}

bool ImGui::M82InputText(
    const char* label,
    char* buffer,
    std::size_t bufferSize,
    ImGuiInputTextFlags flags,
    ImGuiInputTextCallback callback,
    void* userData)
{
    EnsureSymbolMasterLoaded();
    RequestSymbolMasterRefresh();
    PersistSymbolMasterWhenStable();

    const bool isSymbolInput =
        label != nullptr && std::strcmp(label, "##symbol") == 0;
    const bool inputWasActive =
        isSymbolInput && ImGui::GetActiveID() == ImGui::GetID(label);
    const bool enterPressedBeforeInput =
        inputWasActive && ImGui::IsKeyPressed(ImGuiKey_Enter, false);

    const bool changed = ImGui::InputText(
        label,
        buffer,
        bufferSize,
        flags,
        callback,
        userData);

    if (!isSymbolInput) return changed;

    bool selectionChanged = false;
    const bool inputActive = ImGui::IsItemActive();
    if (ImGui::IsItemActivated() && buffer[0] == '\0') {
        RefreshToolbarMatches(buffer);
    }
    if (changed) {
        RefreshToolbarMatches(buffer);
        RecordDirectCode(buffer);
    }

    if (inputActive && ImGui::IsKeyPressed(ImGuiKey_DownArrow) &&
        !g_m82ToolbarMatches.empty())
    {
        const int count = static_cast<int>(g_m82ToolbarMatches.size());
        g_m82ToolbarHighlight =
            (g_m82ToolbarHighlight + 1 + count) % count;
        g_m82ToolbarPopupOpen = true;
    }
    if (inputActive && ImGui::IsKeyPressed(ImGuiKey_UpArrow) &&
        !g_m82ToolbarMatches.empty())
    {
        const int count = static_cast<int>(g_m82ToolbarMatches.size());
        g_m82ToolbarHighlight =
            (g_m82ToolbarHighlight - 1 + count) % count;
        g_m82ToolbarPopupOpen = true;
    }
    if (inputActive && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        g_m82ToolbarPopupOpen = false;
    }
    if (enterPressedBeforeInput) {
        if (g_m82ToolbarPopupOpen && !g_m82ToolbarMatches.empty()) {
            selectionChanged = SelectToolbarMatch(
                buffer,
                bufferSize,
                g_m82ToolbarHighlight);
        }
        else {
            const std::string normalized = NormalizeCode(buffer);
            if (normalized != buffer) {
                ImGui::ClearActiveID();
                std::snprintf(buffer, bufferSize, "%s", normalized.c_str());
                selectionChanged = true;
            }
            RecordDirectCode(buffer);
        }
    }

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("6자리 코드 또는 6자리_AL을 직접 입력할 수 있습니다.");
        ImGui::TextUnformatted("한글 종목명 입력 시 후보를 선택하면 코드가 입력됩니다.");
        ImGui::TextUnformatted("빈 입력란을 클릭하면 최근 선택 종목을 표시합니다.");
        ImGui::Separator();
        ImGui::Text("%s (%zu종목)",
            g_m82MasterStatus.c_str(),
            SymbolMasterSnapshot().size());
        if (!g_m82MasterError.empty()) {
            ImGui::TextColored(
                ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
                "%s",
                g_m82MasterError.c_str());
        }
        ImGui::EndTooltip();
    }

    bool mouseSelectionPending = false;
    trading::SymbolCatalogEntry mouseSelectedEntry;
    if (g_m82ToolbarPopupOpen && !g_m82ToolbarMatches.empty()) {
        if (ImGui::BeginListBox(
                "##m82_toolbar_symbol_matches",
                ImVec2(300.0f, 180.0f)))
        {
            for (int index = 0;
                 index < static_cast<int>(g_m82ToolbarMatches.size());
                 ++index)
            {
                const trading::SymbolCatalogEntry& entry =
                    g_m82ToolbarMatches[static_cast<std::size_t>(index)];
                std::string text = entry.code;
                if (!entry.name.empty() && entry.name != entry.code) {
                    text += "  " + entry.name;
                }
                if (!entry.market.empty()) {
                    text += "  [" + entry.market + "]";
                }

                ImGui::PushID(index);
                const bool selected = index == g_m82ToolbarHighlight;
                if (ImGui::Selectable(text.c_str(), selected)) {
                    mouseSelectedEntry = entry;
                    mouseSelectionPending = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndListBox();
        }
    }

    if (mouseSelectionPending) {
        selectionChanged = ApplyToolbarEntry(
            buffer,
            bufferSize,
            mouseSelectedEntry) || selectionChanged;
    }

    return changed || selectionChanged;
}

static void DrawTradingToolbar()
{
    ImGui::PushID("practical_trading_toolbar");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));

    const trading::app::MarketDataSnapshot market =
        g_marketDataModule.Snapshot();
    const trading::KiwoomRuntimeSnapshot runtime = RuntimeSnapshot();
    const bool socketUp =
        runtime.sessionState == trading::KiwoomSessionState::LoginPending ||
        runtime.sessionState == trading::KiwoomSessionState::RegistrationPending ||
        runtime.sessionState == trading::KiwoomSessionState::ReconciliationPending ||
        runtime.sessionState == trading::KiwoomSessionState::Ready;

    const std::string selectedCode = NormalizeCode(g_symbolInput);
    trading::SymbolCatalogEntry selectedEntry;
    const bool hasSelectedEntry =
        TryResolveSymbolEntry(selectedCode, selectedEntry);
    const bool quoteReady =
        market.state == trading::app::MarketDataState::Ready &&
        market.hasLatestBar &&
        !selectedCode.empty() &&
        market.code == selectedCode;

    ImGui::SetNextItemWidth(110.0f);
    ImGui::M82InputText("##symbol", g_symbolInput, sizeof(g_symbolInput));
    ImGui::SameLine();

    if (hasSelectedEntry) {
        ImGui::TextUnformatted(selectedEntry.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", MarketDisplayName(selectedEntry.market));
    }
    else if (!selectedCode.empty()) {
        ImGui::TextDisabled("종목명 확인 중");
    }
    else {
        ImGui::TextDisabled("종목 선택");
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    if (quoteReady) {
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
            "현재봉 시가대비 %s%s원 (%+.2f%%)",
            change > 0 ? "+" : "",
            FormatInteger(change).c_str(),
            changeRate);
        ImGui::SameLine();
        ImGui::TextDisabled(
            "O %s  H %s  L %s  %d분봉 거래량 %s",
            FormatInteger(market.latestBar.open).c_str(),
            FormatInteger(market.latestBar.high).c_str(),
            FormatInteger(market.latestBar.low).c_str(),
            market.minuteUnit,
            FormatInteger(static_cast<long long>(market.latestBar.volume)).c_str());
    }
    else {
        ImGui::TextDisabled(
            market.state == trading::app::MarketDataState::Loading
                ? "실제 시세 조회 중"
                : "선택 종목 실제 시세 없음");
    }

    ImGui::SameLine();
    ImGui::TextDisabled("| 수량");
    ImGui::SameLine();
    if (ImGui::SmallButton("-")) {
        g_orderQuantity = (std::max)(1, g_orderQuantity - 1);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(55.0f);
    if (ImGui::InputInt("##toolbar_order_quantity", &g_orderQuantity, 0, 0)) {
        g_orderQuantity = (std::max)(1, g_orderQuantity);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) ++g_orderQuantity;

    ImGui::SameLine();
    const bool canBuy = CanSubmitEntryOrders() && quoteReady;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.68f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.86f, 0.18f, 0.18f, 1.0f));
    if (!canBuy) ImGui::BeginDisabled();
    if (ImGui::Button("즉시매수")) {
        g_commandBus.Push(Cmd::MockBuy, market.code, g_orderQuantity);
    }
    if (!canBuy) ImGui::EndDisabled();
    ImGui::PopStyleColor(2);

    trading::PositionSnapshot selectedPosition;
    const bool hasSelectedPosition =
        !selectedCode.empty() && FindPosition(selectedCode, selectedPosition);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.28f, 0.68f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.40f, 0.88f, 1.0f));
    const bool canLiquidatePosition =
        CanSubmitLiquidationOrders() && hasSelectedPosition;
    if (!canLiquidatePosition) ImGui::BeginDisabled();
    if (ImGui::Button("보유청산")) {
        g_commandBus.Push(Cmd::LiquidatePosition, selectedCode);
    }
    if (!canLiquidatePosition) ImGui::EndDisabled();
    ImGui::PopStyleColor(2);

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
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f), "모의");
    ImGui::SameLine();
    ImGui::TextColored(
        socketUp ? ImVec4(0.30f, 0.90f, 0.40f, 1.0f)
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
        ImGui::Text("부팅 %.0fms / 렌더 %.1fHz", g_bootMilliseconds, g_renderRateHz);
        if (market.lastStockTradeTimestampMs > 0) {
            ImGui::Text(
                "0B %llu건 / 최근 %lldms",
                static_cast<unsigned long long>(market.stockTradeTickCount),
                static_cast<long long>(tradeAgeMs));
        }
        else {
            ImGui::Text(
                "0B %s",
                market.stockTradeSubscriptionRequested ? "수신대기" : "미등록");
        }
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.18f, 0.18f, 1.0f));
    const bool canLiquidateAll = CanSubmitLiquidationOrders();
    if (!canLiquidateAll) ImGui::BeginDisabled();
    const bool liquidateAll = ImGui::Button("전량청산");
    if (!canLiquidateAll) ImGui::EndDisabled();
    ImGui::PopStyleColor(2);

    ImGui::PopStyleVar();
    ImGui::PopID();

    if (liquidateAll) ImGui::OpenPopup("confirm_liquidate_all");
    if (ImGui::BeginPopupModal(
            "confirm_liquidate_all",
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

static void DrawTradingMarketDataPanel()
{
    ImGui::Begin(
        "실제 시세",
        nullptr,
        ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);

    const trading::app::MarketDataSnapshot snapshot =
        g_marketDataModule.Snapshot();

    if (snapshot.state != trading::app::MarketDataState::Ready ||
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

    DrawTimeFrameButtons(snapshot.code);
    ImGui::SameLine();
    if (ImGui::Button("재조회")) {
        g_commandBus.Push(Cmd::ResetFeed);
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s | %d분 | 수신 %zu봉 | %d분봉 거래량 %s",
        snapshot.code.c_str(),
        snapshot.minuteUnit,
        snapshot.barCount,
        snapshot.minuteUnit,
        FormatInteger(static_cast<long long>(snapshot.latestBar.volume)).c_str());

    if (snapshot.continuation.continueYn == "Y" ||
        snapshot.continuation.continueYn == "y")
    {
        ImGui::SameLine();
        ImGui::TextDisabled("| 추가 과거데이터 있음");
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
            trading::app::FeatureLevel::Visible) &&
        !g_indicatorSpecs.empty())
    {
        const bool calculationNeeded =
            indicatorSnapshot.state != trading::app::IndicatorModuleState::Ready ||
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
            indicatorSource.completedRevision = marketSeries.completedRevision;

            std::string indicatorError;
            if (!g_indicatorModule.Update(indicatorSource, indicatorError)) {
                g_log.Add("FAULT", "지표 계산 실패: %s", indicatorError.c_str());
                std::string healthError;
                g_featureRegistry.SetHealth(
                    "indicators", false, indicatorError, healthError);
            }
            else {
                indicatorSnapshot = g_indicatorModule.Snapshot();
                const trading::app::FeatureMetrics& metrics =
                    indicatorSnapshot.metrics;
                RecordFeatureWork(
                    "indicators",
                    metrics.lastProcessingMicros,
                    metrics.retainedBytes + g_indicatorRenderAdapter.RetainedBytes(),
                    metrics.symbolCount,
                    metrics.renderSeriesCount,
                    metrics.mergedEventCount - previousMergedEvents,
                    metrics.droppedEventCount - previousDroppedEvents);
                std::string healthError;
                g_featureRegistry.SetHealth("indicators", true, {}, healthError);
                useIndicators = true;
            }
        }
        else {
            useIndicators = true;
        }
    }

    trading::app::ComparisonModuleSnapshot comparisonSnapshot =
        g_comparisonModule.Snapshot();
    trading::app::IndicatorModuleSnapshot workspaceIndicator =
        indicatorSnapshot;
    if (!useIndicators) {
        workspaceIndicator.level = trading::app::FeatureLevel::Off;
    }

    const bool chartNeedsUpdate = g_chartWorkspaceModule.NeedsUpdate(
        marketSeries.revision,
        workspaceIndicator,
        g_indicatorRenderAdapter,
        comparisonSnapshot,
        g_comparisonRenderAdapter);

    if (chartNeedsUpdate) {
        std::string chartError;
        const bool updated = g_chartWorkspaceModule.UpdateMarketChart(
            "main-market-chart",
            snapshot.code,
            snapshot.code,
            chartSource,
            workspaceIndicator,
            g_indicatorRenderAdapter,
            comparisonSnapshot,
            g_comparisonRenderAdapter,
            chartError);
        if (!updated) {
            g_log.Add(
                "FAULT",
                "차트 워크스페이스 갱신 실패: %s",
                chartError.c_str());
            std::string healthError;
            g_featureRegistry.SetHealth(
                "chart-workspace", false, chartError, healthError);
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
        if (trading::app::FindIndicatorDefinition(
                g_indicatorDefinitions,
                g_mainRenderSurface.selectedOwnerId) != nullptr)
        {
            trading::ui::SelectIndicator(
                g_indicatorManagerUi,
                g_mainRenderSurface.selectedOwnerId,
                g_mainRenderSurface.selectionDoubleClicked);
        }
        else {
            trading::app::ComparisonDefinition comparison;
            if (g_comparisonModule.FindDefinition(
                    g_mainRenderSurface.selectedOwnerId,
                    comparison))
            {
                trading::ui::SelectComparison(
                    g_comparisonManagerUi,
                    comparison.id,
                    g_mainRenderSurface.selectionDoubleClicked);
            }
        }
        WakeFrames(4);
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

static void DrawTradingDashboard()
{
    ImGui::Begin("대시보드");

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

    const bool canLiquidate = CanSubmitLiquidationOrders();
    if (ImGui::BeginTable(
            "positions",
            8,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp))
    {
        const char* headers[] = {
            "선택", "종목", "수량", "평단", "현재가",
            "평가손익", "수익률", "전량매도"};
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
            if (ImGui::SmallButton("전량매도")) {
                g_commandBus.Push(Cmd::LiquidatePosition, position.code);
            }
            if (!canLiquidate) ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::End();
}
