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

#define InputText M82InputText
#include "shell_main.cpp"
#undef InputText

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

    void SelectToolbarMatch(
        char* buffer,
        std::size_t bufferSize,
        int index)
    {
        if (index < 0 ||
            index >= static_cast<int>(g_m82ToolbarMatches.size()))
        {
            return;
        }

        trading::SymbolCatalogEntry entry =
            g_m82ToolbarMatches[static_cast<std::size_t>(index)];
        entry.code = NormalizeCode(entry.code);
        std::snprintf(buffer, bufferSize, "%s", entry.code.c_str());
        AddRecent(entry);
        g_m82ToolbarPopupOpen = false;
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

    const bool changed = ImGui::InputText(
        label,
        buffer,
        bufferSize,
        flags,
        callback,
        userData);

    if (label == nullptr || std::strcmp(label, "##symbol") != 0) {
        return changed;
    }

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
    if (inputActive && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        if (g_m82ToolbarPopupOpen && !g_m82ToolbarMatches.empty()) {
            SelectToolbarMatch(
                buffer,
                bufferSize,
                g_m82ToolbarHighlight);
        }
        else {
            const std::string normalized = NormalizeCode(buffer);
            if (normalized != buffer) {
                std::snprintf(buffer, bufferSize, "%s", normalized.c_str());
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

                const bool selected = index == g_m82ToolbarHighlight;
                if (ImGui::Selectable(text.c_str(), selected)) {
                    SelectToolbarMatch(buffer, bufferSize, index);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }
    }

    return changed;
}
