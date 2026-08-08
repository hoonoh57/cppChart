#include "../core/indicator_engine.h"
#include "../core/vwap_indicator.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    using trading::Bar;
    using trading::EpochMillis;
    using trading::PriceWon;
    using trading::TradingDateYmd;
    using trading::Volume;
    using trading::indicators::IndicatorRegistry;
    using trading::indicators::IndicatorSpec;
    using trading::indicators::IndicatorValue;

    [[noreturn]] void Fail(const char* message)
    {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::exit(1);
    }

    void Check(bool condition, const char* message)
    {
        if (!condition) Fail(message);
    }

    void CheckNear(double actual, double expected, const char* message)
    {
        if (std::fabs(actual - expected) > 1.0e-5) Fail(message);
    }

    Bar MakeBar(
        PriceWon open,
        PriceWon high,
        PriceWon low,
        PriceWon close,
        Volume volume,
        EpochMillis timestampMs,
        TradingDateYmd tradingDateYmd)
    {
        Bar bar;
        bar.open = open;
        bar.high = high;
        bar.low = low;
        bar.close = close;
        bar.volume = volume;
        bar.closeTimestampMs = timestampMs;
        bar.tickCount = 1;
        bar.tradingDateYmd = tradingDateYmd;
        return bar;
    }

    IndicatorSpec VwapSpec(double first = 1.0, double second = 2.0)
    {
        IndicatorSpec spec;
        spec.id = "vwap.main";
        spec.type = "VWAP";
        spec.parameters.emplace("std_dev_1", first);
        spec.parameters.emplace("std_dev_2", second);
        return spec;
    }

    void CheckOutputs(const IndicatorValue& value, const char* message)
    {
        using namespace trading::indicators;
        Check(value.outputCount == 5U, message);
        Check(value.IsReady(VwapValueOutput), message);
        Check(value.IsReady(VwapUpper1Output), message);
        Check(value.IsReady(VwapLower1Output), message);
        Check(value.IsReady(VwapUpper2Output), message);
        Check(value.IsReady(VwapLower2Output), message);
    }

    void CheckParity(
        const IndicatorValue& actual,
        const IndicatorValue& expected,
        bool expectedReplacement,
        const char* message)
    {
        Check(actual.timestampMs == expected.timestampMs, message);
        Check(actual.outputCount == expected.outputCount, message);
        Check(actual.readyMask == expected.readyMask, message);
        Check(actual.replaced == expectedReplacement, message);
        Check(actual.fault == expected.fault, message);
        for (std::size_t index = 0; index < actual.outputCount; ++index) {
            const double left = actual.Value(index);
            const double right = expected.Value(index);
            if (std::isnan(left) || std::isnan(right)) {
                Check(std::isnan(left) && std::isnan(right), message);
            }
            else {
                CheckNear(left, right, message);
            }
        }
    }

    IndicatorValue BatchLast(
        IndicatorRegistry& registry,
        const std::vector<Bar>& bars)
    {
        std::string error;
        auto instance = registry.Create(VwapSpec(), error);
        Check(instance.IsValid(), "VWAP comparison instance creation failed");
        std::vector<IndicatorValue> output;
        Check(trading::indicators::CalculateBatch(
                  instance, bars, output, error),
              "VWAP comparison batch failed");
        return output.back();
    }
}

int main()
{
    using namespace trading::indicators;

    IndicatorRegistry registry;
    Check(RegisterVwapIndicator(registry),
          "VWAP registration must succeed");
    Check(!RegisterVwapIndicator(registry),
          "duplicate VWAP registration must fail");

    std::string error;
    Check(!registry.Create(VwapSpec(-1.0, 2.0), error).IsValid(),
          "negative VWAP deviation must fail");

    IndicatorSpec incomplete;
    incomplete.id = "vwap.incomplete";
    incomplete.type = "VWAP";
    incomplete.parameters.emplace("std_dev_1", 1.0);
    Check(!registry.Create(incomplete, error).IsValid(),
          "incomplete VWAP parameters must fail");

    const std::vector<Bar> bars = {
        MakeBar(10, 12, 8, 10, 100, 1000, 20260803),
        MakeBar(12, 15, 9, 12, 300, 2000, 20260803),
        MakeBar(13, 16, 10, 13, 0, 3000, 20260803),
        MakeBar(20, 22, 18, 20, 50, 4000, 20260804)
    };

    auto batchInstance = registry.Create(VwapSpec(), error);
    std::vector<IndicatorValue> batch;
    Check(trading::indicators::CalculateBatch(
              batchInstance, bars, batch, error),
          "VWAP batch calculation failed");
    Check(batch.size() == bars.size(), "VWAP batch size mismatch");

    for (const IndicatorValue& value : batch) {
        CheckOutputs(
            value,
            "VWAP must publish Value/Upper1/Lower1/Upper2/Lower2");
    }

    const double deviation = std::sqrt(0.75);
    CheckNear(batch[0].Value(VwapValueOutput), 10.0,
              "first VWAP value mismatch");
    CheckNear(batch[1].Value(VwapValueOutput), 11.5,
              "weighted VWAP value mismatch");
    CheckNear(batch[1].Value(VwapUpper1Output), 11.5 + deviation,
              "weighted VWAP upper band mismatch");
    CheckNear(batch[2].Value(VwapValueOutput), 11.5,
              "zero-volume bar must preserve VWAP");
    CheckNear(batch[3].Value(VwapValueOutput), 20.0,
              "trading-date change must reset VWAP");

    auto incremental = registry.Create(VwapSpec(), error);
    for (std::size_t index = 0; index < bars.size(); ++index) {
        CheckParity(
            incremental.Update(bars[index]),
            batch[index],
            false,
            "VWAP batch/incremental output parity mismatch");
    }

    auto noVolume = registry.Create(VwapSpec(), error);
    const IndicatorValue missing = noVolume.Update(
        MakeBar(10, 12, 8, 10, 0, 1000, 20260803));
    CheckOutputs(missing, "zero-volume VWAP output frame mismatch");
    for (std::size_t index = 0; index < missing.outputCount; ++index) {
        Check(std::isnan(missing.Value(index)),
              "zero-volume VWAP must publish NaN");
    }

    auto live = registry.Create(VwapSpec(), error);
    live.Update(bars[0]);
    live.Update(bars[1]);

    const Bar replacement =
        MakeBar(14, 16, 12, 14, 300, 2000, 20260803);
    const IndicatorValue replacementExpected =
        BatchLast(registry, { bars[0], replacement });
    const IndicatorValue replacementActual = live.Update(replacement);
    Check(replacementActual.replaced,
          "same-timestamp VWAP update must replace the live tail");
    CheckParity(
        replacementActual,
        replacementExpected,
        true,
        "VWAP replacement parity mismatch");

    const Bar nextSession =
        MakeBar(30, 33, 27, 30, 25, 3000, 20260804);
    const IndicatorValue nextExpected =
        BatchLast(registry, { bars[0], replacement, nextSession });
    CheckParity(
        live.Update(nextSession),
        nextExpected,
        false,
        "VWAP next-session parity mismatch");

    const IndicatorValue backward = live.Update(
        MakeBar(31, 34, 28, 31, 10, 2500, 20260804));
    Check(backward.fault == IndicatorFault::TimestampMovedBackward,
          "backward VWAP timestamp must fail closed");

    const IndicatorValue missingDate = live.Update(
        MakeBar(31, 34, 28, 31, 10, 3000, 0));
    Check(missingDate.fault == IndicatorFault::InvalidInput,
          "missing VWAP TradingDate must fail closed");

    const IndicatorValue invalidDate = live.Update(
        MakeBar(31, 34, 28, 31, 10, 3000, 20260230));
    Check(invalidDate.fault == IndicatorFault::InvalidInput,
          "invalid VWAP TradingDate must fail closed");

    const IndicatorValue recovered = live.Update(
        MakeBar(31, 34, 28, 31, 10, 3000, 20260804));
    Check(recovered.replaced,
          "VWAP must preserve state after rejected input");
    Check(live.RetainedBytes() > 0U,
          "VWAP retained-byte metric must be positive");

    live.Reset();
    CheckNear(live.Update(bars[0]).Value(VwapValueOutput), 10.0,
              "VWAP reset mismatch");

    std::puts("[PASS] vwap_indicator_tests");
    return 0;
}
