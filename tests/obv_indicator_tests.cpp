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
        if (std::fabs(actual - expected) > 1.0e-9) {
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

    trading::indicators::IndicatorSpec ObvSpec()
    {
        trading::indicators::IndicatorSpec spec;
        spec.id = "obv.main";
        spec.type = "OBV";
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
    IndicatorSpec invalidSpec = ObvSpec();
    invalidSpec.parameters.emplace("period", 10.0);
    Check(!registry.Create(invalidSpec, error).IsValid(),
          "OBV parameters must fail closed");

    const std::vector<Bar> bars = {
        MakeBar(10, 100, 1000),
        MakeBar(12, 200, 2000),
        MakeBar(11, 50, 3000),
        MakeBar(11, 70, 4000),
        MakeBar(13, 80, 5000)
    };
    const double expected[] = {
        100.0,
        300.0,
        250.0,
        250.0,
        330.0
    };

    IndicatorInstance batchInstance = registry.Create(ObvSpec(), error);
    Check(batchInstance.IsValid(),
          "batch OBV instance creation failed");
    std::vector<IndicatorValue> batch;
    Check(CalculateBatch(batchInstance, bars, batch, error),
          "OBV batch calculation failed");
    Check(batch.size() == bars.size(),
          "OBV batch output size mismatch");
    for (std::size_t index = 0; index < batch.size(); ++index) {
        Check(batch[index].ready,
              "OBV must be ready from the first sample");
        Check(batch[index].fault == IndicatorFault::None,
              "OBV batch fault mismatch");
        CheckNear(batch[index].value, expected[index],
                  "OBV legacy fixture mismatch");
    }

    IndicatorInstance incremental = registry.Create(ObvSpec(), error);
    Check(incremental.IsValid(),
          "incremental OBV instance creation failed");
    for (std::size_t index = 0; index < bars.size(); ++index) {
        const IndicatorValue value = incremental.Update(bars[index]);
        Check(value.timestampMs == batch[index].timestampMs,
              "OBV batch/incremental timestamp parity mismatch");
        Check(value.ready == batch[index].ready,
              "OBV batch/incremental readiness parity mismatch");
        Check(value.fault == batch[index].fault,
              "OBV batch/incremental fault parity mismatch");
        CheckNear(value.value, batch[index].value,
                  "OBV batch/incremental value parity mismatch");
    }

    IndicatorInstance live = registry.Create(ObvSpec(), error);
    Check(live.IsValid(), "live OBV instance creation failed");
    CheckNear(live.Update(MakeBar(10, 100, 1000)).value, 100.0,
              "first live OBV value mismatch");
    CheckNear(live.Update(MakeBar(12, 200, 2000)).value, 300.0,
              "second live OBV value mismatch");
    CheckNear(live.Update(MakeBar(11, 50, 3000)).value, 250.0,
              "third live OBV value mismatch");

    IndicatorValue value = live.Update(MakeBar(15, 60, 3000));
    Check(value.replaced,
          "same-timestamp OBV update must replace the live tail");
    CheckNear(value.value, 360.0,
              "same-timestamp OBV replacement mismatch");

    value = live.Update(MakeBar(14, 40, 4000));
    Check(!value.replaced,
          "new OBV timestamp must append instead of replace");
    CheckNear(value.value, 320.0,
              "post-replacement OBV state mismatch");

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
    CheckNear(value.value, 370.0,
              "rejected OBV input must not mutate state");

    Check(live.RetainedBytes() >= sizeof(double) * 2U,
          "OBV retained-byte metric is unexpectedly small");

    live.Reset();
    value = live.Update(MakeBar(7, 25, 7000));
    Check(value.ready && !value.replaced,
          "OBV reset must restore an empty state");
    CheckNear(value.value, 25.0,
              "OBV reset value mismatch");

    std::puts("[PASS] obv_indicator_tests");
    return 0;
}
