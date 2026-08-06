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
        Default
    };

    struct IndicatorWorkspaceState final
    {
        std::vector<IndicatorInstanceDefinition> indicators;
        std::map<std::string, float> paneHeightWeights;
    };

    // Application-state boundary. The renderer receives an already resolved
    // render plan and never reads JSON or creates default indicators.
    bool LoadIndicatorWorkspaceState(
        const std::string& savedPath,
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
