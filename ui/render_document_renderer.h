#pragma once

#include "../render/chart_viewport.h"
#include "../render/render_document.h"

#include "imgui.h"

namespace trading::ui
{
    struct RenderSurfaceState final
    {
        std::uint64_t renderedRevision = 0;
        bool dirty = true;
        render::ChartViewport viewport;
        bool crosshairVisible = false;
        EpochMillis crosshairTimestampMs = 0;
        double crosshairValue = 0.0;
    };

    void DrawRenderDocument(
        const render::RenderDocument& document,
        ImVec2 size,
        RenderSurfaceState& surfaceState);
}
