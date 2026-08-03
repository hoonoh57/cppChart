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

        Pane pricePane;
        pricePane.id = workspaceId + ".price";
        pricePane.title = "Price";
        pricePane.heightWeight = 0.80f;

        CandleSeries candles;
        candles.id = seriesId + ".candles";
        candles.label = title;
        candles.bars.SetShared(
            source.completedBars,
            source.hasLiveBar ? &source.liveBar : nullptr);
        pricePane.candles.push_back(std::move(candles));

        Pane volumePane;
        volumePane.id = workspaceId + ".volume";
        volumePane.title = "Volume";
        volumePane.heightWeight = 0.20f;

        HistogramSeries volume;
        volume.id = seriesId + ".volume";
        volume.label = "Volume";

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
