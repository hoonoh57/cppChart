#include "../core/market_types.h"

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

    Bar parsed;
    parsed.open = 70000;
    parsed.high = 70100;
    parsed.low = 69900;
    parsed.close = 70050;
    parsed.volume = 1000;
    parsed.closeTimestampMs =
        Kst20260803MidnightUtcMs + 9LL * 60LL * 60LL * 1000LL;
    parsed.tickCount = 1;
    Check(parsed.tradingDateYmd == 0,
          "partially constructed bar must not mutate implicitly");

    std::vector<Bar> normalized;
    normalized.push_back(parsed);
    Check(normalized.back().tradingDateYmd == 20260803,
          "bar storage boundary must normalize missing trading date");

    Bar explicitInvalid = parsed;
    explicitInvalid.tradingDateYmd = 20260230;
    normalized.push_back(explicitInvalid);
    Check(normalized.back().tradingDateYmd == 20260230,
          "explicit invalid trading date must remain visible for fail-closed validation");

    const Bar aggregateCompatible(
        70000,
        70100,
        69900,
        70050,
        1000,
        Kst20260803MidnightUtcMs,
        1);
    Check(aggregateCompatible.tradingDateYmd == 20260803,
          "bar value constructor must normalize trading date");

    std::puts("[PASS] trading_date_tests");
    return 0;
}
