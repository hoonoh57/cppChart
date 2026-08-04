# cppChart Modularization and Performance Plan

## Goal and baseline

Preserve verified real Kiwoom minute, realtime, account, and order paths while
building detachable feature modules joined to one generic renderer. Production
remains fail-closed and synthetic-free. Windows MSVC build and the complete
headless suite remain mandatory.

## Permanent rules

- ordinal bar-order X axis and one shared pane slot geometry;
- generic documents, series, axes, styles, references, and pane metadata only;
- feature lifecycle, calculation, source selection, and configuration outside renderer/shell;
- multiple indicator or comparison instances with independent parameters/styles;
- drag-resizable panes and stable viewport/selection/layout during live updates;
- independent failures must not corrupt the primary real market chart.

## M1-M6

Complete. M6 actual-screen/GPU acceptance is closed. Delivered real-data gates,
market/workspace/feature modules, execution levels, generic renderer, ordinal axis,
shared completed history/live tail, value grids, crosshairs, pan/zoom/latest reset,
boundaries/tooltips, and real `0B` viewport preservation.

## M7 — reusable and dynamically managed indicators

Status: implementation and automated verification complete; user acceptance closed
with the report that indicator-management behavior is highly stabilized.

### Indicator engine and catalog

One batch/incremental implementation per type, deterministic specs, fixed output
channels/readiness mask, same-timestamp replacement, fail-closed errors, and
explicit trading-date/session VWAP.

Current catalog: SMA, EMA, JMA, Bollinger Bands, RSI, MACD, DMI, SuperTrend, VWAP,
OBV, and Wilder ADX.

### Dynamic instances and presentation

- selector and Add dialog;
- default/price/new/existing pane targets;
- duplicate with unique ID/color and preserved/remappable pane placement;
- independent same-type parameters and same-pane overlay;
- hide/show/delete and market-only fallback;
- per-output visibility, pane, colors, width, solid/dashed/dotted style;
- pane height and auto/fixed/symmetric scale;
- reference CRUD and overbought/oversold creation;
- owner-based legends/selection and draggable pane separators;
- complete candidate validation and safe same-frame vector replacement.

## M8 — real stock/index comparison

Status: first production slice implemented and automated verification complete.
Actual `ka20005 + 0I` visual/data acceptance remains.

### Data and lifecycle

`ComparisonModule` supports up to 32 configured comparison instances and preserves
source data across style/placement changes.

- arbitrary stock history and multiple stock `0B` live tails;
- arbitrary index/industry history through `ka20005` and multiple `0I` live tails;
- reconnect restoration and explicit unsubscribe;
- KOSPI preset `001`, KOSDAQ preset `101`;
- decimal and x100 index values normalize identically;
- one source failure is isolated from the primary chart and other comparisons.

### Placement and axes

- separate lower pane using its primary right axis;
- main price pane close overlay using a dedicated left secondary axis;
- multiple secondary-axis columns with independent ranges and precision;
- comparison color, width, style, label, divisor, decimals, pane title/height;
- add/reload/hide/show/delete from the docked `비교` editor;
- document-wide maximum left-axis width reserved by every pane, preserving candle,
  volume, indicator, crosshair, and time-axis alignment.

### Remaining M8 work after focused acceptance

- symbol/name search service instead of code-only entry;
- synchronized timeframe reload policy across all comparison instances;
- normalized return, relative strength, beta, and correlation modes;
- measured subscription/series limits and multi-workspace sharing;
- physical reconnect and intraday soak with multiple stocks and indices.

## Verified implementation baseline

- implementation HEAD: `1e0d0d7dcfd91de05435d69b954e75b573a93bad`
- Windows CI: `30875659339` (`#1016`)
- artifact: `8879511685`
- digest: `sha256:70c9f9e8d36a212d33da3c591121844ff05748170748b48b69fd8112130bf599`
- workflow permission: read-only

CI passed repository/real-data policy, architecture isolation, all existing tests,
new indicator parity/replacement tests, comparison lifecycle and render-cache tests,
index normalization, shared dual-axis geometry, MSVC x64 build, clean-tree, and
artifact publication.

## M9

Not started: one strategy evaluator for replay/backtest/live and generic
signal/order/fill/result chart contributions.

## Continuous verification

Every milestone runs repository policy, architecture boundaries, real-data-only
checks, MSVC build, full headless suite, clean-tree, and artifact publication. User
testing is reserved for actual data, visual/GPU interaction, reconnect, orders,
fills, and soak.
