#pragma once

#include "indicator_configuration.h"

#include <map>
#include <string>
#include <vector>

namespace trading::app
{
    struct ChartWorkspacePersistenceState final
    {
        std::vector<IndicatorInstanceDefinition> indicators;
        std::map<std::string, float> paneHeightWeights;
    };

    bool SerializeChartWorkspaceState(
        const ChartWorkspacePersistenceState& state,
        std::string& json,
        std::string& error);

    bool ParseChartWorkspaceState(
        const std::string& json,
        ChartWorkspacePersistenceState& state,
        std::string& error);

    bool LoadChartWorkspaceState(
        const std::string& path,
        ChartWorkspacePersistenceState& state,
        bool& found,
        std::string& error);

    bool SaveChartWorkspaceState(
        const std::string& path,
        const ChartWorkspacePersistenceState& state,
        std::string& error);
}
