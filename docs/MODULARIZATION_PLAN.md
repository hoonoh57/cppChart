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
- isolated failures never corrupt the primary real chart.

## M1-M6

Complete and visually accepted. Includes real-data gates, market/workspace/feature
modules, execution levels, generic renderer, ordinal axis, shared history/live tail,
value grids, crosshairs, pan/zoom/latest reset, boundaries/tooltips, and real `0B`
viewport preservation.

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

## Verified implementation

- HEAD: `1e0d0d7dcfd91de05435d69b954e75b573a93bad`
- Windows CI #1016: `30875659339`
- artifact: `8879511685`
- digest: `sha256:70c9f9e8d36a212d33da3c591121844ff05748170748b48b69fd8112130bf599`
- workflow: read-only

CI passed repository/real-data policy, architecture isolation, existing tests, new
indicator parity/replacement tests, comparison lifecycle/render tests, index
normalization, shared dual-axis geometry, MSVC x64 build, clean-tree, and artifact
publication.

## M9

Not started: one strategy evaluator for replay/backtest/live and generic
signal/order/fill/result chart contributions.

## Continuous verification

Every milestone runs policy, architecture, real-data-only, MSVC build, full tests,
clean-tree, and artifact publication. User testing is reserved for actual data,
visual/GPU interaction, reconnect, orders/fills, and soak.
