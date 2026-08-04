#pragma once

#include "../app/comparison_module.h"

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

    void SelectComparison(
        ComparisonManagerUiState& state,
        const std::string& comparisonId,
        bool focusWindow);

    void DrawComparisonManagerWindow(
        std::vector<app::ComparisonDefinition>& definitions,
        const app::ComparisonModuleSnapshot& snapshot,
        ComparisonManagerUiState& state,
        ApplyComparisonDefinitions applyDefinitions,
        RequestComparisonData requestData);
}
