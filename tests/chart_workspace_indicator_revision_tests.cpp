#include "../app/chart_workspace_module.h"
#include "../app/indicator_render_adapter.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::exit(1);
    }

    void Check(bool condition, const char* message)
    {
        if (!condition) Fail(message);
    }

    trading::Bar Bar(
        trading::EpochMillis timestampMs,
        trading::PriceWon close)
    {
        trading::Bar bar;
        bar.open = close;
        bar.high = close;
        bar.low = close;
        bar.close = close;
        bar.volume = 100;
        bar.closeTimestampMs = timestampMs;
        bar.tickCount = 1;
        bar.tradingDateYmd = 20260803;
        return bar;
    }

    trading::app::ChartMarketSource Market()
    {
        trading::app::ChartMarketSource source;
        source.completedBars =
            std::make_shared<const std::vector<trading::Bar>>(
                std::vector<trading::Bar>{ Bar(1000, 100), Bar(2000, 110) });
        source.liveBar = Bar(3000, 120);
        source.hasLiveBar = true;
        source.barCount = 3;
        source.revision = 5;
        source.completedRevision = 1;
        source.liveRevision = 5;
        return source;
    }

    std::shared_ptr<const std::vector<
        trading::indicators::IndicatorValue>> Completed()
    {
        auto values = std::make_shared<std::vector<
            trading::indicators::IndicatorValue>>();
        for (int index = 0; index < 2; ++index) {
            trading::indicators::IndicatorValue value;
            value.timestampMs = 1000 + index * 1000;
            value.SetOutput(0, 101.0 + index * 10.0);
            values->push_back(value);
        }
        return values;
    }

    trading::app::IndicatorModuleSnapshot Snapshot(
        trading::app::FeatureLevel level,
        std::uint64_t calculationRevision)
    {
        trading::app::IndicatorModuleSnapshot snapshot;
        snapshot.state = trading::app::IndicatorModuleState::Ready;
        snapshot.level = level;
        snapshot.symbol = "005930";
        snapshot.completedRevision = 1;
        snapshot.calculationRevision = calculationRevision;

        trading::app::IndicatorSeriesSnapshot series;
        series.spec.id = "sma.fast";
        series.spec.type = "SMA";
        series.completedValues = Completed();
        series.liveValue.timestampMs = 3000;
        series.liveValue.SetOutput(0, 121.0);
        series.hasLiveValue = true;
        snapshot.series.push_back(std::move(series));
        return snapshot;
    }
}

int main()
{
    using namespace trading::app;

    IndicatorOutputBinding binding;
    binding.indicatorId = "sma.fast";
    binding.outputIndex = 0;
    binding.kind = IndicatorRenderKind::Line;
    binding.paneId = "price";
    binding.seriesId = "indicator.sma.fast";
    binding.label = "SMA";

    IndicatorRenderPlan plan;
    plan.bindings.push_back(binding);

    IndicatorRenderAdapter adapter;
    std::string error;
    Check(adapter.Configure(plan, error),
          "indicator adapter configuration failed");

    ChartWorkspaceModule workspace;
    const ChartMarketSource market = Market();
    IndicatorModuleSnapshot visible =
        Snapshot(FeatureLevel::Visible, 7);

    Check(workspace.NeedsUpdate(
              market.revision,
              visible,
              adapter),
          "empty workspace must need indicator-aware update");
    Check(workspace.UpdateMarketChart(
              "main",
              "005930",
              "005930",
              market,
              visible,
              adapter,
              error),
          "indicator-aware workspace update failed");
    Check(!workspace.NeedsUpdate(
              market.revision,
              visible,
              adapter),
          "unchanged indicator snapshot and plan must not rebuild");

    IndicatorModuleSnapshot standby =
        Snapshot(FeatureLevel::Standby, 7);
    Check(workspace.NeedsUpdate(
              market.revision,
              standby,
              adapter),
          "indicator execution-level change must invalidate workspace");
    Check(workspace.UpdateMarketChart(
              "main",
              "005930",
              "005930",
              market,
              standby,
              adapter,
              error),
          "Standby indicator workspace update failed");
    Check(workspace.Snapshot().document->panes[0].lines.empty(),
          "Standby indicator must remove render contributions");

    Check(workspace.NeedsUpdate(
              market.revision,
              visible,
              adapter),
          "Visible reactivation must invalidate market-only document");

    IndicatorRenderPlan wider = plan;
    wider.bindings[0].width = 2.0f;
    Check(adapter.Configure(wider, error),
          "updated render plan configuration failed");
    Check(workspace.NeedsUpdate(
              market.revision,
              visible,
              adapter),
          "render-plan revision must invalidate workspace");

    std::puts("[PASS] chart_workspace_indicator_revision_tests");
    return 0;
}
