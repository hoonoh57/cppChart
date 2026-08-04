# cppChart Modularization and Performance Plan

## Goal

Preserve verified real Kiwoom minute, realtime, account, and order paths while
building detachable feature modules joined to one generic renderer. Production is
fail-closed and synthetic-free. Windows MSVC build and the full headless suite are
mandatory, but CI success alone never proves a user-visible feature is complete.

## Permanent rules

- ordinal bar-order X axis and one shared pane slot geometry;
- generic documents, series, axes, styles, references, and pane metadata only;
- feature lifecycle, calculation, source selection, and configuration outside renderer/shell;
- multiple indicator/comparison instances with independent parameters/styles;
- drag-resizable panes and stable viewport/selection/layout during live updates;
- isolated failures never corrupt the primary real chart;
- manual chart observation state belongs to the render surface and is never inferred
  from or written into calculation data;
- an approved implementation plan is an execution contract and must not be silently
  reduced, reordered, or partially declared complete;
- UI milestones are complete only after the full user path is visible and operable.

## Execution-control rules

These rules prevent repeated multi-hour delays on small UI changes.

1. Track every approved item as `not started / code / related tests / visual acceptance`.
2. Never remove or defer an approved item without explicit approval.
3. Implement the user path first: `input → candidates → selection → confirmation → action → result`.
4. If a UI task produces no direct screen-path change for 30 minutes, stop and recheck scope.
5. After two identical failures, abandon the current method; do not attempt a third equivalent retry.
6. Do not use full CI as the inner development loop. Run focused compile/tests first.
7. Full CI is normally limited to one integration run and one final retry.
8. Do not enable workflow write-back or commit source from CI unless explicitly approved.
9. Use `complete` only for actual-screen acceptance. Build/test success is `automated verification complete`.
10. A user asking whether work is still progressing is a process alarm: compare the approved checklist with actual source and screen changes immediately.

## M1-M6

Complete and visually accepted. Includes real-data gates, market/workspace/feature
modules, execution levels, generic renderer, ordinal axis, shared history/live tail,
value grids, crosshairs, pan/zoom/latest reset, boundaries/tooltips, and real `0B`
viewport preservation.

### Chart observation viewport extension

Implementation, automated verification, and actual-screen acceptance are complete.

Implemented and accepted:

- configurable future bar slots to the right of the latest candle;
- right-side space retained by initial view, live auto-follow, and reset;
- horizontal manual pan beyond the latest data index;
- pane-local vertical range pan;
- right Y-axis drag scaling anchored around the mouse value;
- minimum/finite range guards;
- pane-local manual Y state retained across live-tail updates;
- double-click reset to latest X plus automatic Y range with margins;
- independent hit regions for viewport pan, Y scaling, wheel zoom, legends,
  crosshair, and pane splitters;
- chart internal vertical scroll removed and bottom X axis always visible.

## M7 — indicators

Complete and user accepted as highly stabilized.

Catalog: SMA, EMA, JMA, Bollinger Bands, RSI, MACD, DMI, SuperTrend, VWAP, OBV,
and Wilder ADX.

All types use common batch/incremental calculation, same-timestamp replacement,
fail-closed validation, dynamic instances, same-pane overlay, hide/show/delete,
styles, references, axis/pane settings, legends, and drag-resizable pane layout.

## M8 — real stock/index comparison

M8 infrastructure and normalized comparison calculations are implemented and
automatically verified. **M8.2 is not complete because the approved top-toolbar
symbol-search user path is still missing.**

### Implemented and automatically verified

- up to 32 stock/index comparison definitions;
- arbitrary stock minutes and multiple stock `0B` live tails;
- `ka20005` index minutes and multiple `0J` live tails;
- reconnect restoration and explicit unsubscribe;
- KOSPI `001`, KOSDAQ `101` presets;
- decimal/x100 index-value normalization;
- docked comparison editor;
- separate lower pane primary axis;
- price-pane left secondary axis through generic `axisId`;
- multiple left-axis columns and independent ranges/precision;
- document-wide shared left-axis width;
- cached completed comparison render points;
- source failure isolation;
- `ka10099` symbol catalog request/parsing/search foundation;
- comparison-add popup code/name search;
- RawClose;
- Indexed100;
- ReturnPercent;
- RelativeStrength100;
- fixed first-common-timestamp anchor;
- transformed live point updates;
- completed-point cache reuse on live-only updates.

Verified baseline:

- branch HEAD before documentation update: `bd0dc433fbf3b071834f4a116387dfed712d4ffa`
- Windows CI: `30950313269` / `#1176`
- artifact: `8908930495`
- digest: `sha256:1e241704fc48e3f8f860f9b6f5d17e01a0a1dbc45b14eeffd4339e850107f770`

### Approved but not implemented or not accepted

- top toolbar code/Korean-name autocomplete;
- candidate list under the main symbol input;
- mouse selection in the main toolbar;
- Up/Down keyboard navigation;
- Enter confirmation;
- confirmed code/name/market state;
- rejection of arbitrary unconfirmed text before `Cmd::LoadSymbol`;
- one shared search component for main toolbar and comparison popup;
- duplicate comparison-source prevention;
- explicit catalog loading/loaded/error UI;
- recent symbol selection;
- actual-screen acceptance of all four normalized comparison modes;
- synchronized timeframe reload for every comparison;
- actual `ka20005 + 0J` visual/data acceptance;
- physical multi-source reconnect and intraday soak.

### Immediate P0 sequence

Do not start beta, correlation, ranking, or unrelated optimization before P0 acceptance.

1. Extract a shared symbol-search UI/state helper.
2. Replace `shell_main.cpp::DrawToolbar()` code-only `InputText` with the helper.
3. Implement candidate popup/list, mouse, Up/Down, Enter, Escape.
4. Keep a valid selected code/name/market separate from free query text.
5. Allow `Cmd::LoadSymbol` only from a valid catalog selection/exact six-digit match.
6. Replace comparison-popup private search UI with the shared helper.
7. Reject duplicate comparison kind+code.
8. Show catalog loading, symbol count, refresh, and exact failure.
9. Run focused search/catalog/UI-helper tests.
10. Build and verify the actual toolbar path locally.
11. Run full Windows CI once.
12. Obtain user screenshot acceptance.

P0 acceptance path:

```text
Type "삼성"
→ show catalog candidates
→ select "005930 삼성전자 [KOSPI]"
→ confirm code/name/market
→ request actual 005930 ka10080/0B
→ perform the same search in comparison add
```

No `M8.2 complete` statement is allowed before this path is accepted.

## M9

Not started: one strategy evaluator for replay/backtest/live and generic
signal/order/fill/result chart contributions.

## Continuous verification

Every milestone eventually runs policy, architecture, real-data-only, MSVC build,
full tests, clean-tree, and artifact publication. These are final integration gates,
not substitutes for focused development or actual-screen acceptance.

The exact operational handoff, current checklist, failure-prevention protocol, and
file-by-file next steps are maintained in `SESSION_HANDOFF.md`.
