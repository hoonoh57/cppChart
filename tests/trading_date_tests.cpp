#include "../core/market_types.h"

#include <cstdio>
#include <cstdlib>

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
}

int main()
{
    using namespace trading;

    Check(IsValidTradingDateYmd(20260803),
          "valid trading date must pass");
    Check(IsValidTradingDateYmd(20240229),
          "valid leap day must pass");
    Check(!IsValidTradingDateYmd(20260229),
          "invalid non-leap day must fail");
    Check(!IsValidTradingDateYmd(20261301),
          "invalid trading month must fail");
    Check(!IsValidTradingDateYmd(0),
          "missing trading date must fail");

    constexpr EpochMillis Kst20260803MidnightUtcMs = 1785682800000LL;
    Check(
        KstTradingDateYmdFromEpoch(Kst20260803MidnightUtcMs) == 20260803,
        "KST midnight trading-date conversion mismatch");
    Check(
        KstTradingDateYmdFromEpoch(
            Kst20260803MidnightUtcMs + 23LL * 60LL * 60LL * 1000LL +
            59LL * 60LL * 1000LL + 59999LL) == 20260803,
        "KST end-of-day trading-date conversion mismatch");
    Check(
        KstTradingDateYmdFromEpoch(
            Kst20260803MidnightUtcMs + 24LL * 60LL * 60LL * 1000LL) ==
            20260804,
        "KST next-day trading-date conversion mismatch");
    Check(KstTradingDateYmdFromEpoch(0) == 0,
          "missing timestamp must not invent a trading date");

    std::puts("[PASS] trading_date_tests");
    return 0;
}
