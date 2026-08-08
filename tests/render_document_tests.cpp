#include "../render/render_document.h"

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

    void TestValidDocument()
    {
        trading::render::RenderDocument document;
        document.workspaceId = "main-chart";
        document.title = "005930";
        document.revision = 7;

        trading::render::Pane pricePane;
        pricePane.id = "price";
        pricePane.title = "Price";

        trading::render::LegendEntry legend;
        legend.id = "legend.sma.5";
        legend.ownerId = "sma.5";
        legend.label = "SMA 5";
        pricePane.legends.push_back(legend);

        trading::render::CandleSeries candles;
        candles.id = "stock.candles";
        candles.label = "005930";
        candles.bars.push_back(MakeBar(1000, 100, 110, 95, 105, 10));
        candles.bars.push_back(MakeBar(2000, 105, 115, 101, 112, 20));
        pricePane.candles.push_back(candles);

        trading::render::LineSeries average;
        average.id = "sma.5";
        average.label = "SMA 5";
        average.points.push_back({ 1000, 102.0 });
        average.points.push_back({ 2000, 108.0 });
        pricePane.lines.push_back(average);

        trading::render::Pane volumePane;
        volumePane.id = "volume";
        volumePane.title = "Volume";
        volumePane.heightWeight = 0.25f;

        trading::render::HistogramSeries volume;
        volume.id = "stock.volume";
        volume.label = "Volume";
        volume.points.push_back({ 1000, 10.0, true });
        volume.points.push_back({ 2000, 20.0, true });
        volumePane.histograms.push_back(volume);

        document.panes.push_back(pricePane);
        document.panes.push_back(volumePane);

        std::string error;
        Check(trading::render::ValidateRenderDocument(document, error),
              "valid render document was rejected");
    }

    void TestInvalidDocuments()
    {
        trading::render::RenderDocument empty;
        std::string error;
        Check(!trading::render::ValidateRenderDocument(empty, error),
              "empty render document must fail");

        trading::render::RenderDocument duplicate;
        duplicate.workspaceId = "main";
        trading::render::Pane pane;
        pane.id = "price";

        trading::render::LineSeries first;
        first.id = "same";
        first.points.push_back({ 1000, 1.0 });
        pane.lines.push_back(first);

        trading::render::ReferenceLine second;
        second.id = "same";
        second.value = 1.0;
        pane.referenceLines.push_back(second);
        duplicate.panes.push_back(pane);

        Check(!trading::render::ValidateRenderDocument(duplicate, error),
              "duplicate renderer element IDs must fail");

        trading::render::RenderDocument unordered;
        unordered.workspaceId = "main";
        trading::render::Pane unorderedPane;
        unorderedPane.id = "price";
        trading::render::CandleSeries unorderedCandles;
        unorderedCandles.id = "candles";
        unorderedCandles.bars.push_back(
            MakeBar(2000, 100, 110, 95, 105, 10));
        unorderedCandles.bars.push_back(
            MakeBar(1000, 105, 115, 101, 112, 20));
        unorderedPane.candles.push_back(unorderedCandles);
        unordered.panes.push_back(unorderedPane);

        Check(!trading::render::ValidateRenderDocument(unordered, error),
              "unordered candle timestamps must fail");

        trading::render::RenderDocument invalidLegend;
        invalidLegend.workspaceId = "main";
        trading::render::Pane legendPane;
        legendPane.id = "price";
        trading::render::LegendEntry selectable;
        selectable.id = "legend.invalid";
        selectable.label = "Invalid";
        selectable.selectable = true;
        legendPane.legends.push_back(selectable);
        invalidLegend.panes.push_back(legendPane);
        Check(!trading::render::ValidateRenderDocument(
                  invalidLegend, error),
              "selectable legend without owner must fail");
    }
}

int main()
{
    TestValidDocument();
    TestInvalidDocuments();
    std::puts("[PASS] render_document_tests");
    return 0;
}
