#pragma once

#include "render_document.h"

#include <cstdint>
#include <string>
#include <vector>

namespace trading::render
{
    RenderDocument BuildMarketChartDocument(
        const std::string& workspaceId,
        const std::string& title,
        const std::string& seriesId,
        const std::vector<Bar>& bars,
        std::uint64_t revision);
}
