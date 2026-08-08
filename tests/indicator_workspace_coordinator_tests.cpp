#include "../app/indicator_workspace_coordinator.h"

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
        int index,
        trading::EpochMillis timestampMs,
        trading::TradingDateYmd tradingDateYmd = 20260803)
    {
        const trading::PriceWon close = 10000 + index * 10;
        trading::Bar bar;
        bar.open = close - 5;
        bar.high = close + 15;
        bar.low = close - 20;
        bar.close = close;
        bar.volume = 100 + index * 10;
        bar.closeTimestampMs = timestampMs;
        bar.tickCount = 1;
        bar.tradingDateYmd = tradingDateYmd;
        return bar;
    }

    trading::app::ChartMarketSource Source(
        std::uint64_t revision,
        trading::PriceWon liveOffset = 0)
    {
        auto completed = std::make_shared<std::vector<trading::Bar>>();
        completed->reserve(30);
        for (int index = 0; index < 30; ++index) {
            completed->push_back(MakeBar(
                index,
                1000 + static_cast<trading::EpochMillis>(index) * 1000));
        }

        trading::app::ChartMarketSource source;
        source.completedBars = completed;
        source.liveBar = MakeBar(30, 31000);
        source.liveBar.close += liveOffset;
        source.liveBar.high += liveOffset;
        source.liveBar.open += liveOffset;
        source.liveBar.low += liveOffset;
        source.hasLiveBar = true;
        source.barCount = completed->size() + 1U;
        source.revision = revision;
        source.completedRevision = 1;
        source.liveRevision = revision;
        return source;
    }

    bool HasReference(
        const trading::render::RenderDocument& document,
        const std::string& id)
    {
        for (const auto& pane : document.panes) {
            for (const auto& reference : pane.referenceLines) {
                if (reference.id == id) return true;
            }
        }
        return false;
    }
}

int main()
{
    using namespace trading::app;

    IndicatorWorkspaceCoordinator coordinator;
    std::string error;
    const auto initialSpecs = InitialIndicatorSpecs();
    Check(initialSpecs.size() == 5U,
          "initial indicator specification count mismatch");
    Check(coordinator.Configure(initialSpecs, error),
          "initial indicator workspace configuration failed");
    Check(coordinator.SetLevel(FeatureLevel::Visible, error),
          "indicator workspace Visible transition failed");

    const ChartMarketSource firstSource = Source(1);
    Check(coordinator.UpdateChart(
              "main",
              "005930",
              "005930",
              firstSource,
              error),
          "indicator workspace initial chart update failed");

    const IndicatorWorkspaceSnapshot first = coordinator.Snapshot();
    Check(first.level == FeatureLevel::Visible,
          "indicator workspace level mismatch");
    Check(first.indicators.state == IndicatorModuleState::Ready,
          "indicator workspace calculation must be Ready");
    Check(first.indicators.series.size() == 5U,
          "indicator workspace calculated series count mismatch");
    Check(first.workspace.state == ChartWorkspaceState::Ready,
          "indicator workspace chart must be Ready");
    Check(first.workspace.document != nullptr,
          "indicator workspace document is missing");
    Check(first.workspace.paneCount >= 5U,
          "initial indicators must contribute price and lower panes");
    Check(first.workspace.seriesCount > 10U,
          "initial indicators must contribute standard render series");
    Check(HasReference(
              *first.workspace.document,
              "indicator.adx.14.reference.20"),
          "default ADX 20 reference line is missing");
    Check(HasReference(
              *first.workspace.document,
              "indicator.jma.20.slope.zero"),
          "default JMA slope zero reference line is missing");
    Check(first.renderPlanRevision > 0U,
          "indicator render plan revision must be positive");
    Check(first.renderAdapterRetainedBytes > 0U,
          "indicator render adapter retained bytes must be positive");

    const auto firstCompleted =
        first.indicators.series[0].completedValues.get();
    const auto firstDocument = first.workspace.document;

    const ChartMarketSource replacementSource = Source(2, 25);
    Check(coordinator.UpdateChart(
              "main",
              "005930",
              "005930",
              replacementSource,
              error),
          "indicator workspace live replacement failed");
    const IndicatorWorkspaceSnapshot replaced = coordinator.Snapshot();
    Check(replaced.workspace.document != firstDocument,
          "indicator live revision must publish a new chart document");
    Check(replaced.indicators.series[0].completedValues.get() ==
              firstCompleted,
          "indicator live revision must reuse completed calculation history");
    Check(replaced.indicators.metrics.mergedEventCount == 1U,
          "indicator live revision merged-event metric mismatch");

    Check(coordinator.SetLevel(FeatureLevel::Standby, error),
          "indicator workspace Standby transition failed");
    Check(coordinator.UpdateChart(
              "main",
              "005930",
              "005930",
              replacementSource,
              error),
          "Standby indicator workspace must publish market-only chart");
    const IndicatorWorkspaceSnapshot standby = coordinator.Snapshot();
    Check(standby.workspace.seriesCount == 2U,
          "Standby indicator workspace must remove indicator rendering work");
    Check(standby.indicators.series.size() == 5U,
          "Standby indicator workspace must preserve calculated state");

    Check(coordinator.SetLevel(FeatureLevel::Active, error),
          "indicator workspace Active transition failed");
    Check(coordinator.UpdateChart(
              "main",
              "005930",
              "005930",
              replacementSource,
              error),
          "Active indicator workspace reactivation failed");
    const IndicatorWorkspaceSnapshot active = coordinator.Snapshot();
    Check(active.workspace.seriesCount > 10U,
          "Active indicator workspace must restore indicator rendering work");

    Check(coordinator.SetLevel(FeatureLevel::Off, error),
          "indicator workspace Off transition failed");
    Check(coordinator.UpdateChart(
              "main",
              "005930",
              "005930",
              replacementSource,
              error),
          "Off indicator workspace must preserve market-only chart");
    const IndicatorWorkspaceSnapshot off = coordinator.Snapshot();
    Check(off.indicators.series.empty(),
          "Off indicator workspace must release calculated state");
    Check(off.workspace.seriesCount == 2U,
          "Off indicator workspace must publish no indicator contributions");

    auto invalidSpecs = initialSpecs;
    invalidSpecs[0].type = "UNKNOWN";
    Check(!coordinator.Configure(invalidSpecs, error),
          "unsupported indicator configuration must fail closed");
    Check(coordinator.Specs().size() == initialSpecs.size() &&
              coordinator.Specs()[0].type == "SMA",
          "failed reconfiguration must preserve the last valid specification");

    Check(coordinator.SetLevel(FeatureLevel::Visible, error),
          "indicator workspace Visible rebuild transition failed");
    Check(coordinator.UpdateChart(
              "main",
              "005930",
              "005930",
              replacementSource,
              error),
          "indicator workspace rebuild after Off failed");
    const auto lastGood = coordinator.Snapshot().workspace.document;

    ChartMarketSource invalidSource = Source(3);
    invalidSource.liveBar.tradingDateYmd = 0;
    Check(!coordinator.UpdateChart(
              "main",
              "005930",
              "005930",
              invalidSource,
              error),
          "invalid VWAP trading date must fail coordinator update");
    const IndicatorWorkspaceSnapshot failed = coordinator.Snapshot();
    Check(failed.workspace.document == lastGood,
          "failed indicator update must retain the last good chart document");
    Check(!failed.error.empty(),
          "failed indicator workspace update must expose an error");

    std::puts("[PASS] indicator_workspace_coordinator_tests");
    return 0;
}
