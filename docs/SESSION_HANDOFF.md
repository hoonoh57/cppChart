# cppChart Session Handoff

Read `ARCHITECTURE_CONSTITUTION.md`, `MODULARIZATION_PLAN.md`, then this file.

Repository: `hoonoh57/cppChart`
Branch: `p2/kiwoom-mock-gateway`
Local: `E:\2026\gpt\cpp\shell`
PR #1: Draft
Verified viewport implementation HEAD: `80542a44cfcaea570703b46d4a84ad36025837b3`
Pull-request verification merge SHA: `96204f598925a302707b8d37f4351233e551e46b`
Windows CI run: `30898400950`
Artifact: `8888150112`
Digest: `sha256:1396a02c41e52434ba3ff828e775aa05fb8380a6d454d335762a245088caa226`
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

M1-M6 are complete. M7 dynamic indicator calculation/management is stabilized and
visually accepted. The 11-type catalog is SMA, EMA, JMA, Bollinger Bands, RSI,
MACD, DMI, SuperTrend, VWAP, OBV, and Wilder ADX. Comparison supports stock `0B`,
`ka20005` index history, sector-index realtime `0J`, KOSPI `001`, KOSDAQ `101`,
separate panes, and price secondary axes.

## 2026-08-04 actual-screen layout corrections

The user confirmed these corrections as normal:

- `기준선 추가 / 과매수 추가 / 과매도 추가` stay in the fixed properties footer.
- Apply/Revert remain visible without scrolling the editor body.
- pane/splitter item spacing no longer enlarges chart content.
- the chart window no longer develops an internal vertical scrollbar.
- the bottom X axis remains visible with several lower panes.

## Chart observation viewport — implemented and CI verified

The latest candle previously remained attached to the right price axis and the Y
range was always automatic, making live candle observation difficult. The renderer
now provides the following generic viewport behavior:

### Horizontal observation space

- initial view, live auto-follow, and double-click reset reserve future bar slots to
  the right of the latest candle;
- the user may drag the chart horizontally so the latest candle is not attached to
  the price axis;
- manual horizontal placement remains stable during `0B`/`0J` live-tail updates;
- double-click restores the latest view with the configured right-side margin.

### Vertical pane movement

- dragging inside a pane vertically moves that pane's visible value range;
- a sharply rising latest candle can therefore be moved down into view without
  changing calculation data;
- each pane owns independent manual Y state;
- manual Y offset remains stable during live-tail replacement.

### Y-axis scale drag

- dragging the right Y-axis expands or contracts candle/series height vertically;
- scaling is anchored around the mouse value so the inspected price region remains
  under the cursor;
- minimum span and finite-range guards prevent collapse or invalid ranges;
- double-click in the pane/Y-axis restores an automatic visible-data range with
  top and bottom padding.

### Interaction boundaries

- horizontal pan, vertical pane pan, right-axis Y scaling, wheel X zoom, pane
  separator resize, legends, and crosshair use separate hit regions;
- manual Y state is stored in `RenderSurfaceState`, not the immutable document;
- live market revisions do not clear manual viewport state;
- double-click explicitly clears manual X/Y state and reapplies observation margins.

## Verification

Windows CI `30898400950` passed:

- repository and real-data-only policy;
- architecture, indicator, comparison, and chart-observation contracts;
- future-space and auto-follow/reset viewport tests;
- vertical range pan and mouse-anchored Y-scale tests;
- invalid/minimum-range fail-closed tests;
- MSVC x64 `shell.exe` build;
- complete headless suite;
- clean-tree verification;
- executable artifact publication.

## Focused actual-screen acceptance

```powershell
.\build.bat
.\shell.exe
```

Confirm using actual `ka10080 + 0B` data:

1. the latest candle starts with visible space between it and the right Y axis;
2. horizontal drag can increase or reduce that right-side space;
3. vertical drag inside the price pane moves candles up/down;
4. dragging the right Y axis expands/contracts candle height around the cursor;
5. double-click restores a latest view with right, top, and bottom margins;
6. lower panes retain independent Y movement/scaling;
7. live updates preserve manual X/Y state until explicit reset;
8. wheel zoom, crosshair, legend selection, and pane splitters still work.

Return a screenshot only for a failed item. After acceptance, continue M8 with
code/name search and normalized relative-strength comparison. PR #1 remains Draft
until actual account/order, physical multi-source reconnect, and intraday soak are
accepted.
