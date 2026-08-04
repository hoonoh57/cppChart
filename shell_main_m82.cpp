#include "imgui.h"

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
    bool g_m82CatalogRequested = false;
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

    void RefreshToolbarMatches(const char* query)
    {
        std::vector<trading::SymbolCatalogEntry> catalog;
        {
            std::lock_guard<std::mutex> lock(g_symbolCatalogMutex);
            catalog = g_symbolCatalog;
        }

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

    const trading::KiwoomRuntimeSnapshot runtime = RuntimeSnapshot();
    if (!g_m82CatalogRequested &&
        runtime.sessionState == trading::KiwoomSessionState::Ready)
    {
        g_m82CatalogRequested = true;
        std::string error;
        if (!RefreshSymbolCatalog(error)) {
            g_m82CatalogError = error;
        }
        else {
            g_m82CatalogError.clear();
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
