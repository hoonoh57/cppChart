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

    trading::Bar MakeBar(
        trading::EpochMillis timestamp,
        trading::PriceWon close,
        trading::Volume volume)
    {
        trading::Bar bar;
        bar.closeTimestampMs = timestamp;
        bar.open = close - 10;
        bar.high = close + 20;
        bar.low = close - 30;
        bar.close = close;
        bar.volume = volume;
        bar.tickCount = 1;
        bar.tradingDateYmd = 20260803;
        return bar;
    }

    trading::app::ChartMarketSource MarketSource()
    {
        trading::app::ChartMarketSource source;
        source.completedBars =
            std::make_shared<const std::vector<trading::Bar>>(
                std::vector<trading::Bar>{
                    MakeBar(1000, 10000, 100),
                    MakeBar(2000, 10100, 200)
                });
        source.liveBar = MakeBar(3000, 10200, 300);
        source.hasLiveBar = true;
        source.barCount = 3;
        source.revision = 10;
        source.completedRevision = 4;
        source.liveRevision = 10;
        return source;
    }

    std::shared_ptr<const std::vector<
        trading::indicators::IndicatorValue>> CompletedValues()
    {
        auto values = std::make_shared<
            std::vector<trading::indicators::IndicatorValue>>();
        trading::indicators::IndicatorValue first;
        first.timestampMs = 1000;
        first.SetOutput(0, 10000.0);
        values->push_back(first);

        trading::indicators::IndicatorValue second;
        second.timestampMs = 2000;
        second.SetOutput(0, 10050.0);
        values->push_back(second);
        return values;
    }

    trading::app::IndicatorModuleSnapshot IndicatorSnapshot(
        std::shared_ptr<const std::vector<
            trading::indicators::IndicatorValue>> completed,
        double liveValue,
        std::uint64_t calculationRevision,
        trading::app::FeatureLevel level =
            trading::app::FeatureLevel::Visible)
    {
        using namespace trading::app;

        IndicatorModuleSnapshot snapshot;
        snapshot.state = level == FeatureLevel::Off
            ? IndicatorModuleState::Empty
            : IndicatorModuleState::Ready;
        snapshot.level = level;
        snapshot.symbol = "005930";
        snapshot.sourceRevision = 10;
        snapshot.completedRevision = 4;
        snapshot.calculationRevision = calculationRevision;

        if (level != FeatureLevel::Off) {
            IndicatorSeriesSnapshot series;
            series.spec.id = "sma.fast";
            series.spec.type = "SMA";
            series.completedValues = std::move(completed);
            series.hasLiveValue = true;
            series.liveValue.timestampMs = 3000;
            series.liveValue.SetOutput(0, liveValue);
            snapshot.series.push_back(std::move(series));
        }
        return snapshot;
    }

    trading::app::IndicatorRenderPlan RenderPlan(
        float width = 1.5f)
    {
        using namespace trading::app;

        IndicatorRenderPlan plan;
        IndicatorOutputBinding binding;
        binding.indicatorId = "sma.fast";
        binding.outputIndex = 0;
        binding.kind = IndicatorRenderKind::Line;
        binding.paneId = "price";
        binding.seriesId = "indicator.sma.fast.value";
        binding.label = "SMA 3";
        binding.width = width;
        plan.bindings.push_back(binding);
        return plan;
    }

    const trading::render::LineSeries& IndicatorLine(
        const trading::app::ChartWorkspaceSnapshot& snapshot)
    {
        Check(snapshot.document != nullptr,
              "workspace document is missing");
        const auto& lines = snapshot.document->panes[0].lines;
        Check(lines.size() == 1U,
              "workspace indicator line count mismatch");
        return lines[0];
    }
}

int main()
{
    using namespace trading::app;

    const ChartMarketSource market = MarketSource();
    const auto completed = CompletedValues();

    IndicatorRenderAdapter adapter;
    std::string error;
    Check(adapter.Configure(RenderPlan(), error),
          "workspace indicator render plan configuration failed");

    ChartWorkspaceModule workspace;
    IndicatorModuleSnapshot firstIndicators =
        IndicatorSnapshot(completed, 10100.0, 20);
    Check(workspace.NeedsUpdate(
              market.revision,
              firstIndicators,
              adapter),
          "empty indicator-aware workspace must need an update");
    Check(workspace.UpdateMarketChart(
              "main",
              "005930",
              "005930",
              market,
              firstIndicators,
              adapter,
              error),
          "indicator-aware workspace update failed");

    const ChartWorkspaceSnapshot first = workspace.Snapshot();
    Check(first.state == ChartWorkspaceState::Ready,
          "indicator-aware workspace must be Ready");
    Check(first.paneCount == 2U,
          "price overlay must not create an extra pane");
    Check(first.seriesCount == 3U,
          "market candles, volume, and SMA line must be present");
    Check(first.indicatorRevision != 0U,
          "workspace must retain composite indicator revision");
    Check(!workspace.NeedsUpdate(
              market.revision,
              firstIndicators,
              adapter),
          "unchanged indicator snapshot and plan must not rebuild");
    const auto firstPrefix =
        IndicatorLine(first).points.SharedPrefix();
    Check(firstPrefix.get() != nullptr && firstPrefix->size() == 2U,
          "workspace must publish shared completed indicator points");
    Check(IndicatorLine(first).points.HasLiveTail(),
          "workspace indicator line must publish a live tail");

    IndicatorModuleSnapshot liveReplacement =
        IndicatorSnapshot(completed, 10150.0, 21);
    Check(workspace.NeedsUpdate(
              market.revision,
              liveReplacement,
              adapter),
          "indicator calculation revision must invalidate workspace");
    Check(workspace.UpdateMarketChart(
              "main",
              "005930",
              "005930",
              market,
              liveReplacement,
              adapter,
              error),
          "indicator-only live update must rebuild workspace document");
    const ChartWorkspaceSnapshot second = workspace.Snapshot();
    Check(second.document != first.document,
          "indicator revision change must publish a new document");
    Check(second.sourceRevision == first.sourceRevision,
          "indicator-only update must preserve market source revision");
    Check(second.indicatorRevision != first.indicatorRevision,
          "indicator-only update must advance composite revision");
    Check(
        IndicatorLine(second).points.SharedPrefix().get() ==
            firstPrefix.get(),
        "indicator-only update must reuse completed render points");
    Check(IndicatorLine(second).points.LiveTail().value == 10150.0,
          "indicator live-tail replacement value mismatch");

    Check(adapter.Configure(RenderPlan(2.5f), error),
          "updated indicator render plan configuration failed");
    Check(workspace.NeedsUpdate(
              market.revision,
              liveReplacement,
              adapter),
          "render-plan revision must invalidate workspace");
    Check(workspace.UpdateMarketChart(
              "main",
              "005930",
              "005930",
              market,
              liveReplacement,
              adapter,
              error),
          "render-plan-only workspace update failed");
    const ChartWorkspaceSnapshot replanned = workspace.Snapshot();
    Check(replanned.indicatorRevision != second.indicatorRevision,
          "render plan revision must invalidate workspace document");
    Check(IndicatorLine(replanned).width == 2.5f,
          "updated render plan width was not applied");

    IndicatorModuleSnapshot offIndicators =
        IndicatorSnapshot(nullptr, 0.0, 22, FeatureLevel::Off);
    Check(workspace.NeedsUpdate(
              market.revision,
              offIndicators,
              adapter),
          "indicator execution-level change must invalidate workspace");
    Check(workspace.UpdateMarketChart(
              "main",
              "005930",
              "005930",
              market,
              offIndicators,
              adapter,
              error),
          "Off indicator snapshot must rebuild a market-only document");
    const ChartWorkspaceSnapshot marketOnly = workspace.Snapshot();
    Check(marketOnly.seriesCount == 2U,
          "Off indicator snapshot must remove indicator contributions");
    Check(marketOnly.document->panes[0].lines.empty(),
          "Off indicator snapshot must remove price overlays");
    Check(workspace.NeedsUpdate(
              market.revision,
              liveReplacement,
              adapter),
          "Visible indicator reactivation must invalidate market-only document");

    IndicatorRenderPlan missingPlan = RenderPlan();
    missingPlan.bindings[0].indicatorId = "missing";
    Check(adapter.Configure(missingPlan, error),
          "missing-source plan must configure structurally");
    Check(!workspace.UpdateMarketChart(
              "main",
              "005930",
              "005930",
              market,
              liveReplacement,
              adapter,
              error),
          "missing indicator binding must fail workspace update");
    const ChartWorkspaceSnapshot failed = workspace.Snapshot();
    Check(failed.state == ChartWorkspaceState::Error,
          "indicator contribution fault must move workspace to Error");
    Check(failed.document == marketOnly.document,
          "failed indicator contribution must retain last good document");

    std::puts("[PASS] chart_workspace_indicator_tests");
    return 0;
}
