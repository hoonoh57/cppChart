#include "../core/indicator_engine.h"
#include "../core/jma_indicator.h"

#include <cmath>
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

    void CheckNan(double actual, const char* message)
    {
        if (!std::isnan(actual)) Fail(message);
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
        bar.volume = 1;
        bar.closeTimestampMs = timestampMs;
        bar.tickCount = 1;
        return bar;
    }

    trading::indicators::IndicatorSpec JmaSpec(
        double period,
        double phase,
        double power)
    {
        trading::indicators::IndicatorSpec spec;
        spec.id = "jma.main";
        spec.type = "JMA";
        spec.parameters.emplace("period", period);
        spec.parameters.emplace("phase", phase);
        spec.parameters.emplace("power", power);
        return spec;
    }
}

int main()
{
    using namespace trading;
    using namespace trading::indicators;

    IndicatorRegistry registry;
    Check(RegisterJmaIndicator(registry),
          "JMA registration must succeed");
    Check(!RegisterJmaIndicator(registry),
          "duplicate JMA registration must fail");

    std::string error;
    Check(!registry.Create(JmaSpec(0, 0, 2), error).IsValid(),
          "zero JMA period must fail");
    Check(!registry.Create(JmaSpec(3, -101, 2), error).IsValid(),
          "JMA phase below -100 must fail");
    Check(!registry.Create(JmaSpec(3, 101, 2), error).IsValid(),
          "JMA phase above 100 must fail");
    Check(!registry.Create(JmaSpec(3, 0, 0), error).IsValid(),
          "zero JMA power must fail");
    Check(!registry.Create(JmaSpec(3.5, 0, 2), error).IsValid(),
          "fractional JMA period must fail");

    const std::vector<Bar> bars = {
        MakeBar(10, 1000),
        MakeBar(20, 2000),
        MakeBar(30, 3000),
        MakeBar(40, 4000),
        MakeBar(50, 5000),
        MakeBar(60, 6000)
    };
    const double expectedValue[] = {
        10.0,
        15.0,
        20.0,
        36.83509826660156,
        48.161800384521484,
        58.36479949951172
    };
    const double expectedSlope[] = {
        0.0,
        50.0,
        33.29999923706055,
        84.19999694824219,
        30.700000762939453,
        21.200000762939453
    };

    IndicatorInstance batchInstance =
        registry.Create(JmaSpec(3, 0, 2), error);
    Check(batchInstance.IsValid(),
          "batch JMA instance creation failed");
    std::vector<IndicatorValue> batch;
    Check(CalculateBatch(batchInstance, bars, batch, error),
          "JMA batch calculation failed");
    Check(batch.size() == bars.size(),
          "JMA batch output size mismatch");
    for (std::size_t index = 0; index < batch.size(); ++index) {
        Check(batch[index].outputCount == 4U,
              "JMA must publish Value/Up/Down/Slope");
        Check(
            batch[index].IsReady(JmaValueOutput) &&
            batch[index].IsReady(JmaUpOutput) &&
            batch[index].IsReady(JmaDownOutput) &&
            batch[index].IsReady(JmaSlopeOutput),
            "JMA outputs must be ready from the first sample");
        Check(batch[index].fault == IndicatorFault::None,
              "JMA batch value fault mismatch");
        CheckNear(batch[index].Value(JmaValueOutput), expectedValue[index],
                  "JMA legacy Value fixture mismatch");
        CheckNear(batch[index].Value(JmaUpOutput), expectedValue[index],
                  "JMA legacy Up fixture mismatch");
        CheckNan(batch[index].Value(JmaDownOutput),
                 "rising JMA must publish NaN on Down");
        CheckNear(batch[index].Value(JmaSlopeOutput), expectedSlope[index],
                  "JMA legacy Slope fixture mismatch");
    }

    IndicatorInstance incremental =
        registry.Create(JmaSpec(3, 0, 2), error);
    Check(incremental.IsValid(),
          "incremental JMA instance creation failed");
    for (std::size_t index = 0; index < bars.size(); ++index) {
        const IndicatorValue value = incremental.Update(bars[index]);
        Check(value.timestampMs == batch[index].timestampMs,
              "JMA batch/incremental timestamp parity mismatch");
        Check(value.readyMask == batch[index].readyMask,
              "JMA batch/incremental readiness parity mismatch");
        Check(value.fault == batch[index].fault,
              "JMA batch/incremental fault parity mismatch");
        for (std::size_t output = 0; output < 4; ++output) {
            const double actual = value.Value(output);
            const double expected = batch[index].Value(output);
            if (std::isnan(expected)) CheckNan(actual, "JMA NaN parity mismatch");
            else CheckNear(actual, expected, "JMA output parity mismatch");
        }
    }

    IndicatorInstance live = registry.Create(JmaSpec(3, 0, 2), error);
    Check(live.IsValid(), "live JMA instance creation failed");
    live.Update(MakeBar(10, 1000));
    live.Update(MakeBar(20, 2000));
    live.Update(MakeBar(30, 3000));

    IndicatorValue value = live.Update(MakeBar(60, 3000));
    Check(value.replaced,
          "same-timestamp JMA update must replace the live tail");
    CheckNear(value.Value(JmaValueOutput), 30.0,
              "same-timestamp JMA replacement mismatch");
    CheckNear(value.Value(JmaSlopeOutput), 100.0,
              "same-timestamp JMA slope mismatch");

    value = live.Update(MakeBar(40, 4000));
    Check(!value.replaced,
          "new JMA timestamp must append instead of replace");
    CheckNear(value.Value(JmaValueOutput), 39.5806999206543,
              "post-replacement JMA state mismatch");
    CheckNear(value.Value(JmaSlopeOutput), 31.899999618530273,
              "post-replacement JMA slope mismatch");

    const IndicatorValue backward = live.Update(MakeBar(50, 3500));
    Check(backward.fault == IndicatorFault::TimestampMovedBackward,
          "backward JMA timestamp must fail closed");

    const IndicatorValue invalid = live.Update(MakeBar(0, 4000));
    Check(invalid.fault == IndicatorFault::InvalidInput,
          "invalid JMA close must fail closed");

    value = live.Update(MakeBar(50, 4000));
    Check(value.replaced,
          "JMA state must remain replaceable after rejected input");
    CheckNear(value.Value(JmaValueOutput), 47.77429962158203,
              "rejected JMA input must not mutate state");

    Check(live.RetainedBytes() >= sizeof(double) * 10U,
          "JMA retained-byte metric is unexpectedly small");

    live.Reset();
    value = live.Update(MakeBar(7, 7000));
    Check(value.IsReady(JmaValueOutput) && !value.replaced,
          "JMA reset must restore an empty state");
    CheckNear(value.Value(JmaValueOutput), 7.0,
              "JMA reset value mismatch");

    value = live.Update(MakeBar(5, 8000));
    CheckNan(value.Value(JmaUpOutput),
             "falling JMA must publish NaN on Up");
    CheckNear(value.Value(JmaDownOutput), 6.0,
              "falling JMA Down output mismatch");
    CheckNear(value.Value(JmaSlopeOutput), -14.300000190734863,
              "falling JMA slope mismatch");

    std::puts("[PASS] jma_indicator_tests");
    return 0;
}
