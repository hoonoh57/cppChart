#include "../app/indicator_properties.h"

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

    trading::indicators::IndicatorSpec Spec(
        const char* id,
        const char* type)
    {
        trading::indicators::IndicatorSpec spec;
        spec.id = id;
        spec.type = type;
        return spec;
    }
}

int main()
{
    using namespace trading;
    using namespace trading::app;

    indicators::IndicatorSpec sma = Spec("sma.20", "SMA");
    sma.parameters.emplace("period", 20.0);

    IndicatorPropertySnapshot properties;
    std::string error;
    Check(DescribeIndicatorProperties(sma, properties, error),
          "SMA properties must be described");
    Check(properties.parameters.size() == 1U,
          "SMA property count mismatch");
    Check(properties.parameters[0].kind ==
              IndicatorParameterKind::Integer,
          "SMA period must be integer");
    Check(IndicatorLegendLabel(sma) == "SMA 20",
          "SMA legend label mismatch");

    indicators::IndicatorSpec jma = Spec("jma.20", "JMA");
    jma.parameters.emplace("period", 20.0);
    jma.parameters.emplace("phase", 0.0);
    jma.parameters.emplace("power", 2.0);
    Check(IndicatorLegendLabel(jma) == "JMA 20 P0 Pow2",
          "JMA legend label mismatch");
    Check(IndicatorLegendLabel(jma, "slope") == "JMA Slope 20",
          "JMA slope legend label mismatch");

    indicators::IndicatorSpec vwap = Spec("vwap.session", "VWAP");
    vwap.parameters.emplace("std_dev_1", 1.0);
    vwap.parameters.emplace("std_dev_2", 2.0);
    Check(IndicatorLegendLabel(vwap) == "VWAP 1/2",
          "VWAP legend label mismatch");

    indicators::IndicatorSpec obv = Spec("obv.20", "OBV");
    obv.parameters.emplace("signal_period", 20.0);
    Check(IndicatorLegendLabel(obv) == "OBV Signal 20",
          "OBV legend label mismatch");

    indicators::IndicatorSpec adx = Spec("adx.14", "ADX");
    adx.parameters.emplace("period", 14.0);
    Check(IndicatorLegendLabel(adx) == "ADX 14",
          "ADX legend label mismatch");

    std::vector<indicators::IndicatorSpec> specs = {
        sma, jma, vwap, obv, adx
    };
    Check(UpdateIndicatorParameter(
              specs, "sma.20", "period", 25.0, error),
          "valid SMA period update must succeed");
    Check(specs[0].parameters["period"] == 25.0,
          "SMA period update was not stored");
    Check(!UpdateIndicatorParameter(
              specs, "sma.20", "period", 25.5, error),
          "fractional integer parameter must fail");
    Check(!UpdateIndicatorParameter(
              specs, "jma.20", "phase", 101.0, error),
          "out-of-range parameter must fail");
    Check(!UpdateIndicatorParameter(
              specs, "missing", "period", 10.0, error),
          "missing indicator target must fail");
    Check(!UpdateIndicatorParameter(
              specs, "adx.14", "unknown", 10.0, error),
          "unknown parameter key must fail");

    std::puts("[PASS] indicator_properties_tests");
    return 0;
}
