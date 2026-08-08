#include "../app/comparison_transform.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

    void Near(double actual, double expected, const char* message)
    {
        if (std::fabs(actual - expected) > 1e-6) Fail(message);
    }

    trading::Bar MakeBar(
        trading::EpochMillis timestamp,
        trading::PriceWon close)
    {
        trading::Bar bar;
        bar.closeTimestampMs = timestamp;
        bar.open = close;
        bar.high = close;
        bar.low = close;
        bar.close = close;
        return bar;
    }
}

int main()
{
    using namespace trading;
    using namespace trading::app;

    const std::vector<Bar> comparison = {
        MakeBar(1000, 200),
        MakeBar(2000, 220),
        MakeBar(3000, 180)
    };
    const std::vector<Bar> primary = {
        MakeBar(1000, 1000),
        MakeBar(2000, 1050),
        MakeBar(3000, 900)
    };

    ComparisonTransformResult raw = TransformComparisonBars(
        comparison, primary, ComparisonValueMode::RawClose, 1.0);
    Check(raw.points.size() == 3U, "raw close point count mismatch");
    Near(raw.points[1].value, 220.0, "raw close value mismatch");

    ComparisonTransformResult indexed = TransformComparisonBars(
        comparison, primary, ComparisonValueMode::Indexed100, 1.0);
    Check(indexed.hasAnchor, "indexed mode anchor missing");
    Check(indexed.anchorTimestampMs == 1000, "indexed anchor timestamp mismatch");
    Near(indexed.points[0].value, 100.0, "indexed anchor mismatch");
    Near(indexed.points[1].value, 110.0, "indexed value mismatch");
    Near(indexed.points[2].value, 90.0, "indexed falling value mismatch");

    ComparisonTransformResult returns = TransformComparisonBars(
        comparison, primary, ComparisonValueMode::ReturnPercent, 1.0);
    Near(returns.points[0].value, 0.0, "return anchor mismatch");
    Near(returns.points[1].value, 10.0, "return gain mismatch");
    Near(returns.points[2].value, -10.0, "return loss mismatch");

    ComparisonTransformResult relative = TransformComparisonBars(
        comparison, primary, ComparisonValueMode::RelativeStrength100, 1.0);
    Near(relative.points[0].value, 100.0, "relative anchor mismatch");
    Near(relative.points[1].value, 110.0 / 105.0 * 100.0,
         "relative strength gain mismatch");
    Near(relative.points[2].value, 100.0,
         "equal relative performance mismatch");

    const std::vector<Bar> missingPrimary = {
        MakeBar(2000, 1050),
        MakeBar(3000, 900)
    };
    ComparisonTransformResult common = TransformComparisonBars(
        comparison,
        missingPrimary,
        ComparisonValueMode::RelativeStrength100,
        1.0);
    Check(common.anchorTimestampMs == 2000,
          "relative mode must use first common timestamp");
    Near(common.points.front().value, 100.0,
         "first common point must be relative anchor");

    render::LinePoint live;
    Check(TransformComparisonLivePoint(
              MakeBar(3000, 198),
              primary,
              indexed,
              ComparisonValueMode::Indexed100,
              1.0,
              live),
          "indexed live transform failed");
    Near(live.value, 99.0, "indexed live value mismatch");

    Check(!TransformComparisonLivePoint(
              MakeBar(4000, 198),
              primary,
              relative,
              ComparisonValueMode::RelativeStrength100,
              1.0,
              live),
          "relative live point without common primary timestamp must fail");

    std::puts("[PASS] comparison_transform_tests");
    return 0;
}
