from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text("\ufeff" + text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


# Render document exposes a stable structure revision. Same-minute live ticks
# may advance document.revision without forcing boundary recalculation.
contract = read("render/render_document.h")
if "std::uint64_t structureRevision" not in contract:
    contract = replace_once(
        contract,
        '''    struct RenderDocument final
    {
        std::uint64_t revision = 0;
''',
        '''    struct RenderDocument final
    {
        std::uint64_t revision = 0;
        std::uint64_t structureRevision = 0;
''',
        "render structure revision",
    )
    write("render/render_document.h", contract)

builder_h = read("render/market_chart_builder.h")
if "std::uint64_t completedRevision" not in builder_h:
    builder_h = replace_once(
        builder_h,
        '''        bool hasLiveBar = false;
        std::shared_ptr<const std::vector<HistogramPoint>> completedVolume;
''',
        '''        bool hasLiveBar = false;
        std::uint64_t completedRevision = 0;
        std::shared_ptr<const std::vector<HistogramPoint>> completedVolume;
''',
        "market chart completed revision",
    )
    write("render/market_chart_builder.h", builder_h)

builder_cpp = read("render/market_chart_builder.cpp")
if "document.structureRevision" not in builder_cpp:
    builder_cpp = replace_once(
        builder_cpp,
        '''        document.title = title;
        document.revision = revision;
''',
        '''        document.title = title;
        document.revision = revision;
        document.structureRevision = source.completedRevision;
''',
        "builder structure revision",
    )
    write("render/market_chart_builder.cpp", builder_cpp)

workspace = read("app/chart_workspace_module.cpp")
if "renderSource.completedRevision" not in workspace:
    workspace = replace_once(
        workspace,
        '''        renderSource.hasLiveBar = source.hasLiveBar;
        renderSource.completedVolume = completedVolume;
''',
        '''        renderSource.hasLiveBar = source.hasLiveBar;
        renderSource.completedRevision = source.completedRevision;
        renderSource.completedVolume = completedVolume;
''',
        "workspace completed revision handoff",
    )
    write("app/chart_workspace_module.cpp", workspace)


renderer = read("ui/render_document_renderer.cpp")
if "PrimaryCandleTimestamps" not in renderer:
    renderer = replace_once(
        renderer,
        '''            return result;
        }

        EpochMillis MinimumViewportSpan(
''',
        '''            return result;
        }

        std::vector<EpochMillis> PrimaryCandleTimestamps(
            const render::RenderDocument& document)
        {
            for (const render::Pane& pane : document.panes) {
                for (const render::CandleSeries& series : pane.candles) {
                    if (!series.visible || series.bars.empty()) continue;
                    std::vector<EpochMillis> result;
                    result.reserve(series.bars.size());
                    for (const Bar& bar : series.bars) {
                        result.push_back(bar.closeTimestampMs);
                    }
                    return result;
                }
            }
            return {};
        }

        EpochMillis MinimumViewportSpan(
''',
        "primary candle timestamps",
    )

if "TimeBoundaryKind::CalendarDate" not in renderer:
    renderer = replace_once(
        renderer,
        '''            for (int grid = 1; grid < 5; ++grid) {
                const float y =
                    plotOrigin.y +
                    plotHeight * static_cast<float>(grid) / 5.0f;
                draw->AddLine(
                    ImVec2(plotOrigin.x, y),
                    ImVec2(plotEnd.x, y),
                    IM_COL32(45, 47, 55, 255));
            }

            std::size_t visibleCandleCount = 0;
''',
        '''            for (int grid = 1; grid < 5; ++grid) {
                const float y =
                    plotOrigin.y +
                    plotHeight * static_cast<float>(grid) / 5.0f;
                draw->AddLine(
                    ImVec2(plotOrigin.x, y),
                    ImVec2(plotEnd.x, y),
                    IM_COL32(45, 47, 55, 255));
            }

            for (const render::TimeBoundary& boundary : state.timeBoundaries) {
                if (!InTimeRange(boundary.timestampMs, visibleRange)) continue;
                const float x = MapX(
                    boundary.timestampMs,
                    visibleRange,
                    plotOrigin.x,
                    plotWidth);
                const bool calendarDate =
                    boundary.kind == render::TimeBoundaryKind::CalendarDate;
                draw->AddLine(
                    ImVec2(x, plotOrigin.y),
                    ImVec2(x, plotEnd.y),
                    calendarDate
                        ? IM_COL32(145, 150, 172, 190)
                        : IM_COL32(105, 110, 126, 135),
                    calendarDate ? 1.5f : 1.0f);

                if (drawTimeAxis) {
                    std::string label = calendarDate
                        ? FormatTimestamp(boundary.timestampMs).substr(0, 5)
                        : std::string("gap");
                    draw->AddText(
                        ImVec2(x + 3.0f, plotOrigin.y + 3.0f),
                        calendarDate
                            ? IM_COL32(205, 208, 224, 230)
                            : IM_COL32(145, 148, 162, 200),
                        label.c_str());
                }
            }

            std::size_t visibleCandleCount = 0;
''',
        "draw generic time boundaries",
    )

if "boundaryStructureRevision" not in renderer:
    renderer = replace_once(
        renderer,
        '''        const EpochMillis minimumSpanMs =
            MinimumViewportSpan(document);
        if (!surfaceState.viewport.initialized) {
''',
        '''        const EpochMillis minimumSpanMs =
            MinimumViewportSpan(document);
        const std::uint64_t boundaryStructureRevision =
            document.structureRevision != 0
                ? document.structureRevision
                : document.revision;
        if (surfaceState.boundaryRevision != boundaryStructureRevision) {
            surfaceState.timeBoundaries = render::FindTimeBoundaries(
                PrimaryCandleTimestamps(document));
            surfaceState.boundaryRevision = boundaryStructureRevision;
        }
        if (!surfaceState.viewport.initialized) {
''',
        "boundary cache refresh",
    )
write("ui/render_document_renderer.cpp", renderer)


build = read("build.bat")
if "render\\time_boundaries.cpp" not in build:
    build = replace_once(
        build,
        '''   render\\chart_viewport.cpp ^
   render\\render_document.cpp ^
''',
        '''   render\\chart_viewport.cpp ^
   render\\time_boundaries.cpp ^
   render\\render_document.cpp ^
''',
        "build time boundaries",
    )
    write("build.bat", build)

run_all = read("tests/run_all.bat")
if "time_boundaries_tests.exe" not in run_all:
    run_all = replace_once(
        run_all,
        '''chart_viewport_tests.exe
if errorlevel 1 exit /b 1

''',
        '''chart_viewport_tests.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\\time_boundaries_tests.cpp ^
  render\\time_boundaries.cpp ^
  /Fe:time_boundaries_tests.exe
if errorlevel 1 exit /b 1
time_boundaries_tests.exe
if errorlevel 1 exit /b 1

''',
        "time boundary test registration",
    )
    write("tests/run_all.bat", run_all)

plan = read("docs/MODULARIZATION_PLAN.md")
plan = plan.replace(
    '''Remaining before the local visual acceptance request:

- session/date boundary rendering;
- final Windows CI verification;
- one focused visual/GPU test covering zoom, pan, crosshair, latest-bar follow, feature levels, and real `0B` updates.
''',
    '''M6 remote implementation complete:

- calendar-date changes and abnormal session gaps are detected by a broker-independent render utility;
- boundaries are cached by completed-history structure revision and are not recomputed on every same-minute `0B` tick;
- boundary lines render through the generic pane renderer;
- headless date, gap, duplicate, and reverse-timestamp fixtures are registered in the complete suite.

Remaining M6 acceptance:

- final Windows CI verification;
- one focused local visual/GPU test covering zoom, pan, crosshair, latest-bar follow, date/session boundaries, feature levels, and real `0B` updates.
''',
)
write("docs/MODULARIZATION_PLAN.md", plan)

handoff = read("docs/SESSION_HANDOFF.md")
handoff = handoff.replace(
    '''Still required before asking the user to test:

1. add session/date boundary rendering;
2. run Windows MSVC build and the complete headless suite;
3. record the verified HEAD and CI run below.
''',
    '''M6 remote implementation is complete:

- calendar-date and abnormal session-gap boundaries are generic renderer data;
- boundary calculation is cached by completed-history structure revision;
- same-minute `0B` events do not recompute boundaries or copy completed history;
- viewport, boundary, market-data, workspace, and runtime tests are in the complete suite.

Still required before asking the user to test:

1. run the final Windows MSVC build and complete headless suite;
2. restore verification-only CI and remove one-shot migration files;
3. record the verified HEAD and CI run below.
''',
)
write("docs/SESSION_HANDOFF.md", handoff)

print("Completed generic calendar/session boundaries and cached them by chart structure revision")
