#pragma once

#include "chart_workspace_module.h"
#include "default_indicator_render_plan.h"
#include "indicator_module.h"
#include "indicator_render_adapter.h"

#include <string>
#include <vector>

namespace trading::app
{
    struct IndicatorWorkspaceSnapshot final
    {
        FeatureLevel level = FeatureLevel::Off;
        IndicatorModuleSnapshot indicators;
        ChartWorkspaceSnapshot workspace;
        std::uint64_t renderPlanRevision = 0;
        std::size_t renderAdapterRetainedBytes = 0;
        std::string error;
    };

    class IndicatorWorkspaceCoordinator final
    {
    public:
        bool Configure(
            const std::vector<indicators::IndicatorSpec>& specs,
            std::string& error);

        bool SetLevel(
            FeatureLevel level,
            std::string& error);

        FeatureLevel Level() const noexcept;

        bool UpdateChart(
            const std::string& workspaceId,
            const std::string& title,
            const std::string& seriesId,
            const ChartMarketSource& source,
            std::string& error);

        IndicatorWorkspaceSnapshot Snapshot() const;

        const std::vector<indicators::IndicatorSpec>& Specs() const noexcept
        {
            return specs_;
        }

    private:
        IndicatorModule indicators_;
        IndicatorRenderAdapter adapter_;
        ChartWorkspaceModule workspace_;
        std::vector<indicators::IndicatorSpec> specs_;
        FeatureLevel level_ = FeatureLevel::Off;
        std::string error_;
    };

    std::vector<indicators::IndicatorSpec>
    InitialIndicatorSpecs();
}
