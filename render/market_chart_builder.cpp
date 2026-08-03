#include "market_chart_builder.h"

#include <utility>

namespace trading::render
{
    RenderDocument BuildMarketChartDocument(
        const std::string& workspaceId,
        const std::string& title,
        const std::string& seriesId,
        const std::vector<Bar>& bars,
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
        candles.bars = bars;
        pricePane.candles.push_back(std::move(candles));

        Pane volumePane;
        volumePane.id = workspaceId + ".volume";
        volumePane.title = "Volume";
        volumePane.heightWeight = 0.20f;

        HistogramSeries volume;
        volume.id = seriesId + ".volume";
        volume.label = "Volume";
        volume.points.reserve(bars.size());
        for (const Bar& bar : bars) {
            HistogramPoint point;
            point.timestampMs = bar.closeTimestampMs;
            point.value = static_cast<double>(bar.volume);
            point.positive = bar.close >= bar.open;
            volume.points.push_back(point);
        }
        volumePane.histograms.push_back(std::move(volume));

        document.panes.push_back(std::move(pricePane));
        document.panes.push_back(std::move(volumePane));
        return document;
    }
}
