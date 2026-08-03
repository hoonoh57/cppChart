#include "../app/market_data_module.h"

#include <cstdio>
#include <cstdlib>
#include <string>

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
        trading::EpochMillis timestamp,
        trading::PriceWon open,
        trading::PriceWon high,
        trading::PriceWon low,
        trading::PriceWon close,
        trading::Volume volume)
    {
        trading::Bar bar;
        bar.closeTimestampMs = timestamp;
        bar.open = open;
        bar.high = high;
        bar.low = low;
        bar.close = close;
        bar.volume = volume;
        bar.tickCount = 1;
        return bar;
    }

    trading::MinuteBarsPage MakePage()
    {
        constexpr trading::EpochMillis KstDateStart = 1785682800000LL;

        trading::MinuteBarsPage page;
        page.result.ok = true;
        page.result.returnCode = 0;
        page.code = "000660";
        page.minuteUnit = 1;
        page.bars.push_back(MakeBar(
            KstDateStart + 9 * 60 * 60 * 1000LL,
            158000,
            158500,
            157500,
            158200,
            100));
        page.bars.push_back(MakeBar(
            KstDateStart + (9 * 60 + 1) * 60 * 1000LL,
            158200,
            158600,
            158000,
            158300,
            120));
        return page;
    }

    trading::StockTradeTick MakeSameMinuteTick()
    {
        trading::StockTradeTick tick;
        tick.code = "000660";
        tick.priceWon = 158700;
        tick.tradeVolume = 5;
        tick.tradeTimeHhmmss = 90130;
        return tick;
    }

    void TestRequestPageAndVisibleRange()
    {
        trading::app::MarketDataModule module;
        std::string error;

        Check(module.BeginRequest("000660", 1, error),
              "market-data request start failed");
        Check(module.Snapshot().state ==
                  trading::app::MarketDataState::Loading,
              "market-data state must become Loading");

        trading::Continuation continuation;
        continuation.continueYn = "Y";
        continuation.nextKey = "next";
        const trading::app::MarketDataApplyResult result =
            module.ApplyMinuteBars(MakePage(), continuation);

        Check(result.applied, "minute-bar page was not applied");
        Check(result.latestPriceWon == 158300,
              "minute-bar latest price mismatch");

        const trading::app::MarketDataSnapshot snapshot =
            module.Snapshot();
        Check(snapshot.state == trading::app::MarketDataState::Ready,
              "market-data state must become Ready");
        Check(snapshot.barCount == 2,
              "market-data bar count mismatch");
        Check(snapshot.hasLatestBar && snapshot.latestBar.close == 158300,
              "market-data latest bar mismatch");
        Check(snapshot.continuation.nextKey == "next",
              "market-data continuation mismatch");

        const std::vector<trading::Bar> one =
            module.CopyVisibleBars(1);
        Check(one.size() == 1 && one.front().close == 158300,
              "visible-range copy must return only the requested tail");

        std::string quoteCode;
        trading::PriceWon quote = 0;
        Check(module.TryGetLatestQuote(quoteCode, quote),
              "latest quote must be available");
        Check(quoteCode == "000660" && quote == 158300,
              "latest quote mismatch");
    }

    void TestRealTimeMergeAndFeatureLevels()
    {
        trading::app::MarketDataModule module;
        std::string error;
        Check(module.BeginRequest("000660", 1, error),
              "request start failed");
        Check(module.ApplyMinuteBars(MakePage(), {}).applied,
              "minute bars were not applied");

        module.SetStockTradeSubscriptionRequested(true);
        const trading::StockTradeTick sameMinute = MakeSameMinuteTick();

        const trading::app::MarketDataApplyResult updated =
            module.ApplyStockTradeTick(sameMinute);
        Check(updated.applied, "same-minute trade was not applied");
        Check(updated.latestPriceWon == 158700,
              "same-minute trade latest price mismatch");
        Check(module.Snapshot().stockTradeTickCount == 1,
              "real-time tick count mismatch");
        Check(module.Snapshot().latestBar.high == 158700,
              "same-minute high was not updated");

        trading::StockTradeTick nextMinute = sameMinute;
        nextMinute.priceWon = 158900;
        nextMinute.tradeTimeHhmmss = 90201;
        const trading::app::MarketDataApplyResult appended =
            module.ApplyStockTradeTick(nextMinute);
        Check(appended.applied, "next-minute trade was not applied");
        Check(module.Snapshot().barCount == 3,
              "next-minute trade must append a bar");
        Check(module.Snapshot().latestBar.tickCount == 1,
              "new live bar tick count mismatch");

        Check(module.SetLevel(
                  trading::app::FeatureLevel::Standby,
                  error),
              "market-data Standby transition failed");
        const trading::app::MarketDataSnapshot standby = module.Snapshot();
        Check(standby.barCount == 3,
              "Standby must retain cached bars");
        Check(!standby.stockTradeSubscriptionRequested,
              "Standby must clear subscription intent");
        Check(module.CopyVisibleBars(10).empty(),
              "Standby must stop renderer bar copies");
        Check(!module.BeginRequest("005930", 1, error),
              "Standby must reject new REST requests");
        Check(module.ApplyStockTradeTick(nextMinute).stale,
              "Standby must ignore real-time tick calculation");
        Check(module.Snapshot().stockTradeTickCount == 2,
              "Standby tick must not alter tick metrics");

        std::string quoteCode;
        trading::PriceWon quote = 0;
        Check(!module.TryGetLatestQuote(quoteCode, quote),
              "Standby must not expose an entry quote");

        Check(module.SetLevel(
                  trading::app::FeatureLevel::Visible,
                  error),
              "market-data Visible transition failed");
        Check(module.CopyVisibleBars(10).size() == 3,
              "Visible must restore renderer access to retained bars");
        Check(module.TryGetLatestQuote(quoteCode, quote),
              "Visible must restore the latest quote");

        Check(module.SetLevel(
                  trading::app::FeatureLevel::Off,
                  error),
              "market-data Off transition failed");
        const trading::app::MarketDataSnapshot off = module.Snapshot();
        Check(off.state == trading::app::MarketDataState::Disconnected,
              "Off market-data must be disconnected");
        Check(off.barCount == 0,
              "Off market-data must release bars");
        Check(off.retainedBytes == 0,
              "Off market-data must release retained capacity");
        Check(!module.BeginRequest("000660", 1, error),
              "Off market-data must reject requests");
    }

    void TestStalePageAndTick()
    {
        trading::app::MarketDataModule module;
        std::string error;
        Check(module.BeginRequest("000660", 1, error),
              "request start failed");

        trading::MinuteBarsPage wrong = MakePage();
        wrong.code = "005930";
        const trading::app::MarketDataApplyResult stalePage =
            module.ApplyMinuteBars(wrong, {});
        Check(stalePage.stale,
              "old-symbol minute page must be marked stale");

        Check(module.ApplyMinuteBars(MakePage(), {}).applied,
              "correct page was not applied");

        trading::StockTradeTick wrongTick;
        wrongTick.code = "005930";
        wrongTick.priceWon = 70000;
        wrongTick.tradeVolume = 1;
        wrongTick.tradeTimeHhmmss = 90130;
        const trading::app::MarketDataApplyResult staleTick =
            module.ApplyStockTradeTick(wrongTick);
        Check(staleTick.stale,
              "other-symbol trade must be marked stale");
        Check(module.Snapshot().stockTradeTickCount == 0,
              "stale trade must not increment tick count");
    }
}

int main()
{
    TestRequestPageAndVisibleRange();
    TestRealTimeMergeAndFeatureLevels();
    TestStalePageAndTick();
    std::puts("[PASS] market_data_module_tests");
    return 0;
}
