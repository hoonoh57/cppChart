#include "../core/standard_indicators.h"

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

    trading::Bar MakeBar(
        int index,
        int close,
        int highOffset = 4,
        int lowOffset = 4)
    {
        trading::Bar bar;
        bar.open = close - 1;
        bar.high = close + highOffset;
        bar.low = close - lowOffset;
        bar.close = close;
        bar.volume = 1000 + index * 10;
        bar.closeTimestampMs = 1000LL + index * 60000LL;
        bar.tradingDateYmd = 20260804;
        return bar;
    }

    trading::indicators::IndicatorInstance Create(
        trading::indicators::IndicatorRegistry& registry,
        const std::string& id,
        const std::string& type,
        std::initializer_list<std::pair<const char*, double>> parameters)
    {
        trading::indicators::IndicatorSpec spec;
        spec.id = id;
        spec.type = type;
        for (const auto& parameter : parameters) {
            spec.parameters.emplace(parameter.first, parameter.second);
        }
        std::string error;
        auto result = registry.Create(spec, error);
        Check(result.IsValid(), error.c_str());
        return result;
    }

    void TestPackRegistration()
    {
        trading::indicators::IndicatorRegistry registry;
        Check(trading::indicators::RegisterEmaIndicator(registry),
              "EMA registration failed");
        Check(trading::indicators::RegisterBollingerIndicator(registry),
              "Bollinger registration failed");
        Check(trading::indicators::RegisterRsiIndicator(registry),
              "RSI registration failed");
        Check(trading::indicators::RegisterMacdIndicator(registry),
              "MACD registration failed");
        Check(trading::indicators::RegisterDmiIndicator(registry),
              "DMI registration failed");
        Check(trading::indicators::RegisterSuperTrendIndicator(registry),
              "SuperTrend registration failed");
        Check(registry.Types().size() == 6U,
              "standard pack type count mismatch");
    }

    void TestEmaAndReplacement()
    {
        trading::indicators::IndicatorRegistry registry;
        trading::indicators::RegisterEmaIndicator(registry);
        auto instance = Create(registry, "ema.3", "EMA", {{"period", 3.0}});

        auto a = instance.Update(MakeBar(0, 100));
        auto b = instance.Update(MakeBar(1, 110));
        auto c = instance.Update(MakeBar(2, 120));
        Check(!a.IsReady(0) && !b.IsReady(0),
              "EMA must wait for seed period");
        Check(c.IsReady(0) && std::fabs(c.Value(0) - 110.0) < 1e-9,
              "EMA seed mismatch");

        trading::Bar replacement = MakeBar(2, 130);
        auto replaced = instance.Update(replacement);
        Check(replaced.replaced, "EMA same timestamp must replace");
        Check(std::fabs(replaced.Value(0) - 113.3333333333) < 1e-6,
              "EMA replacement must restore pre-latest state");
    }

    void TestBollinger()
    {
        trading::indicators::IndicatorRegistry registry;
        trading::indicators::RegisterBollingerIndicator(registry);
        auto instance = Create(
            registry,
            "bb.3",
            "BOLLINGER",
            {{"period", 3.0}, {"deviation", 2.0}});
        instance.Update(MakeBar(0, 100));
        instance.Update(MakeBar(1, 110));
        auto value = instance.Update(MakeBar(2, 120));
        Check(value.IsReady(0) && value.IsReady(1) && value.IsReady(2),
              "Bollinger outputs must become ready together");
        Check(std::fabs(value.Value(0) - 110.0) < 1e-9,
              "Bollinger middle mismatch");
        Check(value.Value(1) > value.Value(0) &&
              value.Value(2) < value.Value(0),
              "Bollinger band ordering mismatch");

        auto replaced = instance.Update(MakeBar(2, 130));
        Check(replaced.replaced, "Bollinger latest bar must replace");
        Check(std::fabs(replaced.Value(0) - 113.3333333333) < 1e-6,
              "Bollinger replacement middle mismatch");
    }

    void TestRsiRange()
    {
        trading::indicators::IndicatorRegistry registry;
        trading::indicators::RegisterRsiIndicator(registry);
        auto instance = Create(registry, "rsi.3", "RSI", {{"period", 3.0}});
        instance.Update(MakeBar(0, 100));
        instance.Update(MakeBar(1, 110));
        instance.Update(MakeBar(2, 105));
        auto value = instance.Update(MakeBar(3, 120));
        Check(value.IsReady(0), "RSI must become ready after period transitions");
        Check(value.Value(0) >= 0.0 && value.Value(0) <= 100.0,
              "RSI must remain in 0..100");
        auto replaced = instance.Update(MakeBar(3, 90));
        Check(replaced.replaced, "RSI latest bar must replace");
        Check(replaced.Value(0) < value.Value(0),
              "RSI replacement must react to changed close");
    }

    void TestMacd()
    {
        trading::indicators::IndicatorRegistry registry;
        trading::indicators::RegisterMacdIndicator(registry);
        auto instance = Create(
            registry,
            "macd",
            "MACD",
            {{"fast_period", 3.0},
             {"slow_period", 5.0},
             {"signal_period", 2.0}});
        trading::indicators::IndicatorValue latest;
        for (int index = 0; index < 12; ++index) {
            latest = instance.Update(MakeBar(index, 100 + index * 3));
        }
        Check(latest.IsReady(trading::indicators::MacdValueOutput),
              "MACD value must become ready");
        Check(latest.IsReady(trading::indicators::MacdSignalOutput),
              "MACD signal must become ready");
        Check(latest.IsReady(trading::indicators::MacdHistogramOutput),
              "MACD histogram must become ready");
        Check(std::fabs(
                  latest.Value(trading::indicators::MacdHistogramOutput) -
                  (latest.Value(trading::indicators::MacdValueOutput) -
                   latest.Value(trading::indicators::MacdSignalOutput))) < 1e-9,
              "MACD histogram identity mismatch");
    }

    void TestDmi()
    {
        trading::indicators::IndicatorRegistry registry;
        trading::indicators::RegisterDmiIndicator(registry);
        auto instance = Create(registry, "dmi.3", "DMI", {{"period", 3.0}});
        trading::indicators::IndicatorValue latest;
        for (int index = 0; index < 12; ++index) {
            latest = instance.Update(
                MakeBar(index, 100 + index * 2, 6, 3));
        }
        Check(latest.IsReady(trading::indicators::DmiPlusOutput),
              "DMI +DI must be ready");
        Check(latest.IsReady(trading::indicators::DmiMinusOutput),
              "DMI -DI must be ready");
        Check(latest.IsReady(trading::indicators::DmiAdxOutput),
              "DMI ADX must be ready");
        for (std::size_t index = 0; index < 3U; ++index) {
            Check(latest.Value(index) >= 0.0 && latest.Value(index) <= 100.0,
                  "DMI output must remain in 0..100");
        }
    }

    void TestSuperTrend()
    {
        trading::indicators::IndicatorRegistry registry;
        trading::indicators::RegisterSuperTrendIndicator(registry);
        auto instance = Create(
            registry,
            "st.3",
            "SUPERTREND",
            {{"period", 3.0}, {"multiplier", 2.0}});
        trading::indicators::IndicatorValue latest;
        for (int index = 0; index < 10; ++index) {
            latest = instance.Update(
                MakeBar(index, 100 + index * 3, 5, 5));
        }
        Check(latest.IsReady(trading::indicators::SuperTrendValueOutput),
              "SuperTrend value must become ready");
        Check(latest.IsReady(trading::indicators::SuperTrendUpOutput) !=
                  latest.IsReady(trading::indicators::SuperTrendDownOutput),
              "SuperTrend must expose exactly one direction line");
        auto replacement = instance.Update(MakeBar(9, 80, 5, 5));
        Check(replacement.replaced,
              "SuperTrend latest bar must replace from prior state");
    }

    void TestInvalidMacdConfiguration()
    {
        trading::indicators::IndicatorRegistry registry;
        trading::indicators::RegisterMacdIndicator(registry);
        trading::indicators::IndicatorSpec spec;
        spec.id = "invalid";
        spec.type = "MACD";
        spec.parameters["fast_period"] = 12.0;
        spec.parameters["slow_period"] = 5.0;
        spec.parameters["signal_period"] = 9.0;
        std::string error;
        auto instance = registry.Create(spec, error);
        Check(!instance.IsValid(),
              "MACD fast >= slow must fail closed");
        Check(!error.empty(), "invalid MACD must report error");
    }
}

int main()
{
    TestPackRegistration();
    TestEmaAndReplacement();
    TestBollinger();
    TestRsiRange();
    TestMacd();
    TestDmi();
    TestSuperTrend();
    TestInvalidMacdConfiguration();
    std::puts("[PASS] standard_indicators_tests");
    return 0;
}
