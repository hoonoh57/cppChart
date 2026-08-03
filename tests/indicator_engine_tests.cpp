#include "../core/indicator_engine.h"
#include "../core/sma_indicator.h"

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

    void CheckNear(
        double actual,
        double expected,
        double tolerance,
        const char* message)
    {
        if (std::fabs(actual - expected) > tolerance) {
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
        trading::EpochMillis timestampMs)
    {
        trading::Bar bar;
        bar.open = close;
        bar.high = close;
        bar.low = close;
        bar.close = close;
        bar.closeTimestampMs = timestampMs;
        return bar;
    }

    trading::indicators::IndicatorSpec SmaSpec(int period)
    {
        trading::indicators::IndicatorSpec spec;
        spec.id = "sma.fast";
        spec.type = "SMA";
        spec.parameters.emplace("period", static_cast<double>(period));
        return spec;
    }

    void TestSpecSerialization()
    {
        using namespace trading::indicators;

        const IndicatorSpec source = SmaSpec(3);
        std::string json;
        std::string error;
        Check(SerializeIndicatorSpec(source, json, error),
              "valid indicator spec must serialize");
        Check(
            json ==
                "{\"id\":\"sma.fast\",\"type\":\"SMA\","
                "\"parameters\":{\"period\":3}}",
            "indicator serialization must be deterministic");

        IndicatorSpec parsed;
        Check(ParseIndicatorSpec(json, parsed, error),
              "serialized indicator spec must parse");
        Check(parsed.id == source.id,
              "indicator id roundtrip mismatch");
        Check(parsed.type == source.type,
              "indicator type roundtrip mismatch");
        Check(parsed.parameters == source.parameters,
              "indicator parameters roundtrip mismatch");

        Check(!ParseIndicatorSpec(
                  "{\"id\":\"x\",\"type\":\"SMA\","
                  "\"parameters\":{},\"unknown\":1}",
                  parsed,
                  error),
              "unknown indicator root keys must fail closed");
    }

    void TestRegistryAndValidation()
    {
        using namespace trading::indicators;

        IndicatorRegistry registry;
        Check(RegisterSmaIndicator(registry),
              "SMA registration must succeed");
        Check(!RegisterSmaIndicator(registry),
              "duplicate SMA registration must fail");
        Check(registry.Contains("SMA"),
              "registry must contain SMA");
        Check(registry.Types().size() == 1U,
              "registry type count mismatch");

        std::string error;
        IndicatorSpec unknown = SmaSpec(3);
        unknown.type = "UNKNOWN";
        Check(!registry.Create(unknown, error).IsValid(),
              "unknown indicator type must fail");

        IndicatorSpec fractional = SmaSpec(3);
        fractional.parameters["period"] = 3.5;
        Check(!registry.Create(fractional, error).IsValid(),
              "fractional SMA period must fail");

        IndicatorSpec extra = SmaSpec(3);
        extra.parameters.emplace("source", 1.0);
        Check(!registry.Create(extra, error).IsValid(),
              "unknown SMA parameters must fail");
    }

    void TestBatchIncrementalParity()
    {
        using namespace trading::indicators;

        IndicatorRegistry registry;
        Check(RegisterSmaIndicator(registry),
              "SMA registration failed");

        const std::vector<trading::Bar> bars = {
            MakeBar(10, 1000),
            MakeBar(20, 2000),
            MakeBar(30, 3000),
            MakeBar(40, 4000)
        };

        std::string error;
        IndicatorInstance batchInstance = registry.Create(SmaSpec(3), error);
        Check(batchInstance.IsValid(),
              "batch SMA instance creation failed");

        std::vector<IndicatorValue> batch;
        Check(CalculateBatch(batchInstance, bars, batch, error),
              "SMA batch calculation failed");
        Check(batch.size() == bars.size(),
              "SMA batch result count mismatch");
        Check(!batch[0].ready && !batch[1].ready,
              "SMA warmup readiness mismatch");
        Check(batch[2].ready && batch[3].ready,
              "SMA ready state mismatch");
        CheckNear(batch[2].value, 20.0, 1e-12,
                  "SMA first ready value mismatch");
        CheckNear(batch[3].value, 30.0, 1e-12,
                  "SMA rolling value mismatch");

        IndicatorInstance incremental = registry.Create(SmaSpec(3), error);
        Check(incremental.IsValid(),
              "incremental SMA instance creation failed");
        Check(incremental.RetainedBytes() >= sizeof(double) * 3U,
              "SMA retained-byte metric must include the rolling window");

        for (std::size_t index = 0; index < bars.size(); ++index) {
            const IndicatorValue live = incremental.Update(bars[index]);
            Check(live.fault == IndicatorFault::None,
                  "incremental SMA update failed");
            Check(live.timestampMs == batch[index].timestampMs,
                  "batch/incremental timestamp parity mismatch");
            Check(live.ready == batch[index].ready,
                  "batch/incremental readiness parity mismatch");
            CheckNear(live.value, batch[index].value, 1e-12,
                      "batch/incremental value parity mismatch");
        }
    }

    void TestMutableLiveTail()
    {
        using namespace trading::indicators;

        IndicatorRegistry registry;
        Check(RegisterSmaIndicator(registry),
              "SMA registration failed");

        std::string error;
        IndicatorInstance instance = registry.Create(SmaSpec(3), error);
        Check(instance.IsValid(), "SMA instance creation failed");

        Check(instance.Update(MakeBar(10, 1000)).fault == IndicatorFault::None,
              "first SMA update failed");
        Check(instance.Update(MakeBar(20, 2000)).fault == IndicatorFault::None,
              "second SMA update failed");

        IndicatorValue value = instance.Update(MakeBar(30, 3000));
        Check(value.ready, "third SMA value must be ready");
        CheckNear(value.value, 20.0, 1e-12,
                  "initial live-tail SMA mismatch");

        value = instance.Update(MakeBar(60, 3000));
        Check(value.replaced,
              "same-timestamp update must replace the mutable live tail");
        CheckNear(value.value, 30.0, 1e-12,
                  "same-timestamp SMA replacement mismatch");

        value = instance.Update(MakeBar(40, 4000));
        Check(!value.replaced,
              "new timestamp must append instead of replace");
        CheckNear(value.value, 40.0, 1e-12,
                  "post-replacement rolling SMA mismatch");

        value = instance.Update(MakeBar(50, 3500));
        Check(value.fault == IndicatorFault::TimestampMovedBackward,
              "backward indicator timestamp must fail closed");
    }

    void TestBatchFailureIsTransactional()
    {
        using namespace trading::indicators;

        IndicatorRegistry registry;
        Check(RegisterSmaIndicator(registry),
              "SMA registration failed");

        std::string error;
        IndicatorInstance instance = registry.Create(SmaSpec(2), error);
        Check(instance.IsValid(), "SMA instance creation failed");

        std::vector<trading::Bar> bars = {
            MakeBar(10, 1000),
            MakeBar(0, 2000)
        };
        std::vector<IndicatorValue> output(1);
        Check(!CalculateBatch(instance, bars, output, error),
              "invalid batch input must fail");
        Check(output.empty(),
              "failed batch calculation must not expose partial results");
    }
}

int main()
{
    TestSpecSerialization();
    TestRegistryAndValidation();
    TestBatchIncrementalParity();
    TestMutableLiveTail();
    TestBatchFailureIsTransactional();

    std::puts("[PASS] indicator_engine_tests");
    return 0;
}
