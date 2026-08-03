#include "../core/indicator_engine.h"
#include "../core/obv_indicator.h"

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
        if (std::fabs(actual - expected) > 1.0e-4) {
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
        trading::Volume volume,
        trading::EpochMillis timestampMs)
    {
        trading::Bar bar;
        bar.open = close;
        bar.high = close;
        bar.low = close;
        bar.close = close;
        bar.volume = volume;
        bar.closeTimestampMs = timestampMs;
        bar.tickCount = 1;
        return bar;
    }

    trading::indicators::IndicatorSpec ObvSpec(double signalPeriod)
    {
        trading::indicators::IndicatorSpec spec;
        spec.id = "obv.main";
        spec.type = "OBV";
        spec.parameters.emplace("signal_period", signalPeriod);
        return spec;
    }
}

int main()
{
    using namespace trading;
    using namespace trading::indicators;

    IndicatorRegistry registry;
    Check(RegisterObvIndicator(registry),
          "OBV registration must succeed");
    Check(!RegisterObvIndicator(registry),
          "duplicate OBV registration must fail");

    std::string error;
    IndicatorSpec missing;
    missing.id = "obv.invalid";
    missing.type = "OBV";
    Check(!registry.Create(missing, error).IsValid(),
          "missing OBV signal period must fail closed");
    Check(!registry.Create(ObvSpec(0), error).IsValid(),
          "zero OBV signal period must fail closed");
    Check(!registry.Create(ObvSpec(3.5), error).IsValid(),
          "fractional OBV signal period must fail closed");

    const std::vector<Bar> bars = {
        MakeBar(10, 100, 1000),
        MakeBar(12, 200, 2000),
        MakeBar(11, 50, 3000),
        MakeBar(11, 70, 4000),
        MakeBar(13, 80, 5000)
    };
    const double expectedObv[] = {
        100.0,
        300.0,
        250.0,
        250.0,
        330.0
    };
    const double expectedSignal[] = {
        0.0,
        0.0,
        216.6666717529297,
        266.6666564941406,
        276.6666564941406
    };
    const double expectedDirection[] = {
        0.0,
        0.0,
        1.0,
        -1.0,
        1.0
    };

    IndicatorInstance batchInstance = registry.Create(ObvSpec(3), error);
    Check(batchInstance.IsValid(),
          "batch OBV instance creation failed");
    std::vector<IndicatorValue> batch;
    Check(CalculateBatch(batchInstance, bars, batch, error),
          "OBV batch calculation failed");
    Check(batch.size() == bars.size(),
          "OBV batch output size mismatch");
    for (std::size_t index = 0; index < batch.size(); ++index) {
        Check(batch[index].outputCount == 3U,
              "OBV must publish OBV/Signal/Direction");
        Check(batch[index].fault == IndicatorFault::None,
              "OBV batch fault mismatch");
        CheckNear(batch[index].Value(ObvValueOutput), expectedObv[index],
                  "OBV legacy value fixture mismatch");
        if (index < 2U) {
            CheckNan(batch[index].Value(ObvSignalOutput),
                     "OBV warmup signal must be NaN");
            CheckNan(batch[index].Value(ObvDirectionOutput),
                     "OBV warmup direction must be NaN");
        }
        else {
            CheckNear(
                batch[index].Value(ObvSignalOutput),
                expectedSignal[index],
                "OBV legacy signal fixture mismatch");
            CheckNear(
                batch[index].Value(ObvDirectionOutput),
                expectedDirection[index],
                "OBV legacy direction fixture mismatch");
        }
    }

    IndicatorInstance incremental = registry.Create(ObvSpec(3), error);
    Check(incremental.IsValid(),
          "incremental OBV instance creation failed");
    for (std::size_t index = 0; index < bars.size(); ++index) {
        const IndicatorValue value = incremental.Update(bars[index]);
        Check(value.timestampMs == batch[index].timestampMs,
              "OBV batch/incremental timestamp parity mismatch");
        Check(value.readyMask == batch[index].readyMask,
              "OBV batch/incremental readiness parity mismatch");
        Check(value.fault == batch[index].fault,
              "OBV batch/incremental fault parity mismatch");
        for (std::size_t output = 0; output < 3; ++output) {
            const double actual = value.Value(output);
            const double expected = batch[index].Value(output);
            if (std::isnan(expected)) CheckNan(actual, "OBV NaN parity mismatch");
            else CheckNear(actual, expected, "OBV output parity mismatch");
        }
    }

    IndicatorInstance live = registry.Create(ObvSpec(3), error);
    Check(live.IsValid(), "live OBV instance creation failed");
    live.Update(MakeBar(10, 100, 1000));
    live.Update(MakeBar(12, 200, 2000));
    live.Update(MakeBar(11, 50, 3000));

    IndicatorValue value = live.Update(MakeBar(15, 60, 3000));
    Check(value.replaced,
          "same-timestamp OBV update must replace the live tail");
    CheckNear(value.Value(ObvValueOutput), 360.0,
              "same-timestamp OBV replacement mismatch");
    CheckNear(value.Value(ObvSignalOutput), 253.3333282470703,
              "same-timestamp OBV signal mismatch");
    CheckNear(value.Value(ObvDirectionOutput), 1.0,
              "same-timestamp OBV direction mismatch");

    value = live.Update(MakeBar(14, 40, 4000));
    Check(!value.replaced,
          "new OBV timestamp must append instead of replace");
    CheckNear(value.Value(ObvValueOutput), 320.0,
              "post-replacement OBV state mismatch");
    CheckNear(value.Value(ObvSignalOutput), 326.6666564941406,
              "post-replacement OBV signal mismatch");
    CheckNear(value.Value(ObvDirectionOutput), -1.0,
              "post-replacement OBV direction mismatch");

    const IndicatorValue backward =
        live.Update(MakeBar(16, 10, 3500));
    Check(backward.fault == IndicatorFault::TimestampMovedBackward,
          "backward OBV timestamp must fail closed");

    const IndicatorValue invalidVolume =
        live.Update(MakeBar(16, -1, 4000));
    Check(invalidVolume.fault == IndicatorFault::InvalidInput,
          "negative OBV volume must fail closed");

    value = live.Update(MakeBar(16, 10, 4000));
    Check(value.replaced,
          "OBV state must remain replaceable after rejected input");
    CheckNear(value.Value(ObvValueOutput), 370.0,
              "rejected OBV input must not mutate state");
    CheckNear(value.Value(ObvSignalOutput), 343.3333435058594,
              "rejected OBV input signal mismatch");

    Check(live.RetainedBytes() >= sizeof(double) * 3U,
          "OBV retained-byte metric must include its signal window");

    live.Reset();
    value = live.Update(MakeBar(7, 25, 7000));
    Check(value.IsReady(ObvValueOutput) && !value.replaced,
          "OBV reset must restore an empty state");
    CheckNear(value.Value(ObvValueOutput), 25.0,
              "OBV reset value mismatch");
    CheckNan(value.Value(ObvSignalOutput),
             "OBV reset signal warmup mismatch");

    std::puts("[PASS] obv_indicator_tests");
    return 0;
}
