#pragma once

#include "symbol_search_state.h"
#include "../app/comparison_module.h"

#include <string>
#include <vector>

namespace trading::ui
{
    struct SymbolSearchUiOptions final
    {
        const char* inputId = "##symbol_search";
        const char* hint = "코드 또는 한글 종목명";
        float inputWidth = 220.0f;
        float popupHeight = 180.0f;
        bool showCatalogStatus = true;
    };

    struct SymbolSearchUiResult final
    {
        bool queryChanged = false;
        bool selectionConfirmed = false;
        bool refreshRequested = false;
        bool escapePressed = false;
    };

    SymbolSearchUiResult DrawSymbolSearch(
        SymbolSearchState& state,
        const std::vector<SymbolCatalogEntry>& catalog,
        const SymbolSearchUiOptions& options = {});

    bool ContainsComparisonSource(
        const std::vector<app::ComparisonDefinition>& definitions,
        app::ComparisonInstrumentKind kind,
        const std::string& code,
        const std::string& excludedId = {});
}
