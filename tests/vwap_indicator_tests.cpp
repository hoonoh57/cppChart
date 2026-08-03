#include "../core/indicator_engine.h"
#include "../core/vwap_indicator.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
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

    void CheckNear(double actual, double expected, const char* message)
    {
        if (std::fabs(actual - expected) > 1.0e-5) {
            std::fprintf(
                stderr,
                "[FAIL] %s actual=%.12f expected=%.12f\n",
                message,
                actual,
                expected);
            std::exit(1);
        }
    }

    trading::Bar MakeBar(
        trading::PriceWon open,
        trading::PriceWon high,
        trading::PriceWon low,
        trading::PriceWon close,
        trading::Volume volume,
        trading::EpochMillis timestampMs,
        trading::TradingDateYmd tradingDateYmd)
    {
        trading::Bar bar;
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

    trading::indicators::IndicatorSpec VwapSpec(
        double stdDev1 = 1.0,
        double stdDev2 = 2.0)
    {
        trading::indicators::IndicatorSpec spec;
        spec.id = "vwap.main";
        spec.type = "VWAP";
        spec.parameters.emplace("std_dev_1", stdDev1);
        spec.parameters.emplace("std_dev_2", stdDev2);
        return spec;
    }

    void CheckFiveOutputs(
        const trading::indicators::IndicatorValue& value,
        const char* message)
    {
        using namespace trading::indicators;
        Check(value.outputCount == 5U, message);
        Check(value.IsReady(VwapValueOutput), message);
        Check(value.IsReady(VwapUpper1Output), message);
        Check(value.IsReady(VwapLower1Output), message);
        Check(value.IsReady(VwapUpper2Output), message);
        Check(value.IsReady(VwapLower2Output), message);
    }

    void CheckOutputParity(
        const trading::indicators::IndicatorValue& left,
        const trading::indicators::IndicatorValue& right,
        const char* message)
    {
        Check(left.timestampMs == right.timestampMs, message);
        Check(left.outputCount == right.outputCount, message);
        Check(left.readyMask == right.readyMask, message);
        Check(left.replaced == right.replaced, message);
        Check(left.fault == right.fault, message);
        for (std::size_t index = 0; index < left.outputCount; ++index) {
            const double leftValue = left.Value(index);
            const double rightValue = right.Value(index);
            if (std::isnan(leftValue) || std::isnan(rightValue)) {
                Check(std::isnan(leftValue) && std::isnan(rightValue), message);
            }
            else {
                CheckNear(leftValue, rightValue, message);
            }
        }
    }

    trading::indicators::IndicatorValue CalculateLast(
        trading::indicators::IndicatorRegistry& registry,
        const std::vector<trading::Bar>& bars)
    {
        std::string error;
        trading::indicators::IndicatorInstance instance =
            registry.Create(VwapSpec(), error);
        Check(instance.IsValid(), "VWAP comparison instance creation failed");
        std::vector<trading::indicators::IndicatorValue> output;
        Check(trading::indicators::CalculateBatch(
                  instance,
                  bars,
                  output,
                  error),
              "VWAP comparison batch failed");
        Check(!output.empty(), "VWAP comparison output is empty");
        return output.back();
    }
}

int main()
{
    using namespace trading;
    using namespace trading::indicators;

    IndicatorRegistry registry;
    Check(RegisterVwapIndicator(registry),
          "VWAP registration must succeed");
    Check(!RegisterVwapIndicator(registry),
          "duplicate VWAP registration must fail");

    std::string error;
    Check(!registry.Create(VwapSpec(-1.0, 2.0), error).IsValid(),
          "negative VWAP deviation must fail");
    Check(!registry.Create(VwapSpec(1.0, 101.0), error).IsValid(),
          "VWAP deviation above 100 must fail");
    Check(!registry.Create(
              VwapSpec((std::numeric_limits<double>::infinity)(), 2.0),
              error).IsValid(),
          "non-finite VWAP deviation must fail");

    IndicatorSpec missing;
    missing.id = "vwap.missing";
    missing.type = "VWAP";
    missing.parameters.emplace("std_dev_1", 1.0);
    Check(!registry.Create(missing, error).IsValid(),
          "VWAP missing std_dev_2 must fail");

    IndicatorSpec extra = VwapSpec();
    extra.parameters.emplace("source", 1.0);
    Check(!registry.Create(extra, error).IsValid(),
          "unknown VWAP parameter must fail");

    const std::vector<Bar> bars = {
        MakeBar(10, 12, 8, 10, 100, 1000, 20260803),
        MakeBar(12, 15, 9, 12, 300, 2000, 20260803),
        MakeBar(13, 16, 10, 13, 0, 3000, 20260803),
        MakeBar(20, 22, 18, 20, 50, 4000, 20260804)
    };

    IndicatorInstance batchInstance = registry.Create(VwapSpec(), error);
    Check(batchInstance.IsValid(),
          "batch VWAP instance creation failed");
    std::vector<IndicatorValue> batch;
    Check(CalculateBatch(batchInstance, bars, batch, error),
          "VWAP batch calculation failed");
    Check(batch.size() == bars.size(),
          "VWAP batch output size mismatch");

    for (const IndicatorValue& value : batch) {
        CheckFiveOutputs(value,
                         "VWAP must publish Value/Upper1/Lower1/Upper2/Lower2");
        Check(value.fault == IndicatorFault::None,
              "VWAP batch fault mismatch");
    }

    CheckNear(batch[0].Value(VwapValueOutput), 10.0,
              "first-session VWAP value mismatch");
    CheckNear(batch[0].Value(VwapUpper1Output), 10.0,
              "first-session VWAP upper1 mismatch");

    const double deviation = std::sqrt(0.75);
    CheckNear(batch[1].Value(VwapValueOutput),
              static_cast<double>(static_cast<float>(11.5)),
              "weighted VWAP value mismatch");
    CheckNear(batch[1].Value(VwapUpper1Output),
              static_cast<double>(static_cast<float>(11.5 + deviation)),
              "weighted VWAP upper1 mismatch");
    CheckNear(batch[1].Value(VwapLower1Output),
              static_cast<double>(static_cast<float>(11.5 - deviation)),
              "weighted VWAP lower1 mismatch");
    CheckNear(batch[1].Value(VwapUpper2Output),
              static_cast<double>(static_cast<float>(11.5 + 2.0 * deviation)),
              "weighted VWAP upper2 mismatch");
    CheckNear(batch[1].Value(VwapLower2Output),
              static_cast<double>(static_cast<float>(11.5 - 2.0 * deviation)),
              "weighted VWAP lower2 mismatch");

    CheckNear(batch[2].Value(VwapValueOutput),
              batch[1].Value(VwapValueOutput),
              "zero-volume bar must preserve session VWAP");
    CheckNear(batch[3].Value(VwapValueOutput), 20.0,
              "trading-date change must reset VWAP");
    CheckNear(batch[3].Value(VwapUpper2Output), 20.0,
              "trading-date reset deviation mismatch");

    IndicatorInstance incremental = registry.Create(VwapSpec(), error);
    Check(incremental.IsValid(),
          "incremental VWAP instance creation failed");
    for (std::size_t index = 0; index < bars.size(); ++index) {
        const IndicatorValue value = incremental.Update(bars[index]);
        CheckOutputParity(
            value,
            batch[index],
            "VWAP batch/incremental output parity mismatch");
    }

    IndicatorInstance emptyVolume = registry.Create(VwapSpec(), error);
    const IndicatorValue noTrades = emptyVolume.Update(
        MakeBar(10, 12, 8, 10, 0, 1000, 20260803));
    CheckFiveOutputs(noTrades,
                     "zero-total-volume VWAP must retain five outputs");
    for (std::size_t index = 0; index < noTrades.outputCount; ++index) {
        Check(std::isnan(noTrades.Value(index)),
              "zero-total-volume VWAP outputs must be NaN");
    }

    IndicatorInstance live = registry.Create(VwapSpec(), error);
    Check(live.IsValid(), "live VWAP instance creation failed");
    live.Update(bars[0]);
    live.Update(bars[1]);

    const Bar replacement =
        MakeBar(14, 16, 12, 14, 300, 2000, 20260803);
    const std::vector<Bar> replacementBars = { bars[0], replacement };
    const IndicatorValue expectedReplacement =
        CalculateLast(registry, replacementBars);
    IndicatorValue value = live.Update(replacement);
    Check(value.replaced,
          "same-timestamp VWAP update must replace the live tail");
    CheckOutputParity(
        value,
        expectedReplacement,
        "VWAP live-tail replacement mismatch");

    const Bar nextSession =
        MakeBar(30, 33, 27, 30, 25, 3000, 20260804);
    const std::vector<Bar> nextSessionBars = {
        bars[0], replacement, nextSession };
    const IndicatorValue expectedNextSession =
        CalculateLast(registry, nextSessionBars);
    value = live.Update(nextSession);
    Check(!value.replaced,
          "new VWAP timestamp must append instead of replace");
    CheckOutputParity(
        value,
        expectedNextSession,
        "VWAP post-replacement session-reset mismatch");

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

    value = live.Update(
        MakeBar(31, 34, 28, 31, 10, 3000, 20260804));
    Check(value.replaced,
          "VWAP state must remain replaceable after rejected input");

    Check(live.RetainedBytes() >= sizeof(double) * 8U,
          "VWAP retained-byte metric is unexpectedly small");

    live.Reset();
    value = live.Update(bars[0]);
    Check(!value.replaced && value.fault == IndicatorFault::None,
          "VWAP reset must restore an empty state");
    CheckNear(value.Value(VwapValueOutput), 10.0,
              "VWAP reset value mismatch");

    std::puts("[PASS] vwap_indicator_tests");
    return 0;
}
