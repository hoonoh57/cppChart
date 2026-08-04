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


path = "ui/render_document_renderer.cpp"
text = read(path)
text = replace_once(
    text,
    '#include "../render/value_grid.h"\n',
    '#include "../render/value_grid.h"\n#include "../render/value_viewport.h"\n',
    "value viewport include")
text = replace_once(
    text,
    "        constexpr double MinimumVisibleSpan = 12.0;\n",
    "        constexpr double MinimumVisibleSpan = 12.0;\n"
    "        constexpr double LatestRightPaddingFraction = 0.12;\n"
    "        constexpr double MaximumRightOverscrollFraction = 0.60;\n"
    "        constexpr double AutomaticTopPaddingFraction = 0.10;\n"
    "        constexpr double AutomaticBottomPaddingFraction = 0.06;\n",
    "viewport margin constants")
text = replace_once(
    text,
    "        struct PaneGeometry final\n",
    "        ValueRange ValueRangeFromViewport(\n"
    "            const render::ValueViewport& viewport) noexcept\n"
    "        {\n"
    "            ValueRange result;\n"
    "            if (!viewport.initialized || viewport.Span() <= 0.0) {\n"
    "                return result;\n"
    "            }\n"
    "            result.minimum = viewport.minimum;\n"
    "            result.maximum = viewport.maximum;\n"
    "            result.valid = true;\n"
    "            return result;\n"
    "        }\n\n"
    "        struct PaneGeometry final\n",
    "value viewport range helper")
text = replace_once(
    text,
    "                const EpochMillis timestamp =\n"
    "                    axis.TimestampForCoordinate(coordinate);\n"
    "                const float x =\n",
    "                if (coordinate > axis.Maximum() + 0.0001) continue;\n"
    "                const EpochMillis timestamp =\n"
    "                    axis.TimestampForCoordinate(coordinate);\n"
    "                const float x =\n",
    "skip future-space time labels")
process = r'''        void ProcessInteraction(
            const ImVec2& plotOrigin,
            float plotWidth,
            float plotHeight,
            const render::OrdinalTimeAxis& axis,
            const AxisRange& dataRange,
            double defaultVisibleSpan,
            const render::Pane& pane,
            ValueRange& values,
            render::ValueViewport& valueViewport,
            bool legendHovered,
            RenderSurfaceState& state)
        {
            if (legendHovered) return;
            if (!ImGui::IsItemHovered() && !ImGui::IsItemActive()) return;

            ImGuiIO& io = ImGui::GetIO();
            const float plotRight = plotOrigin.x + plotWidth;
            const bool mouseInPlot =
                io.MousePos.x >= plotOrigin.x &&
                io.MousePos.x <= plotRight &&
                io.MousePos.y >= plotOrigin.y &&
                io.MousePos.y <= plotOrigin.y + plotHeight;
            const bool mouseInRightAxis =
                io.MousePos.x > plotRight &&
                io.MousePos.x <= plotRight + ValueAxisWidth &&
                io.MousePos.y >= plotOrigin.y &&
                io.MousePos.y <= plotOrigin.y + plotHeight;
            const double mouseRatio = (std::max)(
                0.0,
                (std::min)(1.0,
                    static_cast<double>(io.MousePos.x - plotOrigin.x) /
                    static_cast<double>((std::max)(1.0f, plotWidth))));
            const double valueAnchorRatio = (std::max)(
                0.0,
                (std::min)(1.0,
                    static_cast<double>(io.MousePos.y - plotOrigin.y) /
                    static_cast<double>((std::max)(1.0f, plotHeight))));

            if (mouseInRightAxis &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                state.activeValueAxisPaneId = pane.id;
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                state.activeValueAxisPaneId == pane.id)
            {
                state.activeValueAxisPaneId.clear();
            }
            const bool draggingValueAxis =
                state.activeValueAxisPaneId == pane.id &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f);
            if (mouseInRightAxis || draggingValueAxis) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            }

            if (io.MouseWheel != 0.0f && mouseInPlot) {
                render::ZoomViewport(
                    state.viewport,
                    dataRange.minimum,
                    dataRange.maximum,
                    mouseRatio,
                    static_cast<double>(io.MouseWheel),
                    MinimumVisibleSpan,
                    LatestRightPaddingFraction,
                    MaximumRightOverscrollFraction);
                state.dirty = true;
            }

            const bool draggingLeft =
                ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f);
            const bool draggingRight =
                ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f);
            if (draggingValueAxis) {
                render::ZoomValueViewport(
                    valueViewport,
                    valueAnchorRatio,
                    static_cast<double>(io.MouseDelta.y));
                values = ValueRangeFromViewport(valueViewport);
                state.dirty = true;
            }
            else if (mouseInPlot && (draggingLeft || draggingRight)) {
                if (io.MouseDelta.x != 0.0f) {
                    render::PanViewport(
                        state.viewport,
                        dataRange.minimum,
                        dataRange.maximum,
                        -static_cast<double>(io.MouseDelta.x) /
                            static_cast<double>((std::max)(1.0f, plotWidth)),
                        LatestRightPaddingFraction,
                        MaximumRightOverscrollFraction);
                }
                if (io.MouseDelta.y != 0.0f) {
                    render::PanValueViewport(
                        valueViewport,
                        static_cast<double>(io.MouseDelta.y),
                        static_cast<double>(plotHeight));
                    values = ValueRangeFromViewport(valueViewport);
                }
                state.dirty = true;
            }

            if (!draggingLeft &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (mouseInRightAxis) {
                    const double topPadding =
                        pane.valueScale == render::PaneValueScale::Fixed
                            ? 0.0
                            : AutomaticTopPaddingFraction;
                    const double bottomPadding =
                        pane.valueScale == render::PaneValueScale::Fixed
                            ? 0.0
                            : AutomaticBottomPaddingFraction;
                    render::ResetValueViewport(
                        valueViewport,
                        values.minimum,
                        values.maximum,
                        topPadding,
                        bottomPadding);
                    values = ValueRangeFromViewport(valueViewport);
                    state.dirty = true;
                }
                else if (mouseInPlot) {
                    render::ResetViewport(
                        state.viewport,
                        dataRange.minimum,
                        dataRange.maximum,
                        defaultVisibleSpan,
                        LatestRightPaddingFraction,
                        MaximumRightOverscrollFraction);
                    valueViewport = {};
                    state.dirty = true;
                }
            }

            if (!mouseInPlot) return;
            const render::AxisCoordinate crosshairCoordinate =
                state.viewport.visibleStart +
                state.viewport.Span() * mouseRatio;
            if (crosshairCoordinate > axis.Maximum() + 0.0001) return;

            state.crosshairVisible = true;
            state.crosshairTimestampMs =
                axis.TimestampForCoordinate(crosshairCoordinate);
            const double rawCrosshairValue = UnmapY(
                io.MousePos.y,
                values,
                plotOrigin.y,
                plotHeight);
            state.crosshairValue = render::QuantizeValue(
                pane.cursorGrid,
                rawCrosshairValue);
            state.crosshairValue = (std::max)(
                values.minimum,
                (std::min)(values.maximum, state.crosshairValue));
        }
'''
text = regex_once(
    text,
    r"        void ProcessInteraction\(.*?\n        \}\n\n        void DrawPaneSplitter\(",
    process + "\n        void DrawPaneSplitter(",
    "replace pane interaction")
text = replace_once(
    text,
    "            const ValueRange values = PaneValueRange(pane, axis, visibleRange);\n",
    "            const ValueRange automaticValues =\n"
    "                PaneValueRange(pane, axis, visibleRange);\n"
    "            render::ValueViewport& valueViewport =\n"
    "                state.paneValueViewports[pane.id];\n"
    "            if (automaticValues.valid) {\n"
    "                const double topPadding =\n"
    "                    pane.valueScale == render::PaneValueScale::Fixed\n"
    "                        ? 0.0\n"
    "                        : AutomaticTopPaddingFraction;\n"
    "                const double bottomPadding =\n"
    "                    pane.valueScale == render::PaneValueScale::Fixed\n"
    "                        ? 0.0\n"
    "                        : AutomaticBottomPaddingFraction;\n"
    "                render::FollowValueRange(\n"
    "                    valueViewport,\n"
    "                    automaticValues.minimum,\n"
    "                    automaticValues.maximum,\n"
    "                    topPadding,\n"
    "                    bottomPadding);\n"
    "            }\n"
    "            ValueRange values =\n"
    "                ValueRangeFromViewport(valueViewport);\n",
    "resolve pane value viewport")
text = replace_once(
    text,
    "                pane,\n"
    "                values,\n"
    "                legendHovered,\n",
    "                pane,\n"
    "                values,\n"
    "                valueViewport,\n"
    "                legendHovered,\n",
    "pass value viewport to interaction")
text = replace_once(
    text,
    "                surfaceState.defaultVisibleSpan);\n",
    "                surfaceState.defaultVisibleSpan,\n"
    "                LatestRightPaddingFraction,\n"
    "                MaximumRightOverscrollFraction);\n",
    "initial viewport right margin")
text = replace_once(
    text,
    "                dataRange.minimum,\n"
    "                dataRange.maximum);\n"
    "        }\n"
    "        render::ClampViewport(\n",
    "                dataRange.minimum,\n"
    "                dataRange.maximum,\n"
    "                LatestRightPaddingFraction,\n"
    "                MaximumRightOverscrollFraction);\n"
    "        }\n"
    "        render::ClampViewport(\n",
    "follow latest right margin")
text = replace_once(
    text,
    "            dataRange.maximum,\n"
    "            MinimumVisibleSpan);\n",
    "            dataRange.maximum,\n"
    "            MinimumVisibleSpan,\n"
    "            MaximumRightOverscrollFraction);\n",
    "clamp future-space viewport")
write(path, text)

path = "build.bat"
text = read(path)
text = replace_once(
    text,
    "   render\\value_grid.cpp ^\n",
    "   render\\value_grid.cpp ^\n   render\\value_viewport.cpp ^\n",
    "build value viewport")
write(path, text)

path = "tests/run_all.bat"
text = read(path)
text = replace_once(
    text,
    'call :build_and_run value_grid_tests.exe "tests\\value_grid_tests.cpp render\\value_grid.cpp"\n'
    'if errorlevel 1 exit /b 1\n',
    'call :build_and_run value_grid_tests.exe "tests\\value_grid_tests.cpp render\\value_grid.cpp"\n'
    'if errorlevel 1 exit /b 1\n\n'
    'call :build_and_run value_viewport_tests.exe "tests\\value_viewport_tests.cpp render\\value_viewport.cpp"\n'
    'if errorlevel 1 exit /b 1\n',
    "value viewport tests")
write(path, text)

path = "scripts/verify_modular_architecture.ps1"
text = read(path)
text = replace_once(
    text,
    "    '.\\render\\pane_layout.cpp',\n",
    "    '.\\render\\pane_layout.cpp',\n"
    "    '.\\render\\value_viewport.h',\n"
    "    '.\\render\\value_viewport.cpp',\n",
    "required value viewport files")
marker = "Write-Host 'Major-feature modules, accepted M6 chart contracts, and dynamic M7 indicator management are verified.' -ForegroundColor Green\n"
addition = r'''
$valueViewport = Get-Content '.\render\value_viewport.cpp' -Raw
foreach ($marker in @(
    'ResetValueViewport(',
    'FollowValueRange(',
    'PanValueViewport(',
    'ZoomValueViewport(',
    'viewport.autoScale = false')) {
    if (-not $valueViewport.Contains($marker)) {
        throw "Pane value viewport contract is missing: $marker"
    }
}

foreach ($marker in @(
    'LatestRightPaddingFraction',
    'MaximumRightOverscrollFraction',
    'paneValueViewports',
    'activeValueAxisPaneId',
    'ImGuiMouseCursor_ResizeNS',
    'PanValueViewport(',
    'ZoomValueViewport(',
    'crosshairCoordinate > axis.Maximum()')) {
    if (-not $renderer.Contains($marker)) {
        throw "Chart observation viewport marker is missing: $marker"
    }
}

foreach ($marker in @(
    'latest reset must reserve visible space after the newest bar',
    'manual future-space viewport must survive live updates',
    'dragging the value axis upward must magnify candles',
    'manual value range must survive live data updates')) {
    if (-not $runAll.Contains('value_viewport_tests.exe') -and
        $marker -like '*value*') {
        throw 'value_viewport_tests.exe is not in the complete suite'
    }
}

'''
if marker in text:
    text = text.replace(marker, addition + marker, 1)
else:
    text += "\n" + addition
write(path, text)

print("chart observation viewport applied")
