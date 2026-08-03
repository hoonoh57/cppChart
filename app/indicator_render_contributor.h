#pragma once

#include "indicator_module.h"
#include "../render/render_document.h"

#include <cstddef>
#include <string>

namespace trading::app
{
    struct IndicatorRenderContributionStats final
    {
        std::size_t paneCount = 0;
        std::size_t lineSeriesCount = 0;
        std::size_t histogramSeriesCount = 0;
        std::size_t referenceLineCount = 0;

        std::size_t TotalSeries() const noexcept
        {
            return
                lineSeriesCount +
                histogramSeriesCount +
                referenceLineCount;
        }
    };

    bool AppendIndicatorRenderContributions(
        const IndicatorModuleSnapshot& snapshot,
        render::RenderDocument& document,
        IndicatorRenderContributionStats& stats,
        std::string& error);
}
