#pragma once

#include "../app/indicator_configuration.h"

#include <string>
#include <vector>

namespace trading::ui
{
    struct IndicatorManagerUiState final
    {
        std::string selectedIndicatorId;
        app::IndicatorInstanceDefinition draft;
        bool draftValid = false;
        bool dirty = false;
        bool focusRequested = false;
        int addTypeIndex = 0;
        int addPaneIndex = 0;
        std::string error;
    };

    using ApplyIndicatorDefinitions = bool(*)(
        const std::vector<app::IndicatorInstanceDefinition>&,
        std::string&);

    void SelectIndicator(
        IndicatorManagerUiState& state,
        const std::string& indicatorId,
        bool focusProperties);

    void DrawIndicatorManagerWindow(
        std::vector<app::IndicatorInstanceDefinition>& definitions,
        IndicatorManagerUiState& state,
        ApplyIndicatorDefinitions applyDefinitions);
}
