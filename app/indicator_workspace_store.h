#pragma once

#include "indicator_workspace_state.h"

#include <string>

namespace trading::app
{
    // Single durable owner for indicator workspace JSON. A saved file is never
    // silently replaced by defaults. Every write is read back and compared
    // before it is reported as successful.
    bool LoadVerifiedIndicatorWorkspace(
        const std::string& savedPath,
        const std::string& defaultPath,
        IndicatorWorkspaceState& state,
        IndicatorWorkspaceSource& source,
        std::string& diagnostic);

    bool SaveVerifiedIndicatorWorkspace(
        const std::string& savedPath,
        const IndicatorWorkspaceState& state,
        std::string& error);
}
