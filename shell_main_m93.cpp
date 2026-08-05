#include "app/chart_workspace_profile.h"

namespace trading::app
{
    bool M93LoadChartWorkspaceState(
        const std::string& path,
        ChartWorkspacePersistenceState& state,
        bool& found,
        std::string& error)
    {
        // The durable profile is authoritative. It intentionally ignores
        // renderer-internal fields and can also read the older full JSON.
        if (LoadChartWorkspaceProfileState(
                path,
                state,
                found,
                error))
        {
            return true;
        }

        const std::string profileError = error;
        ChartWorkspacePersistenceState strictState;
        bool strictFound = false;
        std::string strictError;
        if (LoadChartWorkspaceState(
                path,
                strictState,
                strictFound,
                strictError))
        {
            state = std::move(strictState);
            found = strictFound;
            error.clear();
            return true;
        }

        error = "지표 프로필=" + profileError +
            " / 전체 작업공간=" + strictError;
        return false;
    }

    bool M93SaveChartWorkspaceState(
        const std::string& path,
        const ChartWorkspacePersistenceState& state,
        std::string& error)
    {
        return SaveChartWorkspaceProfileState(path, state, error);
    }
}

#define LoadChartWorkspaceState M93LoadChartWorkspaceState
#define SaveChartWorkspaceState M93SaveChartWorkspaceState
#include "shell_main_m90.cpp"
#undef SaveChartWorkspaceState
#undef LoadChartWorkspaceState
