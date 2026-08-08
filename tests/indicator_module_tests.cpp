#include "../app/indicator_module.h"

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
        trading::PriceWon close,
        trading::Volume volume,
        trading::EpochMillis timestampMs,
        trading::TradingDateYmd tradingDateYmd = 20260803)
    {
        trading::Bar bar;
        bar.open = close;
        bar.high = close + 1;
        bar.low = close - 1;
        bar.close = close;
        bar.volume = volume;
        bar.closeTimestampMs = timestampMs;
        bar.tickCount = 1;
        bar.tradingDateYmd = tradingDateYmd;
        return bar;
    }

    trading::indicators::IndicatorSpec SmaSpec()
    {
        trading::indicators::IndicatorSpec spec;
        spec.id = "sma.fast";
        spec.type = "SMA";
        spec.parameters.emplace("period", 3.0);
        return spec;
    }

    trading::indicators::IndicatorSpec VwapSpec()
    {
        trading::indicators::IndicatorSpec spec;
        spec.id = "vwap.session";
        spec.type = "VWAP";
        spec.parameters.emplace("std_dev_1", 1.0);
        spec.parameters.emplace("std_dev_2", 2.0);
        return spec;
    }

    trading::app::IndicatorMarketSource Source(
        std::shared_ptr<const std::vector<trading::Bar>> completed,
        const trading::Bar& live,
        std::uint64_t revision,
        std::uint64_t completedRevision)
    {
        trading::app::IndicatorMarketSource source;
        source.symbol = "005930";
        source.completedBars = std::move(completed);
        source.liveBar = live;
        source.hasLiveBar = true;
        source.revision = revision;
        source.completedRevision = completedRevision;
        return source;
    }
}

int main()
{
    using namespace trading;
    using namespace trading::app;
    using namespace trading::indicators;

    std::string error;

    IndicatorModule invalidModule;
    IndicatorSpec invalid = SmaSpec();
    invalid.parameters["period"] = 0.0;
    Check(!invalidModule.Configure({ invalid }, error),
          "invalid indicator configuration must fail closed");

    IndicatorModule duplicateModule;
    IndicatorSpec duplicate = VwapSpec();
    duplicate.id = "sma.fast";
    Check(!duplicateModule.Configure({ SmaSpec(), duplicate }, error),
          "duplicate indicator ids must fail closed");

    IndicatorModule module;
    Check(module.Configure({ SmaSpec(), VwapSpec() }, error),
          "valid indicator configuration must succeed");
    Check(module.SetLevel(FeatureLevel::Standby, error),
          "indicator Standby transition must succeed");

    auto completed1 = std::make_shared<const std::vector<Bar>>(
        std::vector<Bar>{
            MakeBar(10, 100, 1000),
            MakeBar(20, 200, 2000),
            MakeBar(30, 300, 3000)
        });
    const Bar live1 = MakeBar(40, 400, 4000);
    IndicatorMarketSource source1 = Source(completed1, live1, 1, 1);

    Check(!module.Update(source1, error),
          "Standby indicator module must not calculate");
    Check(error.find("Standby") != std::string::npos,
          "Standby rejection must identify the execution level");

    Check(module.SetLevel(FeatureLevel::Visible, error),
          "indicator Visible transition must succeed");
    Check(module.Update(source1, error),
          "initial indicator module calculation must succeed");

    IndicatorModuleSnapshot first = module.Snapshot();
    Check(first.state == IndicatorModuleState::Ready,
          "indicator module must become Ready");
    Check(first.symbol == "005930",
          "indicator module symbol mismatch");
    Check(first.series.size() == 2U,
          "indicator module series count mismatch");
    Check(first.metrics.eventCount == 1U,
          "indicator module event metric mismatch");
    Check(first.metrics.symbolCount == 1U,
          "indicator module symbol metric mismatch");
    Check(first.metrics.renderSeriesCount == 6U,
          "SMA plus VWAP output-channel metric mismatch");
    Check(first.metrics.retainedBytes > 0U,
          "indicator module retained-byte metric must be positive");
    Check(first.series[0].completedValues != nullptr &&
              first.series[0].completedValues->size() == 3U,
          "SMA completed output history mismatch");
    Check(first.series[1].completedValues != nullptr &&
              first.series[1].completedValues->size() == 3U,
          "VWAP completed output history mismatch");
    Check(first.series[0].hasLiveValue && first.series[1].hasLiveValue,
          "indicator live-tail outputs must be present");

    const auto smaCompletedPointer = first.series[0].completedValues.get();
    const auto vwapCompletedPointer = first.series[1].completedValues.get();

    IndicatorMarketSource replacement = source1;
    replacement.revision = 2;
    replacement.liveBar = MakeBar(50, 500, 4000);
    Check(module.Update(replacement, error),
          "same-timestamp live-tail replacement must succeed");

    IndicatorModuleSnapshot replaced = module.Snapshot();
    Check(replaced.series[0].completedValues.get() == smaCompletedPointer,
          "completed SMA output pointer must be reused on live-tail updates");
    Check(replaced.series[1].completedValues.get() == vwapCompletedPointer,
          "completed VWAP output pointer must be reused on live-tail updates");
    Check(replaced.series[0].liveValue.replaced,
          "SMA live-tail replacement flag mismatch");
    Check(replaced.series[1].liveValue.replaced,
          "VWAP live-tail replacement flag mismatch");
    Check(replaced.metrics.mergedEventCount == 1U,
          "live-tail merged-event metric mismatch");

    IndicatorMarketSource invalidAdvance = replacement;
    invalidAdvance.revision = 3;
    invalidAdvance.liveBar = MakeBar(60, 600, 5000);
    Check(!module.Update(invalidAdvance, error),
          "live timestamp changed without completed revision must fail");
    Check(error.find("completed revision") != std::string::npos,
          "live timestamp revision fault must be explicit");

    auto completed2 = std::make_shared<const std::vector<Bar>>(
        std::vector<Bar>{
            MakeBar(10, 100, 1000),
            MakeBar(20, 200, 2000),
            MakeBar(30, 300, 3000),
            MakeBar(50, 500, 4000)
        });
    IndicatorMarketSource promoted =
        Source(completed2, MakeBar(60, 600, 5000), 4, 2);
    Check(module.Update(promoted, error),
          "completed-revision promotion must rebuild indicators");

    IndicatorModuleSnapshot rebuilt = module.Snapshot();
    Check(rebuilt.state == IndicatorModuleState::Ready,
          "indicator module must recover after valid rebuild");
    Check(rebuilt.completedRevision == 2U,
          "indicator completed revision mismatch");
    Check(rebuilt.series[0].completedValues->size() == 4U,
          "rebuilt SMA completed output size mismatch");
    Check(rebuilt.series[1].completedValues->size() == 4U,
          "rebuilt VWAP completed output size mismatch");
    Check(rebuilt.series[0].completedValues.get() != smaCompletedPointer,
          "completed revision must replace SMA output history");
    Check(rebuilt.series[1].completedValues.get() != vwapCompletedPointer,
          "completed revision must replace VWAP output history");

    Check(module.SetLevel(FeatureLevel::Off, error),
          "indicator Off transition must succeed");
    IndicatorModuleSnapshot off = module.Snapshot();
    Check(off.state == IndicatorModuleState::Empty,
          "Off indicator module must become Empty");
    Check(off.series.empty(),
          "Off indicator module must release calculated series");
    Check(off.metrics.retainedBytes == 0U,
          "Off indicator module must release retained calculation bytes");

    Check(module.SetLevel(FeatureLevel::Visible, error),
          "indicator reactivation must succeed");
    Check(module.Update(promoted, error),
          "reactivated indicator module must rebuild from source");
    IndicatorModuleSnapshot reactivated = module.Snapshot();
    Check(reactivated.state == IndicatorModuleState::Ready,
          "reactivated indicator module must become Ready");
    Check(reactivated.series.size() == 2U,
          "reactivated indicator series count mismatch");

    std::puts("[PASS] indicator_module_tests");
    return 0;
}
