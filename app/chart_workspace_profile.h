#pragma once

#include "chart_workspace_persistence.h"

#include <string>

namespace trading::app
{
    // Loads only durable user intent: indicator identity, type, visibility,
    // calculation parameters, and pane heights. Renderer caches and transient
    // output fields are deliberately excluded from this contract.
    bool LoadChartWorkspaceProfileState(
        const std::string& path,
        ChartWorkspacePersistenceState& state,
        bool& found,
        std::string& error);

    bool SaveChartWorkspaceProfileState(
        const std::string& path,
        const ChartWorkspacePersistenceState& state,
        std::string& error);
}
