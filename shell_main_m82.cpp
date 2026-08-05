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

#define M83_JOIN_INNER(left, right) left##right
#define M83_JOIN(left, right) M83_JOIN_INNER(left, right)
#define M83_DRAW_TOOLBAR_743() DrawToolbarOriginal()
#define M83_DRAW_TOOLBAR_2381() DrawTradingToolbar()
#define DrawToolbar() M83_JOIN(M83_DRAW_TOOLBAR_, __LINE__)()
#define InputText M82InputText
#include "shell_main.cpp"
#undef InputText
#undef DrawToolbar
#undef M83_DRAW_TOOLBAR_2381
#undef M83_DRAW_TOOLBAR_743
#undef M83_JOIN
#undef M83_JOIN_INNER

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

    if (!isSymbolInput) {
        return changed;
    }

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
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(6.0f, 4.0f));

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

    ImGui::SetNextItemWidth(120.0f);
    ImGui::M82InputText(
        "##symbol",
        g_symbolInput,
        sizeof(g_symbolInput));
    ImGui::SameLine();

    if (hasSelectedEntry) {
        ImGui::TextUnformatted(selectedEntry.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(
            "[%s]",
            MarketDisplayName(selectedEntry.market));
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
        const std::string currentText = FormatInteger(current);
        const std::string changeText = FormatInteger(change);
        ImGui::TextColored(
            directionColor,
            "%s원",
            currentText.c_str());
        ImGui::SameLine();
        ImGui::TextColored(
            directionColor,
            "시가대비 %s%s원 (%+.2f%%)",
            change > 0 ? "+" : "",
            changeText.c_str(),
            changeRate);
        ImGui::SameLine();
        ImGui::TextDisabled(
            "O %s  H %s  L %s  현재봉 V %s",
            FormatInteger(market.latestBar.open).c_str(),
            FormatInteger(market.latestBar.high).c_str(),
            FormatInteger(market.latestBar.low).c_str(),
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
    if (ImGui::InputInt(
            "##toolbar_order_quantity",
            &g_orderQuantity,
            0,
            0))
    {
        g_orderQuantity = (std::max)(1, g_orderQuantity);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) {
        ++g_orderQuantity;
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
    if (ImGui::Button("즉시매수")) {
        g_commandBus.Push(
            Cmd::MockBuy,
            market.code,
            g_orderQuantity);
    }
    if (!canBuy) ImGui::EndDisabled();
    ImGui::PopStyleColor(2);

    trading::PositionSnapshot selectedPosition;
    const bool hasSelectedPosition =
        !selectedCode.empty() &&
        FindPosition(selectedCode, selectedPosition);
    ImGui::SameLine();
    ImGui::PushStyleColor(
        ImGuiCol_Button,
        ImVec4(0.12f, 0.28f, 0.68f, 1.0f));
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImVec4(0.18f, 0.40f, 0.88f, 1.0f));
    const bool canLiquidatePosition =
        CanSubmitLiquidationOrders() && hasSelectedPosition;
    if (!canLiquidatePosition) ImGui::BeginDisabled();
    if (ImGui::Button("보유청산")) {
        g_commandBus.Push(Cmd::LiquidatePosition, selectedCode);
    }
    if (!canLiquidatePosition) ImGui::EndDisabled();
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered() && hasSelectedPosition) {
        ImGui::SetTooltip(
            "%s %d주 전량 시장가 청산",
            selectedPosition.name.c_str(),
            selectedPosition.quantity);
    }

    ImGui::SameLine();
    if (g_observeMode.load(std::memory_order_acquire)) {
        const bool canActivate = CanActivateEntries();
        if (!canActivate) ImGui::BeginDisabled();
        if (ImGui::Button("진입 허용")) {
            g_commandBus.Push(Cmd::ArmStrategy);
        }
        if (!canActivate) ImGui::EndDisabled();
    }
    else if (ImGui::Button("관망 전환")) {
        g_commandBus.Push(Cmd::DisarmStrategy);
    }

    ImGui::NewLine();

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
        }
        if (active) ImGui::PopStyleColor(2);
    }

    ImGui::SameLine();
    if (ImGui::Button("조회")) {
        g_commandBus.Push(
            Cmd::LoadSymbol,
            selectedCode,
            g_timeFrameIndex);
    }
    ImGui::SameLine();
    if (ImGui::Button("새로고침")) {
        g_commandBus.Push(Cmd::ResetFeed);
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextColored(
        ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
        "[MOCK]");
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
    ImGui::SameLine();
    ImGui::TextColored(
        g_observeMode.load(std::memory_order_acquire)
            ? ImVec4(1.0f, 0.75f, 0.20f, 1.0f)
            : ImVec4(0.35f, 0.95f, 0.45f, 1.0f),
        g_observeMode.load(std::memory_order_acquire)
            ? "[관망]"
            : "[진입허용]");

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
        else {
            ImGui::Text(
                "0B %s",
                market.stockTradeSubscriptionRequested
                    ? "수신대기"
                    : "미등록");
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
    const bool liquidateAll = ImGui::Button("전량청산");
    if (!canLiquidateAll) ImGui::EndDisabled();
    ImGui::PopStyleColor(2);

    ImGui::PopStyleVar();
    ImGui::PopID();

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
