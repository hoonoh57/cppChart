#include "../render/time_boundaries.h"

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

    void TestContinuousSeriesHasNoBoundary()
    {
        const std::vector<trading::EpochMillis> timestamps = {
            1000, 61000, 121000, 181000, 241000
        };
        const auto boundaries =
            trading::render::FindTimeBoundaries(timestamps, 0, 3);
        Check(boundaries.empty(),
              "continuous minute data must not create boundaries");
    }

    void TestIntradayGap()
    {
        const std::vector<trading::EpochMillis> timestamps = {
            1000, 61000, 121000, 601000, 661000
        };
        const auto boundaries =
            trading::render::FindTimeBoundaries(timestamps, 0, 3);
        Check(boundaries.size() == 1,
              "large intraday gap must create one boundary");
        Check(boundaries[0].timestampMs == 601000,
              "intraday gap timestamp mismatch");
        Check(boundaries[0].kind ==
                  trading::render::TimeBoundaryKind::SessionGap,
              "intraday gap kind mismatch");
    }

    void TestKstDateBoundaryTakesPrecedence()
    {
        constexpr trading::EpochMillis KstOffset =
            9LL * 60LL * 60LL * 1000LL;
        constexpr trading::EpochMillis Day =
            24LL * 60LL * 60LL * 1000LL;
        const trading::EpochMillis localMidnightUtc =
            Day - KstOffset;

        const std::vector<trading::EpochMillis> timestamps = {
            localMidnightUtc - 120000,
            localMidnightUtc - 60000,
            localMidnightUtc,
            localMidnightUtc + 60000
        };
        const auto boundaries =
            trading::render::FindTimeBoundaries(timestamps, 540, 3);
        Check(boundaries.size() == 1,
              "KST date change must create one boundary");
        Check(boundaries[0].timestampMs == localMidnightUtc,
              "KST date boundary timestamp mismatch");
        Check(boundaries[0].kind ==
                  trading::render::TimeBoundaryKind::CalendarDate,
              "calendar date boundary must take precedence");
    }

    void TestInvalidAndDuplicateTimestampsAreIgnored()
    {
        const std::vector<trading::EpochMillis> timestamps = {
            1000, 61000, 61000, 30000, 121000
        };
        const auto boundaries =
            trading::render::FindTimeBoundaries(timestamps, 0, 3);
        Check(boundaries.empty(),
              "duplicates and reverse timestamps must not create false boundaries");
    }
}

int main()
{
    TestContinuousSeriesHasNoBoundary();
    TestIntradayGap();
    TestKstDateBoundaryTakesPrecedence();
    TestInvalidAndDuplicateTimestampsAreIgnored();
    std::puts("[PASS] time_boundaries_tests");
    return 0;
}
