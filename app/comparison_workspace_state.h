#pragma once

#include "comparison_module.h"

#include <map>
#include <string>
#include <vector>

namespace trading::app
{
    enum class ComparisonWorkspaceSource
    {
        None,
        Saved,
        Default
    };

    struct ComparisonWorkspaceState final
    {
        std::vector<ComparisonDefinition> definitions;
        std::map<std::string, float> paneHeightWeights;
    };

    bool LoadComparisonWorkspaceState(
        const std::string& savedPath,
        const std::string& defaultPath,
        ComparisonWorkspaceState& state,
        ComparisonWorkspaceSource& source,
        std::string& diagnostic);

    bool SaveComparisonWorkspaceState(
        const std::string& path,
        const ComparisonWorkspaceState& state,
        std::string& error);

    bool SerializeComparisonWorkspaceState(
        const ComparisonWorkspaceState& state,
        std::string& json,
        std::string& error);

    bool ValidateComparisonWorkspaceState(
        const ComparisonWorkspaceState& state,
        std::string& error);

    const char* ComparisonWorkspaceSourceName(
        ComparisonWorkspaceSource source) noexcept;
}
