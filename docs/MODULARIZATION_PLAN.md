# cppChart Modularization and Performance Plan

## Goal

Preserve verified real Kiwoom minute, realtime, account, and order paths while
building detachable feature modules joined to one generic renderer. Production is
fail-closed and synthetic-free. Windows MSVC build and the full headless suite are
mandatory.

## Permanent rules

- ordinal bar-order X axis and one shared pane slot geometry;
- generic documents, series, axes, styles, references, and pane metadata only;
- feature lifecycle, calculation, source selection, and configuration outside renderer/shell;
- multiple indicator/comparison instances with independent parameters/styles;
- drag-resizable panes and stable viewport/selection/layout during live updates;
- isolated failures never corrupt the primary real chart;
- manual chart observation state belongs to the render surface and is never inferred
  from or written into calculation data.

## M1-M6

Complete and visually accepted. Includes real-data gates, market/workspace/feature
modules, execution levels, generic renderer, ordinal axis, shared history/live tail,
value grids, crosshairs, pan/zoom/latest reset, boundaries/tooltips, and real `0B`
viewport preservation.

### Chart observation viewport extension

Implementation and automated verification are complete. Actual-screen acceptance
remains.

Implemented:

- configurable future bar slots to the right of the latest candle;
- right-side space retained by initial view, live auto-follow, and reset;
- horizontal manual pan beyond the latest data index without clipping the future
  observation area;
- pane-local vertical range pan by dragging inside the chart body;
- right Y-axis drag scaling anchored around the mouse value;
- minimum/finite range guards;
- pane-local manual Y state retained across live-tail updates;
- double-click reset to latest X position plus automatic Y range with top/bottom
  padding;
- independent hit regions for viewport pan, Y scaling, wheel zoom, legends,
  crosshair, and pane splitters.

Verified viewport implementation:

- branch HEAD: `80542a44cfcaea570703b46d4a84ad36025837b3`
- pull-request verification SHA: `96204f598925a302707b8d37f4351233e551e46b`
- Windows CI: `30898400950`
- artifact: `8888150112`
- digest: `sha256:1396a02c41e52434ba3ff828e775aa05fb8380a6d454d335762a245088caa226`

## M7 — indicators

Complete and user accepted as highly stabilized.

Catalog: SMA, EMA, JMA, Bollinger Bands, RSI, MACD, DMI, SuperTrend, VWAP, OBV,
and Wilder ADX.

All types use common batch/incremental calculation, same-timestamp replacement,
fail-closed validation, dynamic instances, same-pane overlay, hide/show/delete,
styles, references, axis/pane settings, legends, and drag-resizable pane layout.

## M8 — real stock/index comparison

First production slice implemented and automatically verified. Actual
`ka20005 + 0J` visual/data acceptance remains.

### Implemented

- up to 32 stock/index comparison definitions;
- arbitrary stock minutes and multiple stock `0B` live tails;
- `ka20005` index minutes and multiple `0J` live tails;
- reconnect restoration and explicit unsubscribe;
- KOSPI `001`, KOSDAQ `101` presets;
- decimal/x100 index-value normalization;
- docked editor with add/reload/hide/show/delete and styling;
- separate lower pane primary axis;
- price-pane left secondary axis through generic `axisId`;
- multiple left-axis columns and independent ranges/precision;
- document-wide shared left-axis width preserving pane/time alignment;
- cached completed comparison render points;
- source failure isolation.

### Next after focused acceptance

- code/name search instead of code-only entry;
- synchronized timeframe reload for all comparisons;
- normalized return, relative strength, beta, and correlation;
- measured subscription/series limits and multi-workspace sharing;
- physical multi-source reconnect and intraday soak.

## M9

Not started: one strategy evaluator for replay/backtest/live and generic
signal/order/fill/result chart contributions.

## Continuous verification

Every milestone runs policy, architecture, real-data-only, MSVC build, full tests,
clean-tree, and artifact publication. User testing is reserved for actual data,
visual/GPU interaction, reconnect, orders/fills, and soak.
