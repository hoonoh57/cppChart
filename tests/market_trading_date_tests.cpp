#include "../core/json_lite.h"
#include "../core/kiwoom_market_data.h"

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
}

int main()
{
    using namespace trading;

    const std::string json =
        "{\"return_code\":0,\"stk_min_pole_chart_qry\":["
        "{\"cur_prc\":\"70000\",\"trde_qty\":\"100\","
        "\"cntr_tm\":\"20260803090000\","
        "\"open_pric\":\"69900\",\"high_pric\":\"70100\","
        "\"low_pric\":\"69800\"},"
        "{\"cur_prc\":\"71000\",\"trde_qty\":\"200\","
        "\"cntr_tm\":\"20260804090000\","
        "\"open_pric\":\"70900\",\"high_pric\":\"71100\","
        "\"low_pric\":\"70800\"}]}";

    const MinuteBarsPage page =
        ParseStockMinuteBarsResponse("005930", 1, json);
    Check(page.result.ok,
          "minute-bar parser must accept explicit two-day fixture");
    Check(page.bars.size() == 2U,
          "minute-bar trading-date fixture count mismatch");
    Check(page.bars[0].tradingDateYmd == 20260803,
          "REST minute bar must normalize first KST trading date");
    Check(page.bars[1].tradingDateYmd == 20260804,
          "REST minute bar must normalize second KST trading date");

    constexpr EpochMillis SessionStart = 1785682800000LL;
    std::vector<Bar> liveBars;
    Bar current;
    current.open = 70000;
    current.high = 70100;
    current.low = 69900;
    current.close = 70050;
    current.volume = 100;
    current.closeTimestampMs =
        SessionStart + 9LL * 60LL * 60LL * 1000LL;
    liveBars.push_back(current);
    Check(liveBars.back().tradingDateYmd == 20260803,
          "live backfill storage must normalize trading date");

    StockTradeTick tick;
    tick.code = "005930";
    tick.priceWon = 70200;
    tick.tradeVolume = 10;
    tick.tradeTimeHhmmss = 90100;

    std::string error;
    Check(MergeStockTradeIntoMinuteBars(
              liveBars,
              1,
              SessionStart,
              tick,
              error),
          "next-minute live trade must merge");
    Check(liveBars.size() == 2U,
          "next-minute live trade must append one bar");
    Check(liveBars.back().tradingDateYmd == 20260803,
          "real-time created bar must inherit normalized KST trading date");

    std::puts("[PASS] market_trading_date_tests");
    return 0;
}
