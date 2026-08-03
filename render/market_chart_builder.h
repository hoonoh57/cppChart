#pragma once

#include "render_document.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace trading::render
{
    struct MarketChartSource final
    {
        std::shared_ptr<const std::vector<Bar>> completedBars;
        Bar liveBar;
        bool hasLiveBar = false;
        std::shared_ptr<const std::vector<HistogramPoint>> completedVolume;
    };

    RenderDocument BuildMarketChartDocument(
        const std::string& workspaceId,
        const std::string& title,
        const std::string& seriesId,
        const MarketChartSource& source,
        std::uint64_t revision);
}
