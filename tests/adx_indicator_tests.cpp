#include "../core/adx_indicator.h"
#include "../core/indicator_engine.h"

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
        trading::PriceWon open,
        trading::PriceWon high,
        trading::PriceWon low,
        trading::PriceWon close,
        trading::EpochMillis timestampMs)
    {
        trading::Bar bar;
        bar.open = open;
        bar.high = high;
        bar.low = low;
        bar.close = close;
        bar.volume = 100;
        bar.closeTimestampMs = timestampMs;
        bar.tickCount = 1;
        return bar;
    }

    trading::indicators::IndicatorSpec AdxSpec(double period)
    {
        trading::indicators::IndicatorSpec spec;
        spec.id = "adx.main";
        spec.type = "ADX";
        spec.parameters.emplace("period", period);
        return spec;
    }

    trading::indicators::IndicatorValue BatchLast(
        trading::indicators::IndicatorRegistry& registry,
        const std::vector<trading::Bar>& bars)
    {
        std::string error;
        trading::indicators::IndicatorInstance instance =
            registry.Create(AdxSpec(3), error);
        Check(instance.IsValid(), "ADX comparison instance creation failed");
        std::vector<trading::indicators::IndicatorValue> values;
        Check(trading::indicators::CalculateBatch(
                  instance,
                  bars,
                  values,
                  error),
              "ADX comparison batch failed");
        Check(!values.empty(), "ADX comparison batch is empty");
        return values.back();
    }
}

int main()
{
    using namespace trading;
    using namespace trading::indicators;

    IndicatorRegistry registry;
    Check(RegisterAdxIndicator(registry),
          "ADX registration must succeed");
    Check(!RegisterAdxIndicator(registry),
          "duplicate ADX registration must fail");

    std::string error;
    Check(!registry.Create(AdxSpec(0), error).IsValid(),
          "zero ADX period must fail");
    Check(!registry.Create(AdxSpec(3.5), error).IsValid(),
          "fractional ADX period must fail");

    IndicatorSpec extra = AdxSpec(3);
    extra.parameters.emplace("source", 1.0);
    Check(!registry.Create(extra, error).IsValid(),
          "unknown ADX parameter must fail");

    const std::vector<Bar> rising = {
        MakeBar(10, 11, 9, 10, 1000),
        MakeBar(11, 12, 10, 11, 2000),
        MakeBar(12, 13, 11, 12, 3000),
        MakeBar(13, 14, 12, 13, 4000),
        MakeBar(14, 15, 13, 14, 5000),
        MakeBar(15, 16, 14, 15, 6000),
        MakeBar(16, 17, 15, 16, 7000)
    };

    IndicatorInstance batchInstance = registry.Create(AdxSpec(3), error);
    Check(batchInstance.IsValid(),
          "batch ADX instance creation failed");
    std::vector<IndicatorValue> batch;
    Check(CalculateBatch(batchInstance, rising, batch, error),
          "ADX batch calculation failed");
    Check(batch.size() == rising.size(),
          "ADX batch output size mismatch");
    for (std::size_t index = 0; index < 5U; ++index) {
        Check(!batch[index].IsReady(AdxValueOutput),
              "ADX must remain in Wilder warm-up before the sixth bar");
    }
    Check(
        batch[5].IsReady(AdxValueOutput) &&
        batch[6].IsReady(AdxValueOutput),
        "ADX must become ready after period DX samples");
    CheckNear(batch[5].Value(AdxValueOutput), 100.0,
              "initial rising ADX fixture mismatch");
    CheckNear(batch[6].Value(AdxValueOutput), 100.0,
              "smoothed rising ADX fixture mismatch");

    IndicatorInstance incremental = registry.Create(AdxSpec(3), error);
    Check(incremental.IsValid(),
          "incremental ADX instance creation failed");
    for (std::size_t index = 0; index < rising.size(); ++index) {
        const IndicatorValue value = incremental.Update(rising[index]);
        Check(value.timestampMs == batch[index].timestampMs,
              "ADX batch/incremental timestamp parity mismatch");
        Check(value.readyMask == batch[index].readyMask,
              "ADX batch/incremental readiness parity mismatch");
        Check(value.fault == batch[index].fault,
              "ADX batch/incremental fault parity mismatch");
        if (value.IsReady(AdxValueOutput)) {
            CheckNear(
                value.Value(AdxValueOutput),
                batch[index].Value(AdxValueOutput),
                "ADX batch/incremental value parity mismatch");
        }
    }

    IndicatorInstance live = registry.Create(AdxSpec(3), error);
    Check(live.IsValid(), "live ADX instance creation failed");
    for (std::size_t index = 0; index < 6U; ++index) {
        live.Update(rising[index]);
    }

    std::vector<Bar> replacedBars(rising.begin(), rising.begin() + 6);
    replacedBars.back() = MakeBar(15, 15, 10, 11, 6000);
    const IndicatorValue expectedReplacement =
        BatchLast(registry, replacedBars);
    IndicatorValue value = live.Update(replacedBars.back());
    Check(value.replaced,
          "same-timestamp ADX update must replace the live tail");
    Check(
        value.IsReady(AdxValueOutput) ==
            expectedReplacement.IsReady(AdxValueOutput),
        "ADX replacement readiness mismatch");
    if (value.IsReady(AdxValueOutput)) {
        CheckNear(
            value.Value(AdxValueOutput),
            expectedReplacement.Value(AdxValueOutput),
            "ADX replacement state mismatch");
    }

    const Bar appended = MakeBar(12, 13, 11, 12, 7000);
    replacedBars.push_back(appended);
    const IndicatorValue expectedAppend = BatchLast(registry, replacedBars);
    value = live.Update(appended);
    Check(!value.replaced,
          "new ADX timestamp must append instead of replace");
    Check(
        value.IsReady(AdxValueOutput) ==
            expectedAppend.IsReady(AdxValueOutput),
        "ADX post-replacement readiness mismatch");
    if (value.IsReady(AdxValueOutput)) {
        CheckNear(
            value.Value(AdxValueOutput),
            expectedAppend.Value(AdxValueOutput),
            "ADX post-replacement state mismatch");
    }

    const IndicatorValue backward =
        live.Update(MakeBar(13, 14, 12, 13, 6500));
    Check(backward.fault == IndicatorFault::TimestampMovedBackward,
          "backward ADX timestamp must fail closed");

    const IndicatorValue invalid =
        live.Update(MakeBar(12, 10, 11, 12, 7000));
    Check(invalid.fault == IndicatorFault::InvalidInput,
          "invalid ADX OHLC must fail closed");

    Check(live.RetainedBytes() >= sizeof(double) * 10U,
          "ADX retained-byte metric is unexpectedly small");

    live.Reset();
    value = live.Update(rising.front());
    Check(!value.IsReady(AdxValueOutput) && !value.replaced,
          "ADX reset must restore Wilder warm-up state");

    std::puts("[PASS] adx_indicator_tests");
    return 0;
}
