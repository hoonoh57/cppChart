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
    "render/cursor_label_layout.h",
    r'''#pragma once

#include <algorithm>
#include <cmath>

namespace trading::render
{
    struct HorizontalLabelPlacement final
    {
        float left = 0.0f;
        float right = 0.0f;
    };

    inline HorizontalLabelPlacement PlaceCenteredHorizontalLabel(
        float anchorX,
        float requestedWidth,
        float minimumX,
        float maximumX) noexcept
    {
        if (!std::isfinite(minimumX)) minimumX = 0.0f;
        if (!std::isfinite(maximumX) || maximumX < minimumX) {
            maximumX = minimumX;
        }
        const float available = maximumX - minimumX;
        if (!std::isfinite(requestedWidth) || requestedWidth < 0.0f) {
            requestedWidth = 0.0f;
        }
        const float width = (std::min)(requestedWidth, available);
        if (!std::isfinite(anchorX)) anchorX = minimumX;

        const float unclampedLeft = anchorX - width * 0.5f;
        const float maximumLeft = maximumX - width;
        const float left = (std::max)(
            minimumX,
            (std::min)(maximumLeft, unclampedLeft));
        return { left, left + width };
    }
}
''',
)

write(
    "tests/cursor_label_layout_tests.cpp",
    r'''#include "../render/cursor_label_layout.h"

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

    bool Near(float left, float right)
    {
        return std::fabs(left - right) < 0.0001f;
    }
}

int main()
{
    const trading::render::HorizontalLabelPlacement centered =
        trading::render::PlaceCenteredHorizontalLabel(50.0f, 20.0f, 0.0f, 100.0f);
    Check(Near(centered.left, 40.0f) && Near(centered.right, 60.0f),
          "centered label placement mismatch");

    const trading::render::HorizontalLabelPlacement left =
        trading::render::PlaceCenteredHorizontalLabel(3.0f, 20.0f, 0.0f, 100.0f);
    Check(Near(left.left, 0.0f) && Near(left.right, 20.0f),
          "left-edge label must remain inside the pane");

    const trading::render::HorizontalLabelPlacement right =
        trading::render::PlaceCenteredHorizontalLabel(97.0f, 20.0f, 0.0f, 100.0f);
    Check(Near(right.left, 80.0f) && Near(right.right, 100.0f),
          "right-edge label must remain inside the pane");

    const trading::render::HorizontalLabelPlacement oversized =
        trading::render::PlaceCenteredHorizontalLabel(50.0f, 200.0f, 10.0f, 90.0f);
    Check(Near(oversized.left, 10.0f) && Near(oversized.right, 90.0f),
          "oversized label must clamp to the available pane width");

    std::puts("[PASS] cursor_label_layout_tests");
    return 0;
}
''',
)

renderer = read("ui/render_document_renderer.cpp")
renderer = replace_once(
    renderer,
    '#include "../render/series_geometry.h"\n#include "../render/value_grid.h"\n',
    '#include "../render/cursor_label_layout.h"\n#include "../render/series_geometry.h"\n#include "../render/value_grid.h"\n',
    "renderer cursor-label include",
)

cursor_value_function = r'''        void DrawCursorValueLabel(
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

'''
cursor_time_function = cursor_value_function + r'''        void DrawCursorTimeLabel(
            ImDrawList* draw,
            const ImVec2& plotOrigin,
            const ImVec2& plotEnd,
            float crossX,
            EpochMillis timestampMs,
            bool useTimeAxisBand)
        {
            const std::string label = FormatTimestamp(timestampMs);
            const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            constexpr float horizontalPadding = 6.0f;
            constexpr float labelHeight = 18.0f;
            const float requestedWidth = textSize.x + horizontalPadding * 2.0f;
            const render::HorizontalLabelPlacement placement =
                render::PlaceCenteredHorizontalLabel(
                    crossX,
                    requestedWidth,
                    plotOrigin.x,
                    plotEnd.x);
            const float top = useTimeAxisBand
                ? plotEnd.y + 1.0f
                : plotEnd.y - labelHeight - 1.0f;
            const float bottom = top + labelHeight;

            draw->AddRectFilled(
                ImVec2(placement.left, top),
                ImVec2(placement.right, bottom),
                IM_COL32(65, 68, 80, 245));
            draw->AddRect(
                ImVec2(placement.left, top),
                ImVec2(placement.right, bottom),
                IM_COL32(205, 208, 220, 180));
            draw->AddText(
                ImVec2(
                    placement.left +
                        (placement.right - placement.left - textSize.x) * 0.5f,
                    top + 2.0f),
                IM_COL32(235, 237, 244, 255),
                label.c_str());
        }

'''
renderer = replace_once(
    renderer,
    cursor_value_function,
    cursor_time_function,
    "cursor time-label helper",
)

old_hover = r'''            if (paneHovered) {
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
'''
new_hover = r'''            if (paneHovered) {
                const float crossX = MapX(
                    state.crosshairTimestampMs,
                    axis,
                    visibleRange,
                    plotOrigin.x,
                    plotWidth);
                const float crossY = MapY(
                    state.crosshairValue,
                    values,
                    plotOrigin.y,
                    plotHeight);
                DrawCursorTimeLabel(
                    draw,
                    plotOrigin,
                    plotEnd,
                    crossX,
                    state.crosshairTimestampMs,
                    drawTimeAxis);
                DrawCursorValueLabel(
                    draw,
                    plotOrigin,
                    plotEnd,
                    crossY,
                    state.crosshairValue,
                    pane.valueDecimals);

                const Bar* nearest = NearestVisibleBar(
'''
renderer = replace_once(
    renderer,
    old_hover,
    new_hover,
    "pane-aware cursor time label",
)
write("ui/render_document_renderer.cpp", renderer)

run_all = read("tests/run_all.bat")
run_anchor = r'''cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\series_geometry_tests.cpp ^
  render\series_geometry.cpp ^
  /Fe:series_geometry_tests.exe
if errorlevel 1 exit /b 1
series_geometry_tests.exe
if errorlevel 1 exit /b 1

'''
run_addition = run_anchor + r'''cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\cursor_label_layout_tests.cpp ^
  /Fe:cursor_label_layout_tests.exe
if errorlevel 1 exit /b 1
cursor_label_layout_tests.exe
if errorlevel 1 exit /b 1

'''
run_all = replace_once(
    run_all,
    run_anchor,
    run_addition,
    "cursor label test registration",
)
write("tests/run_all.bat", run_all)

verify = read("scripts/verify_modular_architecture.ps1")
verify = replace_once(
    verify,
    "    '.\\render\\series_geometry.cpp',\n",
    "    '.\\render\\series_geometry.cpp',\n    '.\\render\\cursor_label_layout.h',\n",
    "cursor label architecture file",
)
verify = replace_once(
    verify,
    "    'SeriesBodyWidth('\n",
    "    'SeriesBodyWidth(',\n    'DrawCursorTimeLabel(',\n    'PlaceCenteredHorizontalLabel('\n",
    "cursor time-label architecture markers",
)
write("scripts/verify_modular_architecture.ps1", verify)

print("Added pane-aware crosshair time label and layout regression test")
