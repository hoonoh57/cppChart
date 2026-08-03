from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def write(path: str, content: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("\ufeff" + content, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


write(
    "render/value_grid.h",
    r'''#pragma once

#include <vector>

namespace trading::render
{
    struct ValueGridBand final
    {
        double upperExclusive = 0.0;
        double step = 0.0;
    };

    struct ValueGrid final
    {
        bool enabled = false;
        double fallbackStep = 0.0;
        std::vector<ValueGridBand> bands;
    };

    bool ValidateValueGrid(
        const ValueGrid& grid,
        const char** error = nullptr) noexcept;

    double ValueGridStep(
        const ValueGrid& grid,
        double value) noexcept;

    double QuantizeValue(
        const ValueGrid& grid,
        double value) noexcept;
}
''',
)

write(
    "render/value_grid.cpp",
    r'''#include "value_grid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace trading::render
{
    bool ValidateValueGrid(
        const ValueGrid& grid,
        const char** error) noexcept
    {
        const auto fail = [error](const char* message) noexcept {
            if (error != nullptr) *error = message;
            return false;
        };

        if (!grid.enabled) {
            if (error != nullptr) *error = nullptr;
            return true;
        }
        if (!std::isfinite(grid.fallbackStep) || grid.fallbackStep < 0.0) {
            return fail("value-grid fallback step is invalid");
        }

        double previousUpper = 0.0;
        for (std::size_t index = 0; index < grid.bands.size(); ++index) {
            const ValueGridBand& band = grid.bands[index];
            if (!std::isfinite(band.upperExclusive) ||
                !std::isfinite(band.step) ||
                band.upperExclusive <= previousUpper ||
                band.step <= 0.0)
            {
                return fail("value-grid bands must be increasing and positive");
            }
            previousUpper = band.upperExclusive;
        }

        if (grid.bands.empty() && grid.fallbackStep <= 0.0) {
            return fail("enabled value-grid requires a positive step");
        }
        if (!grid.bands.empty() && grid.fallbackStep <= 0.0) {
            return fail("banded value-grid requires a positive fallback step");
        }

        if (error != nullptr) *error = nullptr;
        return true;
    }

    double ValueGridStep(
        const ValueGrid& grid,
        double value) noexcept
    {
        if (!grid.enabled || !std::isfinite(value)) return 0.0;
        const double magnitude = std::fabs(value);
        for (const ValueGridBand& band : grid.bands) {
            if (magnitude < band.upperExclusive) return band.step;
        }
        return grid.fallbackStep;
    }

    double QuantizeValue(
        const ValueGrid& grid,
        double value) noexcept
    {
        if (!std::isfinite(value)) return value;
        const double step = ValueGridStep(grid, value);
        if (!std::isfinite(step) || step <= 0.0) return value;
        const double quantized = std::round(value / step) * step;
        return std::fabs(quantized) < step * 0.5 ? 0.0 : quantized;
    }
}
''',
)

write(
    "render/series_geometry.h",
    r'''#pragma once

#include "chart_viewport.h"

namespace trading::render
{
    float SeriesBodyWidth(
        float plotWidth,
        AxisCoordinate visibleSpan,
        float fillRatio = 0.58f,
        float minimumWidth = 1.0f,
        float maximumWidth = 18.0f) noexcept;
}
''',
)

write(
    "render/series_geometry.cpp",
    r'''#include "series_geometry.h"

#include <algorithm>
#include <cmath>

namespace trading::render
{
    float SeriesBodyWidth(
        float plotWidth,
        AxisCoordinate visibleSpan,
        float fillRatio,
        float minimumWidth,
        float maximumWidth) noexcept
    {
        if (!std::isfinite(plotWidth) || plotWidth <= 0.0f) {
            return (std::max)(0.0f, minimumWidth);
        }
        if (!std::isfinite(visibleSpan) || visibleSpan <= 0.0) {
            visibleSpan = 1.0;
        }
        fillRatio = (std::max)(0.05f, (std::min)(1.0f, fillRatio));
        minimumWidth = (std::max)(0.0f, minimumWidth);
        maximumWidth = (std::max)(minimumWidth, maximumWidth);

        const float slotPitch =
            plotWidth / static_cast<float>((std::max)(1.0, visibleSpan));
        return (std::min)(
            maximumWidth,
            (std::max)(minimumWidth, slotPitch * fillRatio));
    }
}
''',
)

write(
    "tests/value_grid_tests.cpp",
    r'''#include "../render/value_grid.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::exit(1);
    }

    void Check(bool condition, const char* message)
    {
        if (!condition) Fail(message);
    }

    trading::render::ValueGrid KrxEquityGrid()
    {
        trading::render::ValueGrid grid;
        grid.enabled = true;
        grid.bands = {
            { 2000.0, 1.0 },
            { 5000.0, 5.0 },
            { 20000.0, 10.0 },
            { 50000.0, 50.0 },
            { 200000.0, 100.0 },
            { 500000.0, 500.0 }
        };
        grid.fallbackStep = 1000.0;
        return grid;
    }
}

int main()
{
    const trading::render::ValueGrid grid = KrxEquityGrid();
    const char* error = nullptr;
    Check(trading::render::ValidateValueGrid(grid, &error),
          "KRX equity value-grid must validate");
    Check(trading::render::ValueGridStep(grid, 1999.0) == 1.0,
          "below 2,000 tick mismatch");
    Check(trading::render::ValueGridStep(grid, 2000.0) == 5.0,
          "2,000 boundary tick mismatch");
    Check(trading::render::ValueGridStep(grid, 5000.0) == 10.0,
          "5,000 boundary tick mismatch");
    Check(trading::render::ValueGridStep(grid, 20000.0) == 50.0,
          "20,000 boundary tick mismatch");
    Check(trading::render::ValueGridStep(grid, 50000.0) == 100.0,
          "50,000 boundary tick mismatch");
    Check(trading::render::ValueGridStep(grid, 200000.0) == 500.0,
          "200,000 boundary tick mismatch");
    Check(trading::render::ValueGridStep(grid, 500000.0) == 1000.0,
          "500,000 boundary tick mismatch");
    Check(trading::render::QuantizeValue(grid, 239543.0) == 239500.0,
          "price cursor must snap to nearest legal tick");
    Check(trading::render::QuantizeValue(grid, 239760.0) == 240000.0,
          "price cursor upper rounding mismatch");

    trading::render::ValueGrid volume;
    volume.enabled = true;
    volume.fallbackStep = 1.0;
    Check(trading::render::QuantizeValue(volume, 1234.49) == 1234.0,
          "volume cursor integer rounding mismatch");
    Check(trading::render::QuantizeValue(volume, 1234.51) == 1235.0,
          "volume cursor upper rounding mismatch");

    std::puts("[PASS] value_grid_tests");
    return 0;
}
''',
)

write(
    "tests/series_geometry_tests.cpp",
    r'''#include "../render/series_geometry.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::exit(1);
    }

    void Check(bool condition, const char* message)
    {
        if (!condition) Fail(message);
    }
}

int main()
{
    const float candle = trading::render::SeriesBodyWidth(1000.0f, 199.0);
    const float volume = trading::render::SeriesBodyWidth(1000.0f, 199.0);
    Check(std::fabs(candle - volume) < 0.0001f,
          "price and volume body widths must share the same axis-slot geometry");
    Check(candle > 1.0f && candle < 18.0f,
          "normal zoom body width must remain readable");
    Check(trading::render::SeriesBodyWidth(1000.0f, 10.0) == 18.0f,
          "high zoom body width must respect maximum");
    Check(trading::render::SeriesBodyWidth(1000.0f, 2000.0) == 1.0f,
          "wide view body width must respect minimum");

    std::puts("[PASS] series_geometry_tests");
    return 0;
}
''',
)

render_contract = read("render/render_document.h")
render_contract = replace_once(
    render_contract,
    '#include "../core/market_types.h"\n',
    '#include "../core/market_types.h"\n#include "value_grid.h"\n',
    "render contract value-grid include",
)
render_contract = replace_once(
    render_contract,
    '''        double fixedMinimum = 0.0;
        double fixedMaximum = 0.0;
        std::vector<CandleSeries> candles;
''',
    '''        double fixedMinimum = 0.0;
        double fixedMaximum = 0.0;
        ValueGrid cursorGrid;
        int valueDecimals = 2;
        std::vector<CandleSeries> candles;
''',
    "pane cursor contract",
)
write("render/render_document.h", render_contract)

builder = read("render/market_chart_builder.cpp")
builder = replace_once(
    builder,
    '''        pricePane.title = "Price";
        pricePane.heightWeight = 0.80f;

''',
    '''        pricePane.title = "Price";
        pricePane.heightWeight = 0.80f;
        pricePane.valueDecimals = 0;
        pricePane.cursorGrid.enabled = true;
        pricePane.cursorGrid.bands = {
            { 2000.0, 1.0 },
            { 5000.0, 5.0 },
            { 20000.0, 10.0 },
            { 50000.0, 50.0 },
            { 200000.0, 100.0 },
            { 500000.0, 500.0 }
        };
        pricePane.cursorGrid.fallbackStep = 1000.0;

''',
    "KRX price cursor grid",
)
builder = replace_once(
    builder,
    '''        volumePane.title = "Volume";
        volumePane.heightWeight = 0.20f;

''',
    '''        volumePane.title = "Volume";
        volumePane.heightWeight = 0.20f;
        volumePane.valueDecimals = 0;
        volumePane.cursorGrid.enabled = true;
        volumePane.cursorGrid.fallbackStep = 1.0;

''',
    "volume cursor grid",
)
write("render/market_chart_builder.cpp", builder)

renderer = read("ui/render_document_renderer.cpp")
renderer = replace_once(
    renderer,
    '#include "render_document_renderer.h"\n',
    '#include "render_document_renderer.h"\n\n#include "../render/series_geometry.h"\n#include "../render/value_grid.h"\n',
    "renderer geometry includes",
)
renderer = replace_once(
    renderer,
    '''        void DrawValueAxis(
''',
    '''        void FormatPaneValue(
            char* buffer,
            std::size_t bufferSize,
            double value,
            int decimals)
        {
            decimals = (std::max)(0, (std::min)(8, decimals));
            std::snprintf(buffer, bufferSize, "%.*f", decimals, value);
        }

        void DrawCursorValueLabel(
            ImDrawList* draw,
            const ImVec2& plotOrigin,
            const ImVec2& plotEnd,
            float y,
            double value,
            int decimals)
        {
            char label[64]{};
            FormatPaneValue(label, sizeof(label), value, decimals);
            draw->AddLine(
                ImVec2(plotOrigin.x, y),
                ImVec2(plotEnd.x, y),
                IM_COL32(205, 208, 220, 150),
                1.0f);
            draw->AddRectFilled(
                ImVec2(plotEnd.x + 2.0f, y - 9.0f),
                ImVec2(plotEnd.x + ValueAxisWidth - 2.0f, y + 9.0f),
                IM_COL32(65, 68, 80, 245));
            draw->AddText(
                ImVec2(plotEnd.x + 5.0f, y - 7.0f),
                IM_COL32(235, 237, 244, 255),
                label);
        }

        void DrawValueAxis(
''',
    "cursor value label helper",
)
renderer = replace_once(
    renderer,
    '''            const ValueRange& values,
            RenderSurfaceState& state)
        {
            if (!ImGui::IsItemHovered()) return;
''',
    '''            const render::Pane& pane,
            const ValueRange& values,
            RenderSurfaceState& state)
        {
            if (!ImGui::IsItemHovered() && !ImGui::IsItemActive()) return;
''',
    "interaction pane signature",
)
renderer = replace_once(
    renderer,
    '''            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f)) {
                render::PanViewport(
                    state.viewport,
                    dataRange.minimum,
                    dataRange.maximum,
                    -static_cast<double>(io.MouseDelta.x) /
                        static_cast<double>((std::max)(1.0f, plotWidth)));
                state.dirty = true;
            }

            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
''',
    '''            const bool draggingLeft =
                ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f);
            const bool draggingRight =
                ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f);
            if (draggingLeft || draggingRight) {
                render::PanViewport(
                    state.viewport,
                    dataRange.minimum,
                    dataRange.maximum,
                    -static_cast<double>(io.MouseDelta.x) /
                        static_cast<double>((std::max)(1.0f, plotWidth)));
                state.dirty = true;
            }

            if (!draggingLeft &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
''',
    "left and right drag pan",
)
renderer = replace_once(
    renderer,
    '''            state.crosshairValue = UnmapY(
                io.MousePos.y,
                values,
                plotOrigin.y,
                plotHeight);
''',
    '''            const double rawCrosshairValue = UnmapY(
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
''',
    "pane value quantization",
)
renderer = replace_once(
    renderer,
    '''            ImGui::PushID(pane.id.c_str());
            ImGui::InvisibleButton("##surface", size);
''',
    '''            ImGui::PushID(pane.id.c_str());
            ImGui::InvisibleButton(
                "##surface",
                size,
                ImGuiButtonFlags_MouseButtonLeft |
                    ImGuiButtonFlags_MouseButtonRight);
''',
    "surface mouse capture flags",
)
renderer = replace_once(
    renderer,
    '''                defaultVisibleSpan,
                values,
                state);

            for (int grid = 1; grid < 5; ++grid) {
''',
    '''                defaultVisibleSpan,
                pane,
                values,
                state);

            const bool paneHovered =
                ImGui::IsItemHovered() || ImGui::IsItemActive();

            for (int grid = 1; grid < 5; ++grid) {
''',
    "pane interaction argument",
)
old_width = '''            std::size_t visibleCandleCount = 0;
            for (const render::CandleSeries& series : pane.candles) {
                if (!series.visible) continue;
                for (const Bar& bar : series.bars) {
                    if (InAxisRange(bar.closeTimestampMs, axis, visibleRange)) {
                        ++visibleCandleCount;
                    }
                }
            }
            const float candleBodyWidth = (std::min)(
                18.0f,
                (std::max)(
                    1.0f,
                    plotWidth /
                        static_cast<float>((std::max)(
                            static_cast<std::size_t>(1),
                            visibleCandleCount)) *
                        0.58f));
'''
new_width = '''            const float seriesBodyWidth = render::SeriesBodyWidth(
                plotWidth,
                visibleRange.Span());
'''
renderer = replace_once(renderer, old_width, new_width, "shared slot body width")
renderer = renderer.replace("candleBodyWidth", "seriesBodyWidth")
old_hover = '''            if (ImGui::IsItemHovered()) {
                const Bar* nearest = NearestVisibleBar(
                    pane,
                    state.crosshairTimestampMs,
                    axis,
                    visibleRange);
                if (nearest != nullptr) {
                    const float crossY = MapY(
                        static_cast<double>(nearest->close),
                        values,
                        plotOrigin.y,
                        plotHeight);
                    draw->AddLine(
                        ImVec2(plotOrigin.x, crossY),
                        ImVec2(plotEnd.x, crossY),
                        IM_COL32(205, 208, 220, 130),
                        1.0f);
                    ImGui::BeginTooltip();
'''
new_hover = '''            if (paneHovered) {
                const float crossY = MapY(
                    state.crosshairValue,
                    values,
                    plotOrigin.y,
                    plotHeight);
                DrawCursorValueLabel(
                    draw,
                    plotOrigin,
                    plotEnd,
                    crossY,
                    state.crosshairValue,
                    pane.valueDecimals);

                const Bar* nearest = NearestVisibleBar(
                    pane,
                    state.crosshairTimestampMs,
                    axis,
                    visibleRange);
                if (nearest != nullptr) {
                    ImGui::BeginTooltip();
'''
renderer = replace_once(renderer, old_hover, new_hover, "mouse-position crosshair")
write("ui/render_document_renderer.cpp", renderer)

build = read("build.bat")
build = replace_once(
    build,
    '''   render\\chart_viewport.cpp ^
   render\\time_axis.cpp ^
''',
    '''   render\\chart_viewport.cpp ^
   render\\time_axis.cpp ^
   render\\value_grid.cpp ^
   render\\series_geometry.cpp ^
''',
    "shell render utility build",
)
write("build.bat", build)

run_all = read("tests/run_all.bat")
anchor = '''chart_viewport_tests.exe
if errorlevel 1 exit /b 1

'''
addition = anchor + r'''cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\value_grid_tests.cpp ^
  render\value_grid.cpp ^
  /Fe:value_grid_tests.exe
if errorlevel 1 exit /b 1
value_grid_tests.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\series_geometry_tests.cpp ^
  render\series_geometry.cpp ^
  /Fe:series_geometry_tests.exe
if errorlevel 1 exit /b 1
series_geometry_tests.exe
if errorlevel 1 exit /b 1

'''
run_all = replace_once(run_all, anchor, addition, "render utility test registration")
write("tests/run_all.bat", run_all)

verify = read("scripts/verify_modular_architecture.ps1")
verify = replace_once(
    verify,
    "    '.\\render\\render_document.cpp',\n",
    "    '.\\render\\render_document.cpp',\n    '.\\render\\value_grid.h',\n    '.\\render\\value_grid.cpp',\n    '.\\render\\series_geometry.h',\n    '.\\render\\series_geometry.cpp',\n",
    "required render utility files",
)
verify = replace_once(
    verify,
    "Write-Host 'Major-feature modules, immutable live-tail storage, and compressed trading-time rendering verified.'",
    """$requiredInteractionMarkers = @(
    'ImGuiButtonFlags_MouseButtonLeft',
    'ImGuiButtonFlags_MouseButtonRight',
    'draggingLeft || draggingRight',
    'QuantizeValue(',
    'SeriesBodyWidth('
)
foreach ($marker in $requiredInteractionMarkers) {
    if (-not $renderer.Contains($marker)) {
        throw \"Chart interaction contract is missing: $marker\"
    }
}
if ($renderer.Contains('static_cast<double>(nearest->close)')) {
    throw 'Horizontal crosshair must not be forced to nearest candle close'
}

Write-Host 'Major-feature modules, immutable live-tail storage, compressed trading-time rendering, and pane-aware interaction verified.'""",
    "interaction verifier",
)
write("scripts/verify_modular_architecture.ps1", verify)

print("Applied aligned series geometry, drag pan, and pane-aware value crosshair")
