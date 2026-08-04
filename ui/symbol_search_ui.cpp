#include "symbol_search_ui.h"

#include "imgui.h"

#include <algorithm>
#include <string>

namespace trading::ui
{
    namespace
    {
        const char* CatalogStateLabel(SymbolCatalogLoadState state) noexcept
        {
            switch (state) {
            case SymbolCatalogLoadState::Loading:
                return "종목 목록 불러오는 중";
            case SymbolCatalogLoadState::Loaded:
                return "종목 목록 준비";
            case SymbolCatalogLoadState::Failed:
                return "종목 목록 오류";
            default:
                return "종목 목록 미요청";
            }
        }
    }

    SymbolSearchUiResult DrawSymbolSearch(
        SymbolSearchState& state,
        const std::vector<SymbolCatalogEntry>& catalog,
        const SymbolSearchUiOptions& options)
    {
        SymbolSearchUiResult result;
        ImGui::SetNextItemWidth(options.inputWidth);
        result.queryChanged = ImGui::InputTextWithHint(
            options.inputId,
            options.hint,
            state.query,
            sizeof(state.query));

        const bool inputActive = ImGui::IsItemActive();
        if (result.queryChanged) {
            RejectFreeSymbolText(state);
            RefreshSymbolMatches(state, catalog);
        }

        if (inputActive && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            MoveSymbolHighlight(state, 1);
        }
        if (inputActive && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            MoveSymbolHighlight(state, -1);
        }
        if (inputActive && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            state.popupOpen = false;
            result.escapePressed = true;
        }
        if (inputActive && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            result.selectionConfirmed = ConfirmHighlightedSymbol(state);
            if (!result.selectionConfirmed) {
                result.selectionConfirmed = ConfirmExactSymbolCode(state, catalog);
            }
        }

        if (state.popupOpen && !state.matches.empty()) {
            const std::string popupId =
                std::string(options.inputId) + "_matches";
            if (ImGui::BeginListBox(
                    popupId.c_str(),
                    ImVec2(options.inputWidth, options.popupHeight)))
            {
                for (int index = 0;
                     index < static_cast<int>(state.matches.size());
                     ++index)
                {
                    const SymbolCatalogEntry& entry =
                        state.matches[static_cast<std::size_t>(index)];
                    const std::string label = entry.code + "  " +
                        entry.name + "  [" + entry.market + "]";
                    const bool highlighted = index == state.highlightedIndex;
                    if (ImGui::Selectable(label.c_str(), highlighted)) {
                        result.selectionConfirmed =
                            ConfirmSymbolMatch(state, index);
                    }
                    if (highlighted) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndListBox();
            }
        }

        if (options.showCatalogStatus) {
            ImGui::TextDisabled("%s", CatalogStateLabel(state.catalogState));
            if (state.catalogState == SymbolCatalogLoadState::Loaded) {
                ImGui::SameLine();
                ImGui::TextDisabled("%zu종목", state.catalogCount);
            }
            if (state.catalogState == SymbolCatalogLoadState::Failed &&
                !state.catalogError.empty())
            {
                ImGui::TextColored(
                    ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
                    "%s",
                    state.catalogError.c_str());
            }
            ImGui::SameLine();
            result.refreshRequested = ImGui::SmallButton("새로고침");
        }

        return result;
    }

    bool ContainsComparisonSource(
        const std::vector<app::ComparisonDefinition>& definitions,
        app::ComparisonInstrumentKind kind,
        const std::string& code,
        const std::string& excludedId)
    {
        return std::any_of(
            definitions.begin(),
            definitions.end(),
            [&](const app::ComparisonDefinition& definition) {
                return
                    definition.id != excludedId &&
                    definition.kind == kind &&
                    definition.code == code;
            });
    }
}
