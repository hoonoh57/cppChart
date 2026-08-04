#pragma once

#include "../app/comparison_module.h"
#include "../core/kiwoom_symbol_catalog.h"
#include "symbol_search_ui.h"

#include <string>
#include <vector>

namespace trading::ui
{
    struct ComparisonManagerUiState final
    {
        std::string selectedId;
        app::ComparisonDefinition draft;
        bool draftValid = false;
        bool dirty = false;
        bool focusRequested = false;
        int addKind = 0;
        int addPlacement = 0;
        int addValueMode = 0;
        SymbolSearchState addSearch;
        char searchQuery[96]{};
        char addCode[32]{};
        char addName[64]{};
        std::string error;
    };

    using ApplyComparisonDefinitions = bool(*)(
        const std::vector<app::ComparisonDefinition>&,
        std::string&);

    using RequestComparisonData = bool(*)(
        const std::string&,
        std::string&);

    using RefreshSymbolCatalog = bool(*)(std::string&);

    void SelectComparison(
        ComparisonManagerUiState& state,
        const std::string& comparisonId,
        bool focusWindow);

    void DrawComparisonManagerWindow(
        std::vector<app::ComparisonDefinition>& definitions,
        const app::ComparisonModuleSnapshot& snapshot,
        const std::vector<SymbolCatalogEntry>& symbolCatalog,
        ComparisonManagerUiState& state,
        ApplyComparisonDefinitions applyDefinitions,
        RequestComparisonData requestData,
        RefreshSymbolCatalog refreshCatalog);
}
