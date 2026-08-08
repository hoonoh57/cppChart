#pragma once

#include "comparison_workspace_state.h"

#include <string>

namespace trading::app
{
    bool LoadVerifiedComparisonWorkspace(
        const std::string& savedPath,
        const std::string& defaultPath,
        ComparisonWorkspaceState& state,
        ComparisonWorkspaceSource& source,
        std::string& diagnostic);

    bool SaveVerifiedComparisonWorkspace(
        const std::string& savedPath,
        const ComparisonWorkspaceState& state,
        std::string& error);
}
