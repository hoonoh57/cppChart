# cppChart Session Handoff

Read `ARCHITECTURE_CONSTITUTION.md`, `MODULARIZATION_PLAN.md`, then this file.

Repository: `hoonoh57/cppChart`
Branch: `p2/kiwoom-mock-gateway`
Local: `E:\2026\gpt\cpp\shell`
PR #1: Draft
Viewport/footer implementation: `2b87b40097cd36be46de4f956664302ef74a6558`
Read-only verification contract: `63827a6c88469911e3eab784c1d427bdd0bfdc0f`
Windows CI #1074: `30894598030`
Artifact: `8886617161`
Digest: `sha256:edb5fb92aa9b9d8c77ac82753ca92fb42b4c6a58742ad04dd50c2ee9adadfbf2`
Workflow: read-only

Pull and use live branch HEAD.

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
git status
git rev-parse HEAD
```

M1-M6 are complete. M7 dynamic indicator calculation/management is stabilized. The
11-type catalog is SMA, EMA, JMA, Bollinger Bands, RSI, MACD, DMI, SuperTrend,
VWAP, OBV, and Wilder ADX. Comparison supports stock `0B`, `ka20005` index history,
sector-index realtime `0J`, KOSPI `001`, KOSDAQ `101`, separate panes, and price
secondary axes.

## 2026-08-04 actual-screen corrections

### Sector-index realtime and indicator discovery

- Corrected sector-index realtime from invalid `0I` to official `0J`.
- Added `0J` registration, decode, delivery, reconnect restoration, removal, and
  removed-subscription non-resurrection coverage.
- `적용 지표` distinguishes active instances from the full 11-type
  `새 지표 추가` catalog.
- The output table has bounded internal scrolling and Apply/Revert remain in a
  fixed properties footer.

### Reference controls and chart X axis

The user then confirmed two additional layout failures:

1. `기준선 추가 / 과매수 추가 / 과매도 추가` remained inside the scrolling editor
   and were hidden until the properties page was scrolled.
2. ImGui vertical item spacing accumulated between every pane and splitter, making
   chart content taller than the supplied viewport and pushing the final X axis
   below an internal chart scrollbar.

Corrections:

- The three reference quick-action buttons are a fixed footer row immediately
  above Apply/Revert, outside the editor child region.
- Footer space is explicitly reserved for quick actions, Apply/Revert, and errors.
- Pane and splitter invisible items use zero vertical `ItemSpacing`, so their total
  height equals the renderer-provided chart height.
- The `실제 시세` window forbids internal vertical scrolling and mouse-wheel window
  scrolling. Wheel input remains available to chart viewport interaction.
- CI guards the fixed quick actions, enlarged footer, no-scroll chart flags, and
  zero pane/splitter spacing.

CI #1074 passed repository/real-data policy, architecture boundaries, comparison
and indicator contracts, the viewport/footer guards, MSVC x64 build, the full
headless suite, clean-tree verification, and artifact publication.

## Focused actual-screen acceptance

1. At normal narrow dock width, confirm the three reference buttons and
   Apply/Revert are visible without scrolling.
2. Expand/collapse parameter, output, pane, and reference sections; only the editor
   body must scroll while both footer rows remain fixed.
3. Load multiple lower panes and confirm the bottom X axis is always visible without
   a chart-window vertical scrollbar.
4. Confirm wheel zoom, horizontal pan, pane separator drag, legends, and synchronized
   crosshair still work.
5. Confirm live `0B`/`0J` updates preserve viewport, selection, pane sizes, fixed
   footer visibility, and X-axis visibility.

Return a screenshot only for a failed item. After acceptance, proceed to code/name
search and normalized relative-strength comparison. PR #1 remains Draft until actual
account/order, physical multi-source reconnect, and intraday soak are accepted.
