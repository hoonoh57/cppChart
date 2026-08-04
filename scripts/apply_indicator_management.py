from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def write(path: str, content: str) -> None:
    (ROOT / path).write_text(content, encoding="utf-8-sig", newline="")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def regex_once(text: str, pattern: str, replacement: str, label: str) -> str:
    result, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return result


# ---------------------------------------------------------------------------
# Generic render contract: line styles and legend samples.
# ---------------------------------------------------------------------------
path = "render/render_document.h"
text = read(path)
text = replace_once(
    text,
    "    struct LegendEntry final\n",
    "    enum class LineStyle\n"
    "    {\n"
    "        Solid,\n"
    "        Dashed,\n"
    "        Dotted\n"
    "    };\n\n"
    "    struct LegendEntry final\n",
    "render line style enum")
text = replace_once(
    text,
    "        ColorRgba color;\n        bool selectable = true;\n",
    "        ColorRgba color;\n"
    "        float width = 1.0f;\n"
    "        LineStyle style = LineStyle::Solid;\n"
    "        bool selectable = true;\n",
    "legend style fields")
text = replace_once(
    text,
    "        ColorRgba color;\n        float width = 1.0f;\n        bool visible = true;\n        std::string ownerId;\n    };\n\n    struct HistogramSeries final",
    "        ColorRgba color;\n"
    "        float width = 1.0f;\n"
    "        LineStyle style = LineStyle::Solid;\n"
    "        bool visible = true;\n"
    "        std::string ownerId;\n"
    "    };\n\n"
    "    struct HistogramSeries final",
    "line series style field")
text = replace_once(
    text,
    "        ColorRgba color;\n        float width = 1.0f;\n        bool visible = true;\n        std::string ownerId;\n    };\n\n    struct TextAnnotation final",
    "        ColorRgba color;\n"
    "        float width = 1.0f;\n"
    "        LineStyle style = LineStyle::Solid;\n"
    "        bool visible = true;\n"
    "        std::string ownerId;\n"
    "    };\n\n"
    "    struct TextAnnotation final",
    "reference style field")
write(path, text)

path = "render/render_document.cpp"
text = read(path)
text = replace_once(
    text,
    "        bool ValidColor(const ColorRgba&) noexcept\n"
    "        {\n"
    "            return true;\n"
    "        }\n",
    "        bool ValidColor(const ColorRgba&) noexcept\n"
    "        {\n"
    "            return true;\n"
    "        }\n\n"
    "        bool ValidLineStyle(LineStyle style) noexcept\n"
    "        {\n"
    "            return\n"
    "                style == LineStyle::Solid ||\n"
    "                style == LineStyle::Dashed ||\n"
    "                style == LineStyle::Dotted;\n"
    "        }\n",
    "render style validator")
text = replace_once(
    text,
    "                if (!ValidColor(legend.color)) {\n"
    "                    error = \"render legend color is invalid: \" + legend.id;\n"
    "                    return false;\n"
    "                }\n",
    "                if (!ValidColor(legend.color) ||\n"
    "                    !std::isfinite(legend.width) ||\n"
    "                    legend.width <= 0.0f ||\n"
    "                    !ValidLineStyle(legend.style))\n"
    "                {\n"
    "                    error = \"render legend style is invalid: \" + legend.id;\n"
    "                    return false;\n"
    "                }\n",
    "legend style validation")
text = replace_once(
    text,
    "                if (!std::isfinite(series.width) || series.width <= 0.0f) {\n",
    "                if (!std::isfinite(series.width) || series.width <= 0.0f ||\n"
    "                    !ValidLineStyle(series.style)) {\n",
    "line style validation")
text = replace_once(
    text,
    "                if (!std::isfinite(line.value) ||\n"
    "                    !std::isfinite(line.width) ||\n"
    "                    line.width <= 0.0f)\n",
    "                if (!std::isfinite(line.value) ||\n"
    "                    !std::isfinite(line.width) ||\n"
    "                    line.width <= 0.0f ||\n"
    "                    !ValidLineStyle(line.style))\n",
    "reference style validation")
write(path, text)

# ---------------------------------------------------------------------------
# Indicator render-plan bindings carry style and legend-role metadata.
# ---------------------------------------------------------------------------
path = "app/indicator_render_adapter.h"
text = read(path)
text = replace_once(
    text,
    "        float width = 1.0f;\n"
    "        bool visible = true;\n"
    "        std::string legendLabel;\n",
    "        float width = 1.0f;\n"
    "        render::LineStyle style = render::LineStyle::Solid;\n"
    "        bool visible = true;\n"
    "        std::string legendRole;\n"
    "        std::string legendLabel;\n",
    "output style metadata")
text = replace_once(
    text,
    "        float width = 1.0f;\n"
    "        bool visible = true;\n"
    "        std::string indicatorId;\n",
    "        float width = 1.0f;\n"
    "        render::LineStyle style = render::LineStyle::Solid;\n"
    "        bool visible = true;\n"
    "        std::string indicatorId;\n",
    "reference style metadata")
write(path, text)

path = "app/indicator_render_adapter.cpp"
text = read(path)
text = replace_once(
    text,
    "            render::ColorRgba color,\n"
    "            bool visible,\n"
    "            std::string& error)\n",
    "            render::ColorRgba color,\n"
    "            float width,\n"
    "            render::LineStyle style,\n"
    "            bool visible,\n"
    "            std::string& error)\n",
    "legend helper signature")
text = replace_once(
    text,
    "            legend.color = color;\n"
    "            legend.selectable = true;\n",
    "            legend.color = color;\n"
    "            legend.width = width;\n"
    "            legend.style = style;\n"
    "            legend.selectable = true;\n",
    "legend style assignment")
text = replace_once(
    text,
    "                    binding.primaryColor,\n"
    "                    binding.visible,\n"
    "                    error))\n",
    "                    binding.primaryColor,\n"
    "                    binding.width,\n"
    "                    binding.style,\n"
    "                    binding.visible,\n"
    "                    error))\n",
    "legend style call")
text = text.replace(
    "                    line.width = binding.width;\n"
    "                    line.visible = binding.visible;\n",
    "                    line.width = binding.width;\n"
    "                    line.style = binding.style;\n"
    "                    line.visible = binding.visible;\n")
text = replace_once(
    text,
    "            line.width = reference.width;\n"
    "            line.visible = reference.visible;\n",
    "            line.width = reference.width;\n"
    "            line.style = reference.style;\n"
    "            line.visible = reference.visible;\n",
    "reference style assignment")
write(path, text)

# ---------------------------------------------------------------------------
# Default plan becomes a compatibility wrapper around dynamic definitions.
# ---------------------------------------------------------------------------
write(
    "app/default_indicator_render_plan.cpp",
    "#include \"default_indicator_render_plan.h\"\n\n"
    "#include \"indicator_configuration.h\"\n\n"
    "namespace trading::app\n"
    "{\n"
    "    bool BuildDefaultIndicatorRenderPlan(\n"
    "        const std::vector<indicators::IndicatorSpec>& specs,\n"
    "        IndicatorRenderPlan& plan,\n"
    "        std::string& error)\n"
    "    {\n"
    "        std::vector<IndicatorInstanceDefinition> definitions;\n"
    "        definitions.reserve(specs.size());\n"
    "        for (const indicators::IndicatorSpec& spec : specs) {\n"
    "            IndicatorInstanceDefinition definition;\n"
    "            if (!CreateIndicatorDefinition(spec, definition, error)) {\n"
    "                return false;\n"
    "            }\n"
    "            definitions.push_back(std::move(definition));\n"
    "        }\n"
    "        return BuildIndicatorRenderPlan(definitions, plan, error);\n"
    "    }\n"
    "}\n")

path = "app/indicator_workspace_coordinator.cpp"
text = read(path)
text = replace_once(
    text,
    "#include \"indicator_workspace_coordinator.h\"\n",
    "#include \"indicator_workspace_coordinator.h\"\n"
    "#include \"indicator_configuration.h\"\n",
    "coordinator configuration include")
text = regex_once(
    text,
    r"    std::vector<indicators::IndicatorSpec>\n"
    r"    InitialIndicatorSpecs\(\)\n"
    r"    \{.*?\n    \}\n\}",
    "    std::vector<indicators::IndicatorSpec>\n"
    "    InitialIndicatorSpecs()\n"
    "    {\n"
    "        return VisibleIndicatorSpecs(\n"
    "            InitialIndicatorDefinitions());\n"
    "    }\n"
    "}\n",
    "initial indicator specs wrapper")
write(path, text)

# ---------------------------------------------------------------------------
# Pure pane-resize state and generic styled rendering.
# ---------------------------------------------------------------------------
path = "ui/render_document_renderer.h"
text = read(path)
text = replace_once(
    text,
    "#include <string>\n#include <vector>\n",
    "#include <map>\n#include <string>\n#include <vector>\n",
    "renderer state map include")
text = replace_once(
    text,
    "        bool selectionDoubleClicked = false;\n",
    "        bool selectionDoubleClicked = false;\n"
    "        std::map<std::string, float> paneHeightWeights;\n",
    "pane height state")
write(path, text)

path = "ui/render_document_renderer.cpp"
text = read(path)
text = replace_once(
    text,
    "#include \"../render/cursor_label_layout.h\"\n",
    "#include \"../render/cursor_label_layout.h\"\n"
    "#include \"../render/pane_layout.h\"\n",
    "pane layout include")
text = replace_once(
    text,
    "        constexpr float LegendSpacing = 4.0f;\n"
    "        constexpr double MinimumVisibleSpan = 12.0;\n",
    "        constexpr float LegendSpacing = 4.0f;\n"
    "        constexpr float PaneSplitterHeight = 7.0f;\n"
    "        constexpr float MinimumPaneHeight = 48.0f;\n"
    "        constexpr double MinimumVisibleSpan = 12.0;\n",
    "pane splitter constants")
text = replace_once(
    text,
    "        struct AxisRange final\n",
    "        void DrawStyledLine(\n"
    "            ImDrawList* draw,\n"
    "            const ImVec2& start,\n"
    "            const ImVec2& end,\n"
    "            ImU32 color,\n"
    "            float width,\n"
    "            render::LineStyle style)\n"
    "        {\n"
    "            const float dx = end.x - start.x;\n"
    "            const float dy = end.y - start.y;\n"
    "            const float length = std::sqrt(dx * dx + dy * dy);\n"
    "            if (style == render::LineStyle::Solid || length < 1.0f) {\n"
    "                draw->AddLine(start, end, color, width);\n"
    "                return;\n"
    "            }\n"
    "            const float unitX = dx / length;\n"
    "            const float unitY = dy / length;\n"
    "            if (style == render::LineStyle::Dotted) {\n"
    "                const float spacing = (std::max)(4.0f, width * 3.0f);\n"
    "                const float radius = (std::max)(1.0f, width * 0.6f);\n"
    "                for (float distance = 0.0f; distance <= length; distance += spacing) {\n"
    "                    draw->AddCircleFilled(\n"
    "                        ImVec2(\n"
    "                            start.x + unitX * distance,\n"
    "                            start.y + unitY * distance),\n"
    "                        radius,\n"
    "                        color);\n"
    "                }\n"
    "                return;\n"
    "            }\n"
    "            const float dash = (std::max)(6.0f, width * 4.0f);\n"
    "            const float gap = (std::max)(4.0f, width * 2.5f);\n"
    "            for (float distance = 0.0f; distance < length; distance += dash + gap) {\n"
    "                const float finish = (std::min)(length, distance + dash);\n"
    "                draw->AddLine(\n"
    "                    ImVec2(\n"
    "                        start.x + unitX * distance,\n"
    "                        start.y + unitY * distance),\n"
    "                    ImVec2(\n"
    "                        start.x + unitX * finish,\n"
    "                        start.y + unitY * finish),\n"
    "                    color,\n"
    "                    width);\n"
    "            }\n"
    "        }\n\n"
    "        struct AxisRange final\n",
    "styled line helper")
text = replace_once(
    text,
    "                draw->AddLine(\n"
    "                    ImVec2(x + 5.0f, centerY),\n"
    "                    ImVec2(x + 17.0f, centerY),\n"
    "                    ToImColor(legend.color),\n"
    "                    selected ? 3.0f : 2.0f);\n",
    "                DrawStyledLine(\n"
    "                    draw,\n"
    "                    ImVec2(x + 5.0f, centerY),\n"
    "                    ImVec2(x + 17.0f, centerY),\n"
    "                    ToImColor(legend.color),\n"
    "                    legend.width + (selected ? 1.0f : 0.0f),\n"
    "                    legend.style);\n",
    "legend styled sample")
text = replace_once(
    text,
    "                        draw->AddLine(\n"
    "                            previous,\n"
    "                            current,\n"
    "                            ToImColor(series.color),\n"
    "                            series.width + (selected ? 1.5f : 0.0f));\n",
    "                        DrawStyledLine(\n"
    "                            draw,\n"
    "                            previous,\n"
    "                            current,\n"
    "                            ToImColor(series.color),\n"
    "                            series.width + (selected ? 1.5f : 0.0f),\n"
    "                            series.style);\n",
    "series styled line")
text = replace_once(
    text,
    "                draw->AddLine(\n"
    "                    ImVec2(plotOrigin.x, y),\n"
    "                    ImVec2(plotEnd.x, y),\n"
    "                    ToImColor(line.color),\n"
    "                    line.width + (selected ? 1.0f : 0.0f));\n",
    "                DrawStyledLine(\n"
    "                    draw,\n"
    "                    ImVec2(plotOrigin.x, y),\n"
    "                    ImVec2(plotEnd.x, y),\n"
    "                    ToImColor(line.color),\n"
    "                    line.width + (selected ? 1.0f : 0.0f),\n"
    "                    line.style);\n"
    "                if (!line.label.empty()) {\n"
    "                    draw->AddText(\n"
    "                        ImVec2(plotOrigin.x + 4.0f, y - 15.0f),\n"
    "                        ToImColor(line.color),\n"
    "                        line.label.c_str());\n"
    "                }\n",
    "reference styled line")
text = replace_once(
    text,
    "        void DrawPane(\n",
    "        void DrawPaneSplitter(\n"
    "            const render::Pane& upperPane,\n"
    "            const render::Pane& lowerPane,\n"
    "            float width,\n"
    "            float availableHeight,\n"
    "            float totalWeight,\n"
    "            RenderSurfaceState& state)\n"
    "        {\n"
    "            ImGui::PushID((\"splitter.\" + upperPane.id + \".\" + lowerPane.id).c_str());\n"
    "            ImGui::InvisibleButton(\n"
    "                \"##pane_splitter\",\n"
    "                ImVec2(width, PaneSplitterHeight),\n"
    "                ImGuiButtonFlags_MouseButtonLeft);\n"
    "            const bool hovered = ImGui::IsItemHovered();\n"
    "            const bool active = ImGui::IsItemActive();\n"
    "            if (hovered || active) {\n"
    "                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);\n"
    "            }\n"
    "            ImDrawList* draw = ImGui::GetWindowDrawList();\n"
    "            const ImVec2 minimum = ImGui::GetItemRectMin();\n"
    "            const ImVec2 maximum = ImGui::GetItemRectMax();\n"
    "            const float centerY = (minimum.y + maximum.y) * 0.5f;\n"
    "            draw->AddRectFilled(\n"
    "                minimum,\n"
    "                maximum,\n"
    "                active\n"
    "                    ? IM_COL32(76, 112, 166, 210)\n"
    "                    : hovered\n"
    "                        ? IM_COL32(64, 82, 112, 190)\n"
    "                        : IM_COL32(28, 30, 36, 255));\n"
    "            draw->AddLine(\n"
    "                ImVec2(minimum.x, centerY),\n"
    "                ImVec2(maximum.x, centerY),\n"
    "                hovered || active\n"
    "                    ? IM_COL32(150, 184, 235, 230)\n"
    "                    : IM_COL32(75, 78, 90, 180),\n"
    "                hovered || active ? 2.0f : 1.0f);\n"
    "            if (active && ImGui::GetIO().MouseDelta.y != 0.0f) {\n"
    "                float& upperWeight = state.paneHeightWeights[upperPane.id];\n"
    "                float& lowerWeight = state.paneHeightWeights[lowerPane.id];\n"
    "                if (render::AdjustAdjacentPaneWeights(\n"
    "                        availableHeight,\n"
    "                        MinimumPaneHeight,\n"
    "                        totalWeight,\n"
    "                        ImGui::GetIO().MouseDelta.y,\n"
    "                        upperWeight,\n"
    "                        lowerWeight))\n"
    "                {\n"
    "                    state.dirty = true;\n"
    "                }\n"
    "            }\n"
    "            ImGui::PopID();\n"
    "        }\n\n"
    "        void DrawPane(\n",
    "pane splitter renderer")
layout_pattern = (
    r"        float totalWeight = 0\.0f;\n"
    r"        for \(const render::Pane& pane : document\.panes\) \{.*?"
    r"        \}\n\n"
    r"        if \(\n"
    r"            surfaceState\.crosshairVisible")
layout_replacement = (
    "        for (const render::Pane& pane : document.panes) {\n"
    "            auto found = surfaceState.paneHeightWeights.find(pane.id);\n"
    "            if (found == surfaceState.paneHeightWeights.end() ||\n"
    "                !std::isfinite(found->second) || found->second <= 0.0f)\n"
    "            {\n"
    "                surfaceState.paneHeightWeights[pane.id] =\n"
    "                    (std::max)(0.01f, pane.heightWeight);\n"
    "            }\n"
    "        }\n\n"
    "        float totalWeight = 0.0f;\n"
    "        for (const render::Pane& pane : document.panes) {\n"
    "            totalWeight += surfaceState.paneHeightWeights[pane.id];\n"
    "        }\n\n"
    "        const float splitterSpace = PaneSplitterHeight *\n"
    "            static_cast<float>((std::max)(\n"
    "                static_cast<std::size_t>(0),\n"
    "                document.panes.size() - 1U));\n"
    "        const float availableHeight =\n"
    "            (std::max)(0.0f, size.y - splitterSpace);\n"
    "        std::vector<PaneGeometry> paneGeometries;\n"
    "        paneGeometries.reserve(document.panes.size());\n\n"
    "        for (std::size_t index = 0; index < document.panes.size(); ++index) {\n"
    "            const render::Pane& pane = document.panes[index];\n"
    "            const float paneHeight =\n"
    "                availableHeight *\n"
    "                surfaceState.paneHeightWeights[pane.id] /\n"
    "                totalWeight;\n"
    "            DrawPane(\n"
    "                pane,\n"
    "                surfaceState.timeAxis,\n"
    "                dataRange,\n"
    "                visibleRange,\n"
    "                surfaceState.defaultVisibleSpan,\n"
    "                index + 1 == document.panes.size(),\n"
    "                ImVec2(size.x, paneHeight),\n"
    "                surfaceState,\n"
    "                paneGeometries);\n"
    "            if (index + 1 < document.panes.size()) {\n"
    "                DrawPaneSplitter(\n"
    "                    pane,\n"
    "                    document.panes[index + 1U],\n"
    "                    size.x,\n"
    "                    availableHeight,\n"
    "                    totalWeight,\n"
    "                    surfaceState);\n"
    "            }\n"
    "        }\n\n"
    "        if (\n"
    "            surfaceState.crosshairVisible")
text = regex_once(text, layout_pattern, layout_replacement, "resizable pane layout")
write(path, text)

# ---------------------------------------------------------------------------
# Shell becomes composition-only and delegates feature-specific property UI.
# ---------------------------------------------------------------------------
path = "shell_main.cpp"
text = read(path)
text = replace_once(
    text,
    "#include \"app/default_indicator_render_plan.h\"\n"
    "#include \"app/indicator_properties.h\"\n"
    "#include \"app/indicator_workspace_coordinator.h\"\n",
    "#include \"app/default_indicator_render_plan.h\"\n"
    "#include \"app/indicator_configuration.h\"\n"
    "#include \"app/indicator_properties.h\"\n"
    "#include \"app/indicator_workspace_coordinator.h\"\n",
    "shell indicator configuration include")
text = replace_once(
    text,
    "#include \"ui/render_document_renderer.h\"\n",
    "#include \"ui/render_document_renderer.h\"\n"
    "#include \"ui/indicator_manager_ui.h\"\n",
    "shell indicator manager include")
text = regex_once(
    text,
    r"static std::vector<trading::indicators::IndicatorSpec> g_indicatorSpecs;\n"
    r"static std::string g_selectedIndicatorId;\n"
    r"static std::string g_indicatorPropertyDraftId;\n"
    r"static std::map<std::string, double> g_indicatorPropertyDraft;\n"
    r"static bool g_indicatorPropertyDirty = false;\n"
    r"static bool g_focusIndicatorProperties = false;\n"
    r"static std::string g_indicatorPropertyError;\n",
    "static std::vector<trading::indicators::IndicatorSpec> g_indicatorSpecs;\n"
    "static std::vector<trading::app::IndicatorInstanceDefinition>\n"
    "    g_indicatorDefinitions;\n"
    "static trading::ui::IndicatorManagerUiState g_indicatorManagerUi;\n",
    "shell indicator globals")
configuration_block = r"static bool InitializeIndicators\(std::string& error\).*?static const char\* KiwoomSessionStateLabel\("
configuration_replacement = (
    "static bool ApplyIndicatorConfiguration(\n"
    "    const std::vector<trading::app::IndicatorInstanceDefinition>& candidate,\n"
    "    std::string& error)\n"
    "{\n"
    "    const std::vector<trading::indicators::IndicatorSpec> specs =\n"
    "        trading::app::VisibleIndicatorSpecs(candidate);\n"
    "    trading::app::IndicatorRenderPlan plan;\n"
    "    if (!trading::app::BuildIndicatorRenderPlan(\n"
    "            candidate,\n"
    "            plan,\n"
    "            error))\n"
    "    {\n"
    "        return false;\n"
    "    }\n\n"
    "    trading::app::IndicatorRenderAdapter validationAdapter;\n"
    "    if (!validationAdapter.Configure(plan, error)) return false;\n"
    "    if (!g_indicatorModule.Configure(specs, error)) return false;\n"
    "    if (!g_indicatorRenderAdapter.Configure(plan, error)) return false;\n\n"
    "    g_indicatorSpecs = specs;\n"
    "    if (!g_mainRenderSurface.selectedOwnerId.empty() &&\n"
    "        trading::app::FindIndicatorDefinition(\n"
    "            candidate,\n"
    "            g_mainRenderSurface.selectedOwnerId) == nullptr)\n"
    "    {\n"
    "        g_mainRenderSurface.selectedOwnerId.clear();\n"
    "        g_mainRenderSurface.selectedPaneId.clear();\n"
    "        g_mainRenderSurface.selectedLegendId.clear();\n"
    "        g_mainRenderSurface.selectedLegendLabel.clear();\n"
    "    }\n"
    "    g_mainRenderSurface.dirty = true;\n"
    "    std::string healthError;\n"
    "    g_featureRegistry.SetHealth(\n"
    "        \"indicators\",\n"
    "        true,\n"
    "        {},\n"
    "        healthError);\n"
    "    WakeFrames(6);\n"
    "    error.clear();\n"
    "    return true;\n"
    "}\n\n"
    "static bool InitializeIndicators(std::string& error)\n"
    "{\n"
    "    const std::vector<trading::app::IndicatorInstanceDefinition>\n"
    "        definitions = trading::app::InitialIndicatorDefinitions();\n"
    "    if (!ApplyIndicatorConfiguration(definitions, error)) return false;\n"
    "    if (!g_indicatorModule.SetLevel(\n"
    "            trading::app::FeatureLevel::Visible,\n"
    "            error))\n"
    "    {\n"
    "        return false;\n"
    "    }\n\n"
    "    g_indicatorDefinitions = definitions;\n"
    "    g_indicatorManagerUi = {};\n"
    "    error.clear();\n"
    "    return true;\n"
    "}\n\n"
    "static const char* KiwoomSessionStateLabel(")
text = regex_once(text, configuration_block, configuration_replacement, "shell dynamic configuration block")
text = replace_once(
    text,
    "    if (FeatureAtLeast(\n"
    "            \"indicators\",\n"
    "            trading::app::FeatureLevel::Visible))\n",
    "    if (FeatureAtLeast(\n"
    "            \"indicators\",\n"
    "            trading::app::FeatureLevel::Visible) &&\n"
    "        !g_indicatorSpecs.empty())\n",
    "skip empty indicator configuration")
text = regex_once(
    text,
    r"    if \(g_mainRenderSurface\.selectionChanged\) \{.*?\n    \}\n"
    r"    const std::uint64_t elapsedMicros",
    "    if (g_mainRenderSurface.selectionChanged) {\n"
    "        trading::ui::SelectIndicator(\n"
    "            g_indicatorManagerUi,\n"
    "            g_mainRenderSurface.selectedOwnerId,\n"
    "            g_mainRenderSurface.selectionDoubleClicked);\n"
    "        WakeFrames(4);\n"
    "    }\n"
    "    const std::uint64_t elapsedMicros",
    "legend selection handoff")
text = regex_once(
    text,
    r"\nstatic void DrawIndicatorPropertiesWindow\(\)\n\{.*?\n\}\n\nstatic void DrawDashboard\(\)",
    "\nstatic void DrawDashboard()",
    "remove hardcoded property window")
text = replace_once(
    text,
    "        DrawIndicatorPropertiesWindow();\n",
    "        trading::ui::DrawIndicatorManagerWindow(\n"
    "            g_indicatorDefinitions,\n"
    "            g_indicatorManagerUi,\n"
    "            ApplyIndicatorConfiguration);\n",
    "draw indicator manager")
write(path, text)

# ui source needs numeric_limits.
path = "ui/indicator_manager_ui.cpp"
text = read(path)
text = replace_once(
    text,
    "#include <cstring>\n#include <set>\n",
    "#include <cstring>\n#include <limits>\n#include <set>\n",
    "indicator manager limits include")
write(path, text)

# ---------------------------------------------------------------------------
# Build and complete headless suite.
# ---------------------------------------------------------------------------
path = "build.bat"
text = read(path)
text = replace_once(
    text,
    "   app\\indicator_render_adapter.cpp ^\n"
    "   app\\default_indicator_render_plan.cpp ^\n",
    "   app\\indicator_render_adapter.cpp ^\n"
    "   app\\indicator_configuration.cpp ^\n"
    "   app\\default_indicator_render_plan.cpp ^\n",
    "build indicator configuration")
text = replace_once(
    text,
    "   render\\series_geometry.cpp ^\n"
    "   render\\time_boundaries.cpp ^\n",
    "   render\\series_geometry.cpp ^\n"
    "   render\\pane_layout.cpp ^\n"
    "   render\\time_boundaries.cpp ^\n",
    "build pane layout")
text = replace_once(
    text,
    "   ui\\render_document_renderer.cpp ^\n",
    "   ui\\render_document_renderer.cpp ^\n"
    "   ui\\indicator_manager_ui.cpp ^\n",
    "build indicator manager UI")
write(path, text)

path = "tests/run_all.bat"
text = read(path)
text = replace_once(
    text,
    "call :build_and_run default_indicator_render_plan_tests.exe \"tests\\default_indicator_render_plan_tests.cpp app\\default_indicator_render_plan.cpp app\\indicator_properties.cpp app\\indicator_render_adapter.cpp render\\render_document.cpp\"\n",
    "call :build_and_run default_indicator_render_plan_tests.exe \"tests\\default_indicator_render_plan_tests.cpp app\\default_indicator_render_plan.cpp app\\indicator_configuration.cpp app\\indicator_properties.cpp app\\indicator_render_adapter.cpp render\\render_document.cpp\"\n",
    "default plan test sources")
text = replace_once(
    text,
    "call :build_and_run indicator_properties_tests.exe \"tests\\indicator_properties_tests.cpp app\\indicator_properties.cpp\"\n"
    "if errorlevel 1 exit /b 1\n",
    "call :build_and_run indicator_properties_tests.exe \"tests\\indicator_properties_tests.cpp app\\indicator_properties.cpp\"\n"
    "if errorlevel 1 exit /b 1\n\n"
    "call :build_and_run indicator_configuration_tests.exe \"tests\\indicator_configuration_tests.cpp app\\indicator_configuration.cpp app\\indicator_properties.cpp app\\indicator_render_adapter.cpp render\\render_document.cpp\"\n"
    "if errorlevel 1 exit /b 1\n",
    "indicator configuration tests")
text = replace_once(
    text,
    "call :build_and_run series_geometry_tests.exe \"tests\\series_geometry_tests.cpp render\\series_geometry.cpp\"\n"
    "if errorlevel 1 exit /b 1\n",
    "call :build_and_run series_geometry_tests.exe \"tests\\series_geometry_tests.cpp render\\series_geometry.cpp\"\n"
    "if errorlevel 1 exit /b 1\n\n"
    "call :build_and_run pane_layout_tests.exe \"tests\\pane_layout_tests.cpp render\\pane_layout.cpp\"\n"
    "if errorlevel 1 exit /b 1\n\n"
    "call :build_and_run render_style_tests.exe \"tests\\render_style_tests.cpp render\\render_document.cpp\"\n"
    "if errorlevel 1 exit /b 1\n",
    "pane layout and style tests")
write(path, text)

# ---------------------------------------------------------------------------
# Static architecture gates for the new invariant.
# ---------------------------------------------------------------------------
path = "scripts/verify_modular_architecture.ps1"
text = read(path)
text = replace_once(
    text,
    "    '.\\app\\indicator_module.cpp',\n",
    "    '.\\app\\indicator_module.cpp',\n"
    "    '.\\app\\indicator_configuration.h',\n"
    "    '.\\app\\indicator_configuration.cpp',\n"
    "    '.\\ui\\indicator_manager_ui.h',\n"
    "    '.\\ui\\indicator_manager_ui.cpp',\n"
    "    '.\\render\\pane_layout.h',\n"
    "    '.\\render\\pane_layout.cpp',\n",
    "required indicator management files")
insert = r'''
$indicatorConfiguration = Get-Content '.\app\indicator_configuration.cpp' -Raw
foreach ($marker in @(
    'IndicatorCatalog()',
    'CreateDefaultIndicatorDefinition(',
    'VisibleIndicatorSpecs(',
    'BuildIndicatorRenderPlan(',
    'DuplicateIndicatorDefinition(',
    'MoveIndicatorToPane(')) {
    if (-not $indicatorConfiguration.Contains($marker)) {
        throw "Dynamic indicator configuration contract is missing: $marker"
    }
}

$indicatorManagerUi = Get-Content '.\ui\indicator_manager_ui.cpp' -Raw
foreach ($marker in @(
    'DrawIndicatorManagerWindow(',
    '지표 추가',
    '복제',
    '감추기',
    '기준선 추가',
    '과매수 추가',
    '과매도 추가',
    'ColorEdit4(',
    'EditLineStyle(',
    'EditPaneSelection(')) {
    if (-not $indicatorManagerUi.Contains($marker)) {
        throw "Indicator management UI contract is missing: $marker"
    }
}

foreach ($marker in @(
    'DrawPaneSplitter(',
    'ImGuiMouseCursor_ResizeNS',
    'paneHeightWeights',
    'DrawStyledLine(')) {
    if (-not $renderer.Contains($marker)) {
        throw "Resizable/styled pane renderer contract is missing: $marker"
    }
}

if ($shell.Contains('DrawIndicatorPropertiesWindow')) {
    throw 'Hardcoded indicator property window remains in shell_main.cpp'
}
foreach ($marker in @(
    'IndicatorInstanceDefinition',
    'DrawIndicatorManagerWindow(',
    'ApplyIndicatorConfiguration(')) {
    if (-not $shell.Contains($marker)) {
        throw "Shell dynamic indicator composition marker is missing: $marker"
    }
}

foreach ($marker in @(
    'indicator_configuration_tests.exe',
    'pane_layout_tests.exe',
    'render_style_tests.exe')) {
    if (-not $runAll.Contains($marker)) {
        throw "Dynamic indicator regression test is not in run_all: $marker"
    }
}

'''
text = replace_once(
    text,
    "Write-Host 'Major-feature modules, accepted M6 chart contracts, and the M7 SMA/JMA/OBV/ADX/VWAP engine plus IndicatorModule live-tail cache are verified.'\n",
    insert + "Write-Host 'Major-feature modules, accepted M6 chart contracts, and dynamic M7 indicator management are verified.'\n",
    "dynamic indicator architecture gates")
write(path, text)

print("indicator management upgrade applied")
