#pragma once

#include "indicator_configuration.h"

#include <map>
#include <string>
#include <vector>

namespace trading::app
{
    enum class IndicatorWorkspaceSource
    {
        None,
        Saved,
        Legacy,
        Default
    };

    struct IndicatorWorkspaceState final
    {
        std::vector<IndicatorInstanceDefinition> indicators;
        std::map<std::string, float> paneHeightWeights;
    };

    // Loads durable application state only. The renderer never reads files and
    // never decides which indicators are enabled.
    bool LoadIndicatorWorkspaceState(
        const std::string& savedPath,
        const std::string& legacyPath,
        const std::string& defaultPath,
        IndicatorWorkspaceState& state,
        IndicatorWorkspaceSource& source,
        std::string& diagnostic);

    bool SaveIndicatorWorkspaceState(
        const std::string& path,
        const IndicatorWorkspaceState& state,
        std::string& error);

    bool SerializeIndicatorWorkspaceState(
        const IndicatorWorkspaceState& state,
        std::string& json,
        std::string& error);

    bool NormalizeIndicatorWorkspaceDefinitions(
        std::vector<IndicatorInstanceDefinition>& definitions,
        std::string& error);

    const char* IndicatorWorkspaceSourceName(
        IndicatorWorkspaceSource source) noexcept;
}
