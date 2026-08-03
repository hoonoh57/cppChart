#pragma once

#include "../render/render_document.h"

#include "imgui.h"

namespace trading::ui
{
    struct RenderSurfaceState final
    {
        std::uint64_t renderedRevision = 0;
        bool dirty = true;
    };

    void DrawRenderDocument(
        const render::RenderDocument& document,
        ImVec2 size,
        RenderSurfaceState& surfaceState);
}
