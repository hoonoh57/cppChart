#include "market_chart_builder.h"

#include <utility>

namespace trading::render
{
    RenderDocument BuildMarketChartDocument(
        const std::string& workspaceId,
        const std::string& title,
        const std::string& seriesId,
        const MarketChartSource& source,
        std::uint64_t revision)
    {
        RenderDocument document;
        document.workspaceId = workspaceId;
        document.title = title;
        document.revision = revision;
        document.structureRevision = source.completedRevision;

        Pane pricePane;
        pricePane.id = "price";
        pricePane.title = "Price";
        pricePane.heightWeight = 0.80f;
        pricePane.valueDecimals = 0;
        pricePane.cursorGrid.enabled = true;
        pricePane.cursorGrid.bands = {
            { 2000.0, 1.0 },
            { 5000.0, 5.0 },
            { 20000.0, 10.0 },
            { 50000.0, 50.0 },
            { 200000.0, 100.0 },
            { 500000.0, 500.0 }
        };
        pricePane.cursorGrid.fallbackStep = 1000.0;

        LegendEntry priceLegend;
        priceLegend.id = seriesId + ".legend.price";
        priceLegend.label = title;
        priceLegend.color = { 232, 232, 238, 255 };
        priceLegend.selectable = false;
        pricePane.legends.push_back(std::move(priceLegend));

        CandleSeries candles;
        candles.id = seriesId + ".candles";
        candles.label = title;
        candles.ownerId = seriesId;
        candles.bars.SetShared(
            source.completedBars,
            source.hasLiveBar ? &source.liveBar : nullptr);
        pricePane.candles.push_back(std::move(candles));

        Pane volumePane;
        volumePane.id = "volume";
        volumePane.title = "Volume";
        volumePane.heightWeight = 0.20f;
        volumePane.valueDecimals = 0;
        volumePane.cursorGrid.enabled = true;
        volumePane.cursorGrid.fallbackStep = 1.0;

        LegendEntry volumeLegend;
        volumeLegend.id = seriesId + ".legend.volume";
        volumeLegend.label = "Volume";
        volumeLegend.color = { 170, 174, 188, 255 };
        volumeLegend.selectable = false;
        volumePane.legends.push_back(std::move(volumeLegend));

        HistogramSeries volume;
        volume.id = seriesId + ".volume";
        volume.label = "Volume";
        volume.ownerId = seriesId + ".volume";

        HistogramPoint liveVolume;
        const HistogramPoint* liveVolumePointer = nullptr;
        if (source.hasLiveBar) {
            liveVolume.timestampMs = source.liveBar.closeTimestampMs;
            liveVolume.value = static_cast<double>(source.liveBar.volume);
            liveVolume.positive =
                source.liveBar.close >= source.liveBar.open;
            liveVolumePointer = &liveVolume;
        }

        volume.points.SetShared(
            source.completedVolume,
            liveVolumePointer);
        volumePane.histograms.push_back(std::move(volume));

        document.panes.push_back(std::move(pricePane));
        document.panes.push_back(std::move(volumePane));
        return document;
    }
}
