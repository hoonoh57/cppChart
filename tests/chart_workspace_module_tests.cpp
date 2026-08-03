#include "../app/chart_workspace_module.h"

#include <cstdio>
#include <cstdlib>
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

    void TestBuildAndImmutableSnapshot()
    {
        trading::app::ChartWorkspaceModule module;
        std::string error;
        const std::vector<trading::Bar> bars = MakeBars();

        Check(module.NeedsUpdate(7, 3),
              "empty workspace must need an update");
        Check(module.UpdateMarketChart(
                  "main",
                  "000660",
                  "000660",
                  bars,
                  7,
                  3,
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
        Check(!module.NeedsUpdate(7, 3),
              "unchanged workspace must not rebuild");

        Check(module.UpdateMarketChart(
                  "main",
                  "000660",
                  "000660",
                  bars,
                  7,
                  3,
                  error),
              "idempotent workspace update failed");
        const trading::app::ChartWorkspaceSnapshot second =
            module.Snapshot();
        Check(second.document == first.document,
              "unchanged update must retain immutable document");

        std::vector<trading::Bar> changed = bars;
        changed.back().close = 10300;
        changed.back().high = 10400;
        Check(module.UpdateMarketChart(
                  "main",
                  "000660",
                  "000660",
                  changed,
                  8,
                  3,
                  error),
              "changed workspace update failed");
        const trading::app::ChartWorkspaceSnapshot third =
            module.Snapshot();
        Check(third.document != first.document,
              "changed source revision must publish a new document");
        Check(third.documentRevision > first.documentRevision,
              "document revision must increase");
        Check(third.retainedBytes > 0,
              "ready workspace must report retained bytes");
    }

    void TestExecutionLevels()
    {
        trading::app::ChartWorkspaceModule module;
        std::string error;
        const std::vector<trading::Bar> bars = MakeBars();

        Check(module.UpdateMarketChart(
                  "main", "000660", "000660", bars, 1, 3, error),
              "initial workspace build failed");
        const auto ready = module.Snapshot();

        Check(module.SetLevel(
                  trading::app::FeatureLevel::Standby,
                  error),
              "Standby transition failed");
        const auto standby = module.Snapshot();
        Check(standby.document == ready.document,
              "Standby must retain the immutable render document");
        Check(!module.NeedsUpdate(2, 3),
              "Standby must suppress render-document rebuilds");
        Check(!module.UpdateMarketChart(
                  "main", "000660", "000660", bars, 2, 3, error),
              "Standby must reject document updates");

        Check(module.SetLevel(
                  trading::app::FeatureLevel::Visible,
                  error),
              "Visible transition failed");
        Check(module.NeedsUpdate(2, 3),
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
        invalid[1].closeTimestampMs = invalid[0].closeTimestampMs;

        Check(!module.UpdateMarketChart(
                  "main",
                  "000660",
                  "000660",
                  invalid,
                  1,
                  3,
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
