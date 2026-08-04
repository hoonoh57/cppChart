#pragma once

#include "../render/chart_viewport.h"
#include "../render/render_document.h"
#include "../render/time_axis.h"
#include "../render/time_boundaries.h"
#include "../render/value_viewport.h"

#include "imgui.h"

#include <map>
#include <string>
#include <vector>

namespace trading::ui
{
    struct RenderSurfaceState final
    {
        std::uint64_t renderedRevision = 0;
        bool dirty = true;
        render::ChartViewport viewport;
        render::OrdinalTimeAxis timeAxis;
        std::uint64_t timeAxisRevision = 0;
        double defaultVisibleSpan = 0.0;
        bool crosshairVisible = false;
        EpochMillis crosshairTimestampMs = 0;
        double crosshairValue = 0.0;
        std::uint64_t boundaryRevision = 0;
        std::vector<render::TimeBoundary> timeBoundaries;
        std::string selectedOwnerId;
        std::string selectedPaneId;
        std::string selectedLegendId;
        std::string selectedLegendLabel;
        bool selectionChanged = false;
        bool selectionDoubleClicked = false;
        std::map<std::string, float> paneHeightWeights;
        std::map<std::string, float> paneDefaultHeightWeights;
        std::map<std::string, render::ValueViewport> paneValueViewports;
        std::string activeValueAxisPaneId;
    };

    void DrawRenderDocument(
        const render::RenderDocument& document,
        ImVec2 size,
        RenderSurfaceState& surfaceState);
}
