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


# 1. Register viewport tests in the full headless suite.
run_all = read("tests/run_all.bat")
anchor = '''render_document_tests.exe
if errorlevel 1 exit /b 1

'''
addition = anchor + '''cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\\chart_viewport_tests.cpp ^
  render\\chart_viewport.cpp ^
  /Fe:chart_viewport_tests.exe
if errorlevel 1 exit /b 1
chart_viewport_tests.exe
if errorlevel 1 exit /b 1

'''
if "chart_viewport_tests.exe" not in run_all:
    run_all = replace_once(run_all, anchor, addition, "viewport test registration")
write("tests/run_all.bat", run_all)


# 2. Feed the complete verified history into the viewport. The renderer then
# clips to the visible range instead of permanently truncating history to the
# current pixel width.
shell = read("shell_main.cpp")
old_limit = '''    const ImVec2 available = ImGui::GetContentRegionAvail();
    const std::size_t visibleLimit = static_cast<std::size_t>((std::max)(
        20,
        static_cast<int>(available.x / 7.0f)));
'''
new_limit = '''    const ImVec2 available = ImGui::GetContentRegionAvail();
    const std::size_t visibleLimit = snapshot.barCount;
'''
shell = replace_once(shell, old_limit, new_limit, "full-history viewport source")
write("shell_main.cpp", shell)


# 3. Draw the vertical crosshair in a final overlay pass so all panes receive
# the same timestamp in the same frame, including panes drawn before the
# hovered pane.
renderer = read("ui/render_document_renderer.cpp")
value_range_end = '''        };

        void IncludeTimestamp(
'''
pane_geometry = '''        };

        struct PaneGeometry final
        {
            ImVec2 plotOrigin;
            ImVec2 plotEnd;
            bool valid = false;
        };

        void IncludeTimestamp(
'''
if "struct PaneGeometry final" not in renderer:
    renderer = replace_once(
        renderer,
        value_range_end,
        pane_geometry,
        "pane geometry declaration",
    )

old_signature = '''        void DrawPane(
            const render::Pane& pane,
            const TimeRange& dataRange,
            const TimeRange& visibleRange,
            EpochMillis minimumSpanMs,
            bool drawTimeAxis,
            ImVec2 size,
            RenderSurfaceState& state)
'''
new_signature = '''        void DrawPane(
            const render::Pane& pane,
            const TimeRange& dataRange,
            const TimeRange& visibleRange,
            EpochMillis minimumSpanMs,
            bool drawTimeAxis,
            ImVec2 size,
            RenderSurfaceState& state,
            std::vector<PaneGeometry>& paneGeometries)
'''
renderer = replace_once(
    renderer,
    old_signature,
    new_signature,
    "DrawPane signature",
)

old_geometry = '''            const ImVec2 plotEnd(
                plotOrigin.x + plotWidth,
                plotOrigin.y + plotHeight);
            ImDrawList* draw = ImGui::GetWindowDrawList();
'''
new_geometry = '''            const ImVec2 plotEnd(
                plotOrigin.x + plotWidth,
                plotOrigin.y + plotHeight);
            PaneGeometry geometry;
            geometry.plotOrigin = plotOrigin;
            geometry.plotEnd = plotEnd;
            geometry.valid = true;
            paneGeometries.push_back(geometry);
            ImDrawList* draw = ImGui::GetWindowDrawList();
'''
renderer = replace_once(
    renderer,
    old_geometry,
    new_geometry,
    "pane geometry capture",
)

old_vertical = '''            if (state.crosshairVisible &&
                InTimeRange(state.crosshairTimestampMs, visibleRange))
            {
                const float crossX = MapX(
                    state.crosshairTimestampMs,
                    visibleRange,
                    plotOrigin.x,
                    plotWidth);
                draw->AddLine(
                    ImVec2(crossX, plotOrigin.y),
                    ImVec2(crossX, plotEnd.y),
                    IM_COL32(205, 208, 220, 180),
                    1.0f);
            }

'''
renderer = replace_once(
    renderer,
    old_vertical,
    "",
    "remove per-pane early crosshair",
)

old_loop_setup = '''        const float availableHeight =
            (std::max)(0.0f, size.y - spacing);

        for (std::size_t index = 0; index < document.panes.size(); ++index) {
'''
new_loop_setup = '''        const float availableHeight =
            (std::max)(0.0f, size.y - spacing);
        std::vector<PaneGeometry> paneGeometries;
        paneGeometries.reserve(document.panes.size());

        for (std::size_t index = 0; index < document.panes.size(); ++index) {
'''
renderer = replace_once(
    renderer,
    old_loop_setup,
    new_loop_setup,
    "pane geometry collection setup",
)

old_call = '''                index + 1 == document.panes.size(),
                ImVec2(size.x, paneHeight),
                surfaceState);
'''
new_call = '''                index + 1 == document.panes.size(),
                ImVec2(size.x, paneHeight),
                surfaceState,
                paneGeometries);
'''
renderer = replace_once(renderer, old_call, new_call, "DrawPane call")

old_end = '''        surfaceState.renderedRevision = document.revision;
        surfaceState.dirty = false;
'''
new_end = '''        if (
            surfaceState.crosshairVisible &&
            InTimeRange(surfaceState.crosshairTimestampMs, visibleRange))
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            for (const PaneGeometry& geometry : paneGeometries) {
                if (!geometry.valid) continue;
                const float width = geometry.plotEnd.x - geometry.plotOrigin.x;
                const float crossX = MapX(
                    surfaceState.crosshairTimestampMs,
                    visibleRange,
                    geometry.plotOrigin.x,
                    width);
                draw->AddLine(
                    ImVec2(crossX, geometry.plotOrigin.y),
                    ImVec2(crossX, geometry.plotEnd.y),
                    IM_COL32(205, 208, 220, 180),
                    1.0f);
            }
        }

        surfaceState.renderedRevision = document.revision;
        surfaceState.dirty = false;
'''
renderer = replace_once(
    renderer,
    old_end,
    new_end,
    "synchronized crosshair overlay",
)
write("ui/render_document_renderer.cpp", renderer)


# 4. Record the current milestone state for the next session.
plan = read("docs/MODULARIZATION_PLAN.md")
old_m6 = '''## Milestone M6 — chart viewport foundation

On the generic renderer contract implement:

- visible time range;
- wheel zoom;
- horizontal pan;
- auto/manual value scale;
- current-price line and label;
- crosshair and OHLCV tooltip;
- date/session boundaries;
- synchronized interaction state for future multi-chart use.

Acceptance requires local visual/GPU testing and is the first planned user-intervention point after remote refactoring.
'''
new_m6 = '''## Milestone M6 — chart viewport foundation

Implemented on the generic renderer contract:

- full verified history remains available to the viewport;
- visible time range;
- wheel zoom anchored at the mouse position;
- right-button horizontal pan;
- double-click reset and latest-bar auto-follow;
- visible-range automatic value scale;
- current-price line and label;
- synchronized vertical crosshair across all panes in the same frame;
- OHLCV and tick-count tooltip;
- time and value axes;
- headless viewport state tests registered in the complete test suite.

Remaining before the local visual acceptance request:

- session/date boundary rendering;
- immutable completed-history plus mutable live-tail sharing so `0B` does not rebuild or copy the complete history on every tick;
- final Windows CI verification after the live-tail split;
- one focused visual/GPU test covering zoom, pan, crosshair, latest-bar follow, feature levels, and real `0B` updates.
'''
plan = replace_once(plan, old_m6, new_m6, "M6 plan status")
write("docs/MODULARIZATION_PLAN.md", plan)

handoff = '''# cppChart Session Handoff

## Mandatory first read

Read these before changing code:

1. `docs/ARCHITECTURE_CONSTITUTION.md`
2. `docs/MODULARIZATION_PLAN.md`
3. this handoff

## Repository state

- repository: `hoonoh57/cppChart`
- development branch: `p2/kiwoom-mock-gateway`
- PR: `#1`, Draft; do not merge before real-data and account acceptance
- production policy: real Kiwoom mock data only; no synthetic fallback

## Product objective

Build a fast chart-based trading workbench that can add indicators, strategies,
index and multi-symbol comparison, replay, backtest, and multi-symbol trading
results without adding feature-specific branches to the renderer or returning to
a monolithic `shell_main.cpp`.

## Architecture invariants

- Objectify only major features with independent state and lifecycle.
- Keep Tick, Bar, render points, and indicator values as compact value types in
  contiguous storage.
- UI publishes commands and renders immutable snapshots; it does not mutate
  market or account state directly.
- The generic renderer consumes only `RenderDocument` and renderer interaction
  state. It must not know Kiwoom API IDs, indicators, strategies, or accounts.
- `Off` stops subscriptions, calculation, rendering, and retained memory;
  `Standby` retains state but stops expensive work; `Visible` renders without
  strategy execution; `Active` enables the complete feature.
- Real errors remain visible and fail closed. Never restore synthetic data to
  make a screen look complete.
- Batch, replay, and real-time indicator/strategy logic must share the same
  calculation implementation.

## Completed modularization

- `FeatureRegistry` and feature metrics
- `MarketDataModule`
- `ChartWorkspaceModule`
- generic `RenderDocument`, builder, and ImGui renderer
- market-data and chart-workspace feature-level controls
- WebSocket `0B` unsubscribe and reconnect behavior
- actual `ka10080` plus `0B` selected-symbol chart path

## Current M6 status

Implemented:

- viewport state and tests
- wheel zoom, horizontal pan, double-click reset, auto-follow
- value/time axes
- current-price line
- OHLCV/tick tooltip
- same-frame synchronized vertical crosshair across panes
- full loaded history available for viewport navigation

Still required before asking the user to test:

1. split completed immutable history from the mutable live bar;
2. share completed history into render documents without copying on every `0B`;
3. add session/date boundary rendering;
4. run Windows MSVC build and the complete headless suite;
5. record the verified HEAD and CI run below.

## User-test policy

Do not request a local pull yet. Ask for a focused local test only after the
remaining M6 items are remotely verified. The focused test must cover real
`ka10080` history, `0B` last-bar updates, wheel zoom, pan, double-click reset,
crosshair synchronization, current-price line, and feature Off/Standby/Visible.

## Verification record

- verified HEAD: pending final M6 verification
- Windows CI run: pending
- local acceptance: not requested
'''
write("docs/SESSION_HANDOFF.md", handoff)

print("Completed M6 viewport registration, full-history navigation, and synchronized crosshair migration")
