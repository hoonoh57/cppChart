#pragma once

#include "chart_workspace_persistence.h"

#include <string>

namespace trading::app
{
    bool SaveChartWorkspaceBootstrap(
        const std::string& path,
        const ChartWorkspacePersistenceState& state,
        std::string& error);

    bool LoadChartWorkspaceBootstrap(
        const std::string& path,
        ChartWorkspacePersistenceState& state,
        bool& found,
        std::string& error);
}
