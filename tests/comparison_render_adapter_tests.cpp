#include "../app/comparison_render_adapter.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
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

    trading::Bar MakeBar(int index, int close)
    {
        trading::Bar bar;
        bar.open = close - 1;
        bar.high = close + 2;
        bar.low = close - 2;
        bar.close = close;
        bar.volume = 100 + index;
        bar.closeTimestampMs = 1000LL + index * 60000LL;
        bar.tradingDateYmd = 20260804;
        return bar;
    }

    trading::render::Pane* FindPane(
        trading::render::RenderDocument& document,
        const std::string& id)
    {
        for (auto& pane : document.panes) {
            if (pane.id == id) return &pane;
        }
        return nullptr;
    }
}

int main()
{
    using namespace trading;
    using namespace trading::app;

    render::RenderDocument document;
    document.workspaceId = "compare-test";
    document.title = "Compare";
    document.revision = 1;
    document.structureRevision = 1;

    render::Pane price;
    price.id = "price";
    price.title = "Price";
    render::CandleSeries candles;
    candles.id = "main.candles";
    candles.bars = std::vector<Bar>{ MakeBar(0, 70000), MakeBar(1, 70100) };
    price.candles.push_back(candles);
    document.panes.push_back(price);

    ComparisonModuleSnapshot snapshot;
    snapshot.level = FeatureLevel::Visible;
    snapshot.revision = 10;

    ComparisonSeriesSnapshot index;
    index.definition.id = "compare.index.1";
    index.definition.kind = ComparisonInstrumentKind::Index;
    index.definition.code = "001";
    index.definition.displayName = "KOSPI";
    index.definition.placement = ComparisonPlacement::PriceSecondaryAxis;
    index.definition.valueDivisor = 100.0;
    index.definition.valueDecimals = 2;
    index.state = ComparisonSeriesState::Ready;
    index.completedBars = std::make_shared<const std::vector<Bar>>(
        std::vector<Bar>{ MakeBar(0, 280000) });
    index.liveBar = MakeBar(1, 280100);
    index.hasLiveBar = true;
    index.barCount = 2;
    index.revision = 2;
    index.completedRevision = 1;
    index.liveRevision = 1;
    snapshot.series.push_back(index);

    ComparisonSeriesSnapshot stock;
    stock.definition.id = "compare.stock.1";
    stock.definition.kind = ComparisonInstrumentKind::Stock;
    stock.definition.code = "005930";
    stock.definition.displayName = "삼성전자";
    stock.definition.placement = ComparisonPlacement::SeparatePane;
    stock.definition.paneId = "comparison.shared";
    stock.definition.paneTitle = "Comparison";
    stock.state = ComparisonSeriesState::Ready;
    stock.completedBars = std::make_shared<const std::vector<Bar>>(
        std::vector<Bar>{ MakeBar(0, 80000) });
    stock.liveBar = MakeBar(1, 80100);
    stock.hasLiveBar = true;
    stock.barCount = 2;
    stock.revision = 3;
    stock.completedRevision = 1;
    stock.liveRevision = 1;
    snapshot.series.push_back(stock);

    ComparisonRenderAdapter adapter;
    std::string error;
    Check(adapter.Apply(snapshot, document, error),
          "comparison render contribution must apply");

    render::Pane* pricePane = FindPane(document, "price");
    Check(pricePane != nullptr, "price pane missing");
    Check(pricePane->valueAxes.size() == 1U,
          "dual-axis comparison must publish one secondary axis");
    Check(pricePane->valueAxes.front().side == render::ValueAxisSide::Left,
          "comparison secondary axis must be left-sided");
    Check(pricePane->lines.size() == 1U,
          "dual-axis comparison close line missing");
    Check(pricePane->lines.front().axisId ==
              pricePane->valueAxes.front().id,
          "comparison line must reference its secondary axis");
    Check(pricePane->lines.front().points.back().value == 2801.0,
          "index divisor must be applied to close values");

    render::Pane* comparePane = FindPane(document, "comparison.shared");
    Check(comparePane != nullptr,
          "separate comparison pane must be created");
    Check(comparePane->lines.size() == 1U,
          "separate comparison close line missing");
    Check(comparePane->lines.front().axisId.empty(),
          "separate comparison must use pane primary axis");

    Check(render::ValidateRenderDocument(document, error),
          "comparison render document must validate");
    Check(adapter.RetainedBytes() > 0U,
          "comparison adapter must report cached points");

    const auto cached = pricePane->lines.front().points.SharedPrefix();
    render::RenderDocument next = document;
    next.panes.clear();
    next.panes.push_back(price);
    snapshot.series.front().liveBar.close = 280200;
    ++snapshot.series.front().liveRevision;
    ++snapshot.revision;
    Check(adapter.Apply(snapshot, next, error),
          "comparison live-only update must apply");
    pricePane = FindPane(next, "price");
    Check(pricePane != nullptr, "next price pane missing");
    Check(pricePane->lines.front().points.SharedPrefix() == cached,
          "completed comparison points must be reused on live update");

    std::puts("[PASS] comparison_render_adapter_tests");
    return 0;
}
