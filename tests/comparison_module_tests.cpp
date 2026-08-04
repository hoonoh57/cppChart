#include "../app/comparison_module.h"

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

    trading::Bar BarAt(int index, int close)
    {
        trading::Bar bar;
        bar.open = close - 1;
        bar.high = close + 2;
        bar.low = close - 2;
        bar.close = close;
        bar.volume = 1000 + index;
        bar.closeTimestampMs = 1785801600000LL + index * 60000LL;
        bar.tradingDateYmd = 20260804;
        return bar;
    }

    trading::MinuteBarsPage Page(
        trading::MinuteBarInstrument instrument,
        const std::string& code,
        int base)
    {
        trading::MinuteBarsPage page;
        page.result.ok = true;
        page.result.returnCode = 0;
        page.instrument = instrument;
        page.code = code;
        page.minuteUnit = 1;
        page.bars = { BarAt(0, base), BarAt(1, base + 10) };
        return page;
    }

    const trading::app::ComparisonSeriesSnapshot* Find(
        const trading::app::ComparisonModuleSnapshot& snapshot,
        const std::string& id)
    {
        for (const auto& series : snapshot.series) {
            if (series.definition.id == id) return &series;
        }
        return nullptr;
    }
}

int main()
{
    using namespace trading::app;

    ComparisonDefinition stock;
    stock.id = "compare.stock.1";
    stock.kind = ComparisonInstrumentKind::Stock;
    stock.code = "005930";
    stock.displayName = "삼성전자";
    stock.paneId = "compare.shared";
    stock.paneTitle = "Comparison";

    ComparisonDefinition index;
    index.id = "compare.index.1";
    index.kind = ComparisonInstrumentKind::Index;
    index.code = "001";
    index.displayName = "KOSPI";
    index.placement = ComparisonPlacement::PriceSecondaryAxis;
    index.valueDivisor = 100.0;

    ComparisonModule module;
    std::string error;
    Check(module.Configure({ stock, index }, error),
          "comparison definitions must configure");
    Check(module.BeginRequest(stock.id, 1, error),
          "stock comparison request must begin");
    Check(module.BeginRequest(index.id, 1, error),
          "index comparison request must begin");

    auto stockApplied = module.ApplyMinuteBars(
        Page(trading::MinuteBarInstrument::Stock, "005930", 70000),
        {});
    Check(stockApplied.applied, "stock comparison bars must apply");
    auto indexApplied = module.ApplyMinuteBars(
        Page(trading::MinuteBarInstrument::Index, "001", 280000),
        {});
    Check(indexApplied.applied, "index comparison bars must apply");

    ComparisonModuleSnapshot snapshot = module.Snapshot();
    Check(snapshot.series.size() == 2U,
          "comparison snapshot series count mismatch");
    const auto* stockSeries = Find(snapshot, stock.id);
    const auto* indexSeries = Find(snapshot, index.id);
    Check(stockSeries != nullptr && stockSeries->barCount == 2U,
          "stock comparison snapshot mismatch");
    Check(indexSeries != nullptr && indexSeries->barCount == 2U,
          "index comparison snapshot mismatch");

    trading::StockTradeTick stockTick;
    stockTick.code = "005930";
    stockTick.tradeTimeHhmmss = 90001;
    stockTick.priceWon = 70020;
    stockTick.tradeVolume = 3;
    stockTick.cumulativeVolume = 1013;
    auto stockRealtime = module.ApplyStockTradeTick(stockTick);
    Check(stockRealtime.applied,
          "stock comparison 0B must replace the KST live minute");

    trading::IndexValueTick indexTick;
    indexTick.code = "001";
    indexTick.tradeTimeHhmmss = 90001;
    indexTick.value = 280050;
    indexTick.tradeVolume = 2;
    indexTick.cumulativeVolume = 1002;
    auto indexRealtime = module.ApplyIndexValueTick(indexTick);
    Check(indexRealtime.applied,
          "index comparison 0I must replace the KST live minute");

    snapshot = module.Snapshot();
    stockSeries = Find(snapshot, stock.id);
    indexSeries = Find(snapshot, index.id);
    Check(stockSeries != nullptr && stockSeries->liveBar.close == 70020,
          "stock live close replacement mismatch");
    Check(indexSeries != nullptr && indexSeries->liveBar.close == 280050,
          "index live close replacement mismatch");

    const std::uint64_t beforeStyleRevision = snapshot.revision;
    stock.color = { 255, 196, 64, 255 };
    stock.width = 2.5f;
    Check(module.Configure({ stock, index }, error),
          "comparison style reconfiguration must succeed");
    snapshot = module.Snapshot();
    stockSeries = Find(snapshot, stock.id);
    Check(stockSeries != nullptr && stockSeries->barCount == 2U,
          "style change must preserve comparison data");
    Check(snapshot.revision > beforeStyleRevision,
          "style change must advance comparison revision");

    ComparisonDefinition invalid = stock;
    invalid.id.clear();
    Check(!module.Configure({ invalid }, error),
          "empty comparison ID must fail closed");

    Check(module.SetLevel(FeatureLevel::Off, error),
          "comparison module must enter Off");
    snapshot = module.Snapshot();
    Check(snapshot.series.front().state == ComparisonSeriesState::Empty,
          "Off comparison module must release data state");

    std::puts("[PASS] comparison_module_tests");
    return 0;
}
