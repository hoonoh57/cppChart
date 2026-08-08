#include "../render/time_axis.h"

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

    bool Near(double left, double right, double tolerance = 0.0001)
    {
        return std::fabs(left - right) <= tolerance;
    }

    void TestTradingGapCompression()
    {
        const trading::EpochMillis first = 1000000;
        const trading::EpochMillis second = first + 60000;
        const trading::EpochMillis nextSession = second + 17LL * 60LL * 60LL * 1000LL;

        trading::render::OrdinalTimeAxis axis;
        std::string error;
        Check(axis.Reset({ first, second, nextSession }, error),
              "valid ordinal axis must build");
        Check(axis.Size() == 3, "axis size mismatch");
        Check(Near(axis.CoordinateForTimestamp(first), 0.0),
              "first bar coordinate mismatch");
        Check(Near(axis.CoordinateForTimestamp(second), 1.0),
              "second bar coordinate mismatch");
        Check(Near(axis.CoordinateForTimestamp(nextSession), 2.0),
              "next session must occupy the next ordinal slot");
        Check(
            Near(
                axis.CoordinateForTimestamp(nextSession) -
                    axis.CoordinateForTimestamp(second),
                1.0),
            "overnight time must not create a horizontal pixel gap");
    }

    void TestCoordinateLookup()
    {
        trading::render::OrdinalTimeAxis axis;
        std::string error;
        Check(axis.Reset({ 1000, 2000, 5000, 6000 }, error),
              "axis build failed");
        Check(Near(axis.CoordinateForTimestamp(3500), 1.5),
              "between-bar timestamp interpolation mismatch");
        Check(axis.TimestampForCoordinate(2.4) == 5000,
              "coordinate lookup must return nearest bar timestamp");
        Check(axis.TimestampForCoordinate(2.6) == 6000,
              "coordinate lookup rounding mismatch");
    }

    void TestInvalidAxis()
    {
        trading::render::OrdinalTimeAxis axis;
        std::string error;
        Check(!axis.Reset({}, error), "empty axis must fail");
        Check(!axis.Reset({ 1000, 1000 }, error),
              "duplicate timestamps must fail");
        Check(!axis.Reset({ 2000, 1000 }, error),
              "descending timestamps must fail");
    }
}

int main()
{
    TestTradingGapCompression();
    TestCoordinateLookup();
    TestInvalidAxis();
    std::puts("[PASS] time_axis_tests");
    return 0;
}
