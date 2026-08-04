#pragma once

#include "../core/kiwoom_symbol_catalog.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace trading::ui
{
    enum class SymbolCatalogLoadState
    {
        Idle,
        Loading,
        Loaded,
        Failed
    };

    struct SymbolSearchSelection final
    {
        std::string code;
        std::string name;
        std::string market;

        bool IsValid() const noexcept
        {
            return code.size() == 6U && !name.empty() && !market.empty();
        }

        void Clear()
        {
            code.clear();
            name.clear();
            market.clear();
        }
    };

    struct SymbolSearchState final
    {
        char query[96]{};
        std::vector<SymbolCatalogEntry> matches;
        int highlightedIndex = -1;
        bool popupOpen = false;
        SymbolSearchSelection selection;
        SymbolCatalogLoadState catalogState = SymbolCatalogLoadState::Idle;
        std::size_t catalogCount = 0;
        std::string catalogError;

        bool HasValidSelection() const noexcept
        {
            return selection.IsValid();
        }
    };

    inline bool SameSymbol(
        const SymbolSearchSelection& left,
        const SymbolCatalogEntry& right) noexcept
    {
        return
            left.code == right.code &&
            left.name == right.name &&
            left.market == right.market;
    }

    inline void ClearSymbolSelection(SymbolSearchState& state)
    {
        state.selection.Clear();
    }

    inline void SetSymbolCatalogLoading(SymbolSearchState& state)
    {
        state.catalogState = SymbolCatalogLoadState::Loading;
        state.catalogCount = 0;
        state.catalogError.clear();
        state.matches.clear();
        state.highlightedIndex = -1;
        state.popupOpen = false;
    }

    inline void SetSymbolCatalogLoaded(
        SymbolSearchState& state,
        std::size_t count)
    {
        state.catalogState = SymbolCatalogLoadState::Loaded;
        state.catalogCount = count;
        state.catalogError.clear();
    }

    inline void SetSymbolCatalogFailed(
        SymbolSearchState& state,
        const std::string& error)
    {
        state.catalogState = SymbolCatalogLoadState::Failed;
        state.catalogCount = 0;
        state.catalogError = error.empty()
            ? "종목 목록을 불러오지 못했습니다."
            : error;
        state.matches.clear();
        state.highlightedIndex = -1;
        state.popupOpen = false;
    }

    inline void RefreshSymbolMatches(
        SymbolSearchState& state,
        const std::vector<SymbolCatalogEntry>& catalog,
        std::size_t limit = 12U)
    {
        state.matches = SearchSymbolCatalog(catalog, state.query, limit);
        state.popupOpen = !state.matches.empty() && state.query[0] != '\0';
        if (state.matches.empty()) {
            state.highlightedIndex = -1;
        }
        else if (
            state.highlightedIndex < 0 ||
            state.highlightedIndex >= static_cast<int>(state.matches.size()))
        {
            state.highlightedIndex = 0;
        }

        if (state.selection.IsValid()) {
            const auto selected = std::find_if(
                catalog.begin(),
                catalog.end(),
                [&state](const SymbolCatalogEntry& entry) {
                    return SameSymbol(state.selection, entry);
                });
            if (selected == catalog.end()) state.selection.Clear();
        }
    }

    inline void MoveSymbolHighlight(
        SymbolSearchState& state,
        int delta)
    {
        if (state.matches.empty() || delta == 0) return;
        const int count = static_cast<int>(state.matches.size());
        int index = state.highlightedIndex;
        if (index < 0 || index >= count) index = 0;
        index = (index + delta) % count;
        if (index < 0) index += count;
        state.highlightedIndex = index;
        state.popupOpen = true;
    }

    inline bool ConfirmSymbolMatch(
        SymbolSearchState& state,
        int matchIndex)
    {
        if (
            matchIndex < 0 ||
            matchIndex >= static_cast<int>(state.matches.size()))
        {
            return false;
        }
        const SymbolCatalogEntry& entry = state.matches[matchIndex];
        state.selection.code = entry.code;
        state.selection.name = entry.name;
        state.selection.market = entry.market;
        std::snprintf(
            state.query,
            sizeof(state.query),
            "%s",
            entry.name.c_str());
        state.highlightedIndex = matchIndex;
        state.popupOpen = false;
        return true;
    }

    inline bool ConfirmHighlightedSymbol(SymbolSearchState& state)
    {
        return ConfirmSymbolMatch(state, state.highlightedIndex);
    }

    inline bool ConfirmExactSymbolCode(
        SymbolSearchState& state,
        const std::vector<SymbolCatalogEntry>& catalog)
    {
        const std::string query = state.query;
        if (query.size() != 6U ||
            !std::all_of(query.begin(), query.end(), [](unsigned char value) {
                return value >= '0' && value <= '9';
            }))
        {
            return false;
        }
        const auto found = std::find_if(
            catalog.begin(),
            catalog.end(),
            [&query](const SymbolCatalogEntry& entry) {
                return entry.code == query;
            });
        if (found == catalog.end()) return false;
        state.matches.assign(1U, *found);
        state.highlightedIndex = 0;
        return ConfirmHighlightedSymbol(state);
    }

    inline void RejectFreeSymbolText(SymbolSearchState& state)
    {
        if (!state.selection.IsValid()) return;
        const std::string query = state.query;
        if (query != state.selection.code && query != state.selection.name) {
            state.selection.Clear();
        }
    }

    inline std::string ConfirmedSymbolLabel(
        const SymbolSearchState& state)
    {
        if (!state.selection.IsValid()) return {};
        return state.selection.code + " " + state.selection.name +
            " [" + state.selection.market + "]";
    }
}
