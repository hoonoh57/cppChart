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

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    std::vector<trading::SymbolCatalogEntry> g_m82ToolbarMatches;
    std::vector<trading::SymbolCatalogEntry> g_m82RecentSymbols;
    int g_m82ToolbarHighlight = -1;
    bool g_m82ToolbarPopupOpen = false;
    double g_m82LastCatalogRequestSeconds = -1000.0;
    std::size_t g_m82ObservedCatalogSize = 0U;
    std::string g_m82CatalogError;

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

    std::vector<trading::SymbolCatalogEntry> SymbolCatalogSnapshot()
    {
        std::lock_guard<std::mutex> lock(g_symbolCatalogMutex);
        return g_symbolCatalog;
    }

    void RefreshToolbarMatches(const char* query)
    {
        const std::vector<trading::SymbolCatalogEntry> catalog =
            SymbolCatalogSnapshot();

        if (query == nullptr || query[0] == '\0') {
            g_m82ToolbarMatches = g_m82RecentSymbols;
        }
        else {
            g_m82ToolbarMatches =
                trading::SearchSymbolCatalog(catalog, query, 12U);
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
        AddRecent(entry);
    }

    void EnsureCatalogRequested()
    {
        if (!g_runtimeRunner || !g_runtimeRunner->IsRunning()) return;

        const std::size_t catalogSize = SymbolCatalogSnapshot().size();
        if (catalogSize > 0U) {
            g_m82CatalogError.clear();
            return;
        }

        const trading::KiwoomRuntimeSnapshot runtime = RuntimeSnapshot();
        const bool tokenUsable =
            runtime.orderSubmissionAllowed ||
            runtime.sessionState == trading::KiwoomSessionState::RegistrationPending ||
            runtime.sessionState == trading::KiwoomSessionState::ReconciliationPending ||
            runtime.sessionState == trading::KiwoomSessionState::Ready;
        if (!tokenUsable) return;

        const double now = NowSeconds();
        if (now - g_m82LastCatalogRequestSeconds < 3.0) return;
        g_m82LastCatalogRequestSeconds = now;

        std::string error;
        if (!RefreshSymbolCatalog(error)) {
            g_m82CatalogError = error.empty()
                ? "종목 목록 요청을 시작하지 못했습니다."
                : error;
        }
        else {
            g_m82CatalogError.clear();
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

    EnsureCatalogRequested();

    const std::size_t catalogSize = SymbolCatalogSnapshot().size();
    if (catalogSize != g_m82ObservedCatalogSize) {
        g_m82ObservedCatalogSize = catalogSize;
        if (buffer[0] != '\0' || ImGui::IsItemActive()) {
            RefreshToolbarMatches(buffer);
        }
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
        ImGui::Text("종목명 목록: %zu종목", catalogSize);
        if (!g_m82CatalogError.empty()) {
            ImGui::TextColored(
                ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
                "종목명 목록 오류: %s",
                g_m82CatalogError.c_str());
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
