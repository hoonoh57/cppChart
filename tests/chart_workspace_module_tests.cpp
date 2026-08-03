#include "../app/chart_workspace_module.h"

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
        bar.open = close - 100;
        bar.high = close + 200;
        bar.low = close - 300;
        bar.close = close;
        bar.volume = volume;
        bar.tickCount = 1;
        return bar;
    }

    std::vector<trading::Bar> MakeBars()
    {
        return {
            MakeBar(1000, 10000, 10),
            MakeBar(2000, 10100, 20),
            MakeBar(3000, 10200, 30)
        };
    }

    trading::app::ChartMarketSource MakeSource(
        const std::vector<trading::Bar>& bars,
        std::uint64_t revision,
        std::uint64_t completedRevision)
    {
        trading::app::ChartMarketSource source;
        auto completed = std::make_shared<std::vector<trading::Bar>>();
        if (bars.size() > 1) {
            completed->assign(bars.begin(), bars.end() - 1);
        }
        source.completedBars = completed;
        source.liveBar = bars.back();
        source.hasLiveBar = true;
        source.barCount = bars.size();
        source.revision = revision;
        source.completedRevision = completedRevision;
        source.liveRevision = revision;
        return source;
    }

    void TestBuildAndImmutableSnapshot()
    {
        trading::app::ChartWorkspaceModule module;
        std::string error;
        const std::vector<trading::Bar> bars = MakeBars();
        trading::app::ChartMarketSource source =
            MakeSource(bars, 7, 1);

        Check(module.NeedsUpdate(7),
              "empty workspace must need an update");
        Check(module.UpdateMarketChart(
                  "main",
                  "000660",
                  "000660",
                  source,
                  error),
              "chart workspace build failed");

        const trading::app::ChartWorkspaceSnapshot first =
            module.Snapshot();
        Check(first.state == trading::app::ChartWorkspaceState::Ready,
              "workspace state must be Ready");
        Check(first.document != nullptr,
              "workspace document is missing");
        Check(first.paneCount == 2,
              "market chart must contain price and volume panes");
        Check(first.seriesCount == 2,
              "market chart must contain two standard series");
        Check(first.document->panes[0].candles[0].bars.size() == 3,
              "workspace candle count mismatch");
        Check(first.document->panes[0].candles[0].bars.SharedPrefix() ==
                  source.completedBars,
              "workspace must retain the immutable completed history pointer");
        Check(first.document->panes[0].candles[0].bars.HasLiveTail(),
              "workspace must publish a separate live candle tail");
        Check(!module.NeedsUpdate(7),
              "unchanged workspace must not rebuild");

        Check(module.UpdateMarketChart(
                  "main",
                  "000660",
                  "000660",
                  source,
                  error),
              "idempotent workspace update failed");
        const trading::app::ChartWorkspaceSnapshot second =
            module.Snapshot();
        Check(second.document == first.document,
              "unchanged update must retain immutable document");

        trading::app::ChartMarketSource liveChanged = source;
        liveChanged.liveBar.close = 10300;
        liveChanged.liveBar.high = 10400;
        liveChanged.revision = 8;
        liveChanged.liveRevision = 8;

        Check(module.UpdateMarketChart(
                  "main",
                  "000660",
                  "000660",
                  liveChanged,
                  error),
              "live-tail workspace update failed");
        const trading::app::ChartWorkspaceSnapshot third =
            module.Snapshot();
        Check(third.document != first.document,
              "changed live revision must publish a new document");
        Check(third.documentRevision > first.documentRevision,
              "document revision must increase");
        Check(third.document->panes[0].candles[0].bars.SharedPrefix() ==
                  source.completedBars,
              "live tick must reuse completed candle history");
        Check(third.document->panes[1].histograms[0].points.SharedPrefix() ==
                  first.document->panes[1].histograms[0].points.SharedPrefix(),
              "live tick must reuse completed volume history");
        Check(third.document->panes[0].candles[0].bars.back().close == 10300,
              "live candle tail was not updated");
        Check(third.retainedBytes > 0,
              "ready workspace must report retained bytes");

        std::vector<trading::Bar> nextBars = bars;
        nextBars.back() = liveChanged.liveBar;
        nextBars.push_back(MakeBar(4000, 10400, 40));
        trading::app::ChartMarketSource nextMinute =
            MakeSource(nextBars, 9, 2);

        Check(module.UpdateMarketChart(
                  "main",
                  "000660",
                  "000660",
                  nextMinute,
                  error),
              "new-minute workspace update failed");
        const trading::app::ChartWorkspaceSnapshot fourth =
            module.Snapshot();
        Check(fourth.completedRevision == 2,
              "new minute must advance completed revision");
        Check(fourth.document->panes[0].candles[0].bars.SharedPrefix() ==
                  nextMinute.completedBars,
              "new minute must publish the new completed history");
        Check(fourth.document->panes[1].histograms[0].points.SharedPrefix() !=
                  third.document->panes[1].histograms[0].points.SharedPrefix(),
              "new minute must rebuild completed volume history once");
    }

    void TestExecutionLevels()
    {
        trading::app::ChartWorkspaceModule module;
        std::string error;
        const trading::app::ChartMarketSource source =
            MakeSource(MakeBars(), 1, 1);

        Check(module.UpdateMarketChart(
                  "main", "000660", "000660", source, error),
              "initial workspace build failed");
        const auto ready = module.Snapshot();

        Check(module.SetLevel(
                  trading::app::FeatureLevel::Standby,
                  error),
              "Standby transition failed");
        const auto standby = module.Snapshot();
        Check(standby.document == ready.document,
              "Standby must retain the immutable render document");
        Check(!module.NeedsUpdate(2),
              "Standby must suppress render-document rebuilds");

        trading::app::ChartMarketSource changed = source;
        changed.revision = 2;
        changed.liveRevision = 2;
        Check(!module.UpdateMarketChart(
                  "main", "000660", "000660", changed, error),
              "Standby must reject document updates");

        Check(module.SetLevel(
                  trading::app::FeatureLevel::Visible,
                  error),
              "Visible transition failed");
        Check(module.NeedsUpdate(2),
              "Visible must detect changed source revision");

        Check(module.SetLevel(
                  trading::app::FeatureLevel::Off,
                  error),
              "Off transition failed");
        const auto off = module.Snapshot();
        Check(off.state == trading::app::ChartWorkspaceState::Empty,
              "Off workspace must become Empty");
        Check(off.document == nullptr,
              "Off workspace must release the render document");
        Check(off.retainedBytes == 0,
              "Off workspace must report no retained document bytes");
    }

    void TestValidationFailure()
    {
        trading::app::ChartWorkspaceModule module;
        std::string error;
        std::vector<trading::Bar> invalid = MakeBars();
        invalid[2].closeTimestampMs = invalid[1].closeTimestampMs;
        const trading::app::ChartMarketSource source =
            MakeSource(invalid, 1, 1);

        Check(!module.UpdateMarketChart(
                  "main",
                  "000660",
                  "000660",
                  source,
                  error),
              "invalid render document must fail");
        Check(module.Snapshot().state ==
                  trading::app::ChartWorkspaceState::Error,
              "validation failure must move workspace to Error");
        Check(!module.Snapshot().error.empty(),
              "validation failure must retain an error");
    }
}

int main()
{
    TestBuildAndImmutableSnapshot();
    TestExecutionLevels();
    TestValidationFailure();
    std::puts("[PASS] chart_workspace_module_tests");
    return 0;
}
