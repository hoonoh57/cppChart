#pragma once

#include "../core/kiwoom_symbol_catalog.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace trading::ui
{
    inline bool IsSixDigitSymbolCode(const std::string& value) noexcept
    {
        return value.size() == 6U &&
            std::all_of(value.begin(), value.end(), [](unsigned char ch) {
                return ch >= '0' && ch <= '9';
            });
    }

    inline bool IsNxtExtendedSymbolCode(const std::string& value) noexcept
    {
        return value.size() == 9U &&
            std::all_of(value.begin(), value.begin() + 6, [](unsigned char ch) {
                return ch >= '0' && ch <= '9';
            }) &&
            value[6] == '_' &&
            (value[7] == 'A' || value[7] == 'a') &&
            (value[8] == 'L' || value[8] == 'l');
    }

    inline bool IsDirectSymbolCode(const std::string& value) noexcept
    {
        return IsSixDigitSymbolCode(value) || IsNxtExtendedSymbolCode(value);
    }

    inline std::string NormalizeDirectSymbolCode(std::string value)
    {
        if (IsNxtExtendedSymbolCode(value)) {
            value[7] = 'A';
            value[8] = 'L';
        }
        return value;
    }

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
            return IsDirectSymbolCode(code) && !name.empty();
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
        std::vector<SymbolCatalogEntry> recent;
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
        return left.code == right.code;
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
            ? "종목명 자동완성 목록을 불러오지 못했습니다."
            : error;
    }

    inline void RecordRecentSymbol(
        SymbolSearchState& state,
        const SymbolCatalogEntry& entry,
        std::size_t limit = 12U)
    {
        if (!IsDirectSymbolCode(entry.code)) return;
        state.recent.erase(
            std::remove_if(
                state.recent.begin(),
                state.recent.end(),
                [&](const SymbolCatalogEntry& existing) {
                    return existing.code == entry.code;
                }),
            state.recent.end());
        state.recent.insert(state.recent.begin(), entry);
        if (state.recent.size() > limit) state.recent.resize(limit);
    }

    inline void RefreshSymbolMatches(
        SymbolSearchState& state,
        const std::vector<SymbolCatalogEntry>& catalog,
        std::size_t limit = 12U)
    {
        const std::string query = state.query;
        if (query.empty()) {
            state.matches = state.recent;
            if (state.matches.size() > limit) state.matches.resize(limit);
        }
        else {
            state.matches = SearchSymbolCatalog(catalog, query, limit);
        }

        state.popupOpen = !state.matches.empty();
        if (state.matches.empty()) {
            state.highlightedIndex = -1;
        }
        else if (
            state.highlightedIndex < 0 ||
            state.highlightedIndex >= static_cast<int>(state.matches.size()))
        {
            state.highlightedIndex = 0;
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

        const SymbolCatalogEntry entry =
            state.matches[static_cast<std::size_t>(matchIndex)];
        state.selection.code = NormalizeDirectSymbolCode(entry.code);
        state.selection.name = entry.name.empty() ? entry.code : entry.name;
        state.selection.market = entry.market;
        std::snprintf(
            state.query,
            sizeof(state.query),
            "%s",
            state.selection.code.c_str());
        state.highlightedIndex = matchIndex;
        state.popupOpen = false;
        RecordRecentSymbol(state, entry);
        return true;
    }

    inline bool ConfirmHighlightedSymbol(SymbolSearchState& state)
    {
        return ConfirmSymbolMatch(state, state.highlightedIndex);
    }

    inline bool ConfirmDirectSymbolCode(SymbolSearchState& state)
    {
        const std::string normalized = NormalizeDirectSymbolCode(state.query);
        if (!IsDirectSymbolCode(normalized)) return false;

        state.selection.code = normalized;
        state.selection.name = normalized;
        state.selection.market = IsNxtExtendedSymbolCode(normalized)
            ? "NXT"
            : "직접입력";
        std::snprintf(
            state.query,
            sizeof(state.query),
            "%s",
            normalized.c_str());

        SymbolCatalogEntry recent;
        recent.code = normalized;
        recent.name = normalized;
        recent.market = state.selection.market;
        RecordRecentSymbol(state, recent);
        state.popupOpen = false;
        return true;
    }

    inline bool ConfirmExactSymbolCode(
        SymbolSearchState& state,
        const std::vector<SymbolCatalogEntry>& catalog)
    {
        const std::string normalized = NormalizeDirectSymbolCode(state.query);
        if (!IsDirectSymbolCode(normalized)) return false;

        const auto found = std::find_if(
            catalog.begin(),
            catalog.end(),
            [&normalized](const SymbolCatalogEntry& entry) {
                return entry.code == normalized;
            });
        if (found == catalog.end()) return ConfirmDirectSymbolCode(state);

        state.matches.assign(1U, *found);
        state.highlightedIndex = 0;
        return ConfirmHighlightedSymbol(state);
    }

    inline void RejectFreeSymbolText(SymbolSearchState& state)
    {
        if (!state.selection.IsValid()) return;
        if (std::string(state.query) != state.selection.code) {
            state.selection.Clear();
        }
    }

    inline std::string ConfirmedSymbolLabel(
        const SymbolSearchState& state)
    {
        if (!state.selection.IsValid()) return {};
        if (state.selection.name == state.selection.code) {
            return state.selection.code;
        }
        return state.selection.code + " " + state.selection.name +
            (state.selection.market.empty()
                ? std::string{}
                : " [" + state.selection.market + "]");
    }
}
