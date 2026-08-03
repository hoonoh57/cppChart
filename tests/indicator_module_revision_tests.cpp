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
        trading::EpochMillis timestampMs)
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

    trading::app::IndicatorMarketSource MakeSource()
    {
        trading::app::IndicatorMarketSource source;
        source.symbol = "005930";
        source.completedBars =
            std::make_shared<const std::vector<trading::Bar>>(
                std::vector<trading::Bar>{
                    MakeBar(10, 1000),
                    MakeBar(20, 2000),
                    MakeBar(30, 3000)
                });
        source.liveBar = MakeBar(40, 4000);
        source.hasLiveBar = true;
        source.revision = 1;
        source.completedRevision = 1;
        return source;
    }
}

int main()
{
    using namespace trading::app;
    using namespace trading::indicators;

    IndicatorSpec sma;
    sma.id = "sma.fast";
    sma.type = "SMA";
    sma.parameters.emplace("period", 3.0);

    IndicatorModule module;
    std::string error;
    Check(module.Configure({ sma }, error),
          "indicator configuration failed");
    Check(module.SetLevel(FeatureLevel::Visible, error),
          "indicator Visible transition failed");

    const IndicatorMarketSource source = MakeSource();
    Check(module.Update(source, error),
          "initial indicator update failed");
    const IndicatorModuleSnapshot first = module.Snapshot();
    Check(first.calculationRevision > 0,
          "first calculation revision must be positive");

    Check(module.Update(source, error),
          "unchanged source update failed");
    const IndicatorModuleSnapshot unchanged = module.Snapshot();
    Check(
        unchanged.calculationRevision == first.calculationRevision,
        "unchanged source must not advance calculation revision");

    Check(module.SetLevel(FeatureLevel::Off, error),
          "indicator Off transition failed");
    const IndicatorModuleSnapshot off = module.Snapshot();
    Check(
        off.calculationRevision > first.calculationRevision,
        "Off invalidation must advance calculation revision");

    Check(module.SetLevel(FeatureLevel::Visible, error),
          "indicator reactivation failed");
    Check(module.Update(source, error),
          "reactivated indicator rebuild failed");
    const IndicatorModuleSnapshot rebuilt = module.Snapshot();
    Check(
        rebuilt.calculationRevision > off.calculationRevision,
        "rebuilt indicator output must advance calculation revision");

    std::puts("[PASS] indicator_module_revision_tests");
    return 0;
}
