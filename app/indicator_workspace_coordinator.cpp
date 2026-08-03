#include "indicator_workspace_coordinator.h"

#include <utility>

namespace trading::app
{
    bool IndicatorWorkspaceCoordinator::Configure(
        const std::vector<indicators::IndicatorSpec>& specs,
        std::string& error)
    {
        IndicatorRenderPlan plan;
        if (!BuildDefaultIndicatorRenderPlan(specs, plan, error)) {
            error_ = error;
            return false;
        }

        IndicatorRenderAdapter candidateAdapter;
        if (!candidateAdapter.Configure(plan, error)) {
            error_ = error;
            return false;
        }
        if (!indicators_.Configure(specs, error)) {
            error_ = error;
            return false;
        }

        adapter_ = std::move(candidateAdapter);
        specs_ = specs;
        error_.clear();
        error.clear();
        return true;
    }

    bool IndicatorWorkspaceCoordinator::SetLevel(
        FeatureLevel level,
        std::string& error)
    {
        if (!indicators_.SetLevel(level, error)) {
            error_ = error;
            return false;
        }

        level_ = level;
        if (level == FeatureLevel::Off) {
            adapter_.ClearCache();
        }
        error_.clear();
        error.clear();
        return true;
    }

    FeatureLevel IndicatorWorkspaceCoordinator::Level() const noexcept
    {
        return level_;
    }

    bool IndicatorWorkspaceCoordinator::UpdateChart(
        const std::string& workspaceId,
        const std::string& title,
        const std::string& seriesId,
        const ChartMarketSource& source,
        std::string& error)
    {
        if (specs_.empty()) {
            error = "indicator workspace configuration is empty";
            error_ = error;
            return false;
        }

        if (
            level_ == FeatureLevel::Visible ||
            level_ == FeatureLevel::Active)
        {
            IndicatorMarketSource indicatorSource;
            indicatorSource.symbol = seriesId;
            indicatorSource.completedBars = source.completedBars;
            indicatorSource.liveBar = source.liveBar;
            indicatorSource.hasLiveBar = source.hasLiveBar;
            indicatorSource.revision = source.revision;
            indicatorSource.completedRevision = source.completedRevision;

            if (!indicators_.Update(indicatorSource, error)) {
                error_ = error;
                return false;
            }

            const IndicatorModuleSnapshot indicatorSnapshot =
                indicators_.Snapshot();
            if (!workspace_.UpdateMarketChart(
                    workspaceId,
                    title,
                    seriesId,
                    source,
                    indicatorSnapshot,
                    adapter_,
                    error))
            {
                error_ = error;
                return false;
            }
        }
        else {
            if (!workspace_.UpdateMarketChart(
                    workspaceId,
                    title,
                    seriesId,
                    source,
                    error))
            {
                error_ = error;
                return false;
            }
        }

        error_.clear();
        error.clear();
        return true;
    }

    IndicatorWorkspaceSnapshot
    IndicatorWorkspaceCoordinator::Snapshot() const
    {
        IndicatorWorkspaceSnapshot result;
        result.level = level_;
        result.indicators = indicators_.Snapshot();
        result.workspace = workspace_.Snapshot();
        result.renderPlanRevision = adapter_.PlanRevision();
        result.renderAdapterRetainedBytes = adapter_.RetainedBytes();
        result.error = error_;
        return result;
    }

    std::vector<indicators::IndicatorSpec>
    InitialIndicatorSpecs()
    {
        std::vector<indicators::IndicatorSpec> specs;

        indicators::IndicatorSpec sma;
        sma.id = "sma.20";
        sma.type = "SMA";
        sma.parameters.emplace("period", 20.0);
        specs.push_back(sma);

        indicators::IndicatorSpec jma;
        jma.id = "jma.20";
        jma.type = "JMA";
        jma.parameters.emplace("period", 20.0);
        jma.parameters.emplace("phase", 0.0);
        jma.parameters.emplace("power", 2.0);
        specs.push_back(jma);

        indicators::IndicatorSpec vwap;
        vwap.id = "vwap.session";
        vwap.type = "VWAP";
        vwap.parameters.emplace("std_dev_1", 1.0);
        vwap.parameters.emplace("std_dev_2", 2.0);
        specs.push_back(vwap);

        indicators::IndicatorSpec obv;
        obv.id = "obv.20";
        obv.type = "OBV";
        obv.parameters.emplace("signal_period", 20.0);
        specs.push_back(obv);

        indicators::IndicatorSpec adx;
        adx.id = "adx.14";
        adx.type = "ADX";
        adx.parameters.emplace("period", 14.0);
        specs.push_back(adx);

        return specs;
    }
}
