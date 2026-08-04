# cppChart Modularization and Performance Plan

## Goal and baseline

Preserve verified real Kiwoom minute-bar, `0B`, account, and order paths while
building detachable feature modules joined to one generic renderer. Production
remains fail-closed and synthetic-free. Windows MSVC build and the complete
headless suite remain mandatory.

## Permanent rules

- ordinal bar-order X axis and shared pane slot geometry;
- renderer consumes generic documents/series/styles/references/pane metadata only;
- indicator catalog, instance lifecycle, parameters, and presentation stay outside renderer/shell;
- multiple same-type instances may share one pane with independent parameters/colors;
- draggable pane separators preserve minimum height and live-update stability;
- viewport, selection, and pane sizes survive live-tail replacement.

## M1-M6

Complete and M6 accepted on the actual screen/GPU path. Delivered real-data gates,
market/workspace/feature modules, execution levels, generic renderer, ordinal axis,
shared history/live tail, value grids, crosshairs, pan/zoom/latest reset,
boundaries/tooltips, and real `0B` viewport preservation.

## M7 — reusable and dynamically managed indicators

Implementation and automated verification are complete. Focused actual-screen
acceptance remains.

### Engine

One batch/incremental implementation per indicator, deterministic specs, eight
output channels/readiness mask, same-timestamp replacement, fail-closed errors,
explicit trading date/session VWAP, and SMA/JMA/VWAP/OBV/ADX implementations.

### Dynamic instances

`IndicatorInstanceDefinition` combines one calculation spec with visibility,
output bindings, pane placement, colors, widths, styles, and references.

Implemented:

- top selector and Add dialog;
- default/price/new/existing pane targets;
- duplicate with unique ID/color and preserved pane placement;
- independent same-type parameters and same-pane overlay;
- output visibility/pane assignment;
- hide/show and permanent delete;
- market-only fallback when no indicators are visible;
- complete candidate validation before module/plan replacement.

### Presentation and panes

- primary/secondary colors, width, solid/dashed/dotted style;
- pane height, auto/fixed/symmetric scale, min/max, decimals;
- reference CRUD and overbought/oversold quick creation;
- owner-based legends, selection, and highlighting;
- draggable separators with resize cursor/highlight/minimum height;
- persistent user resize state and deliberate configured-default synchronization;
- stable ImGui scopes and copied state across vector replacement.

### Verified baseline

- implementation: `dba98bb7bb4690aeee170b2c6e971f1fa5aa1bc1`
- Windows CI: `30868440522` (`#922`)
- artifact: `8877052838`
- digest: `sha256:9f2e83f83f807b198a7a68c1d1743b4da857dfa69fccd9335e31f8a57c6976c7`
- workflow: read-only

CI passed repository/real-data policy, architecture isolation, instance lifecycle/
shared-pane/color tests, style/reference/pane-layout tests, MSVC x64 build, full
legacy/M7 suite, clean-tree, and artifact publication.

### Actual-screen acceptance

Confirm add, duplicate, same-pane overlay with different parameter/color,
hide/show/delete, output styling, reference CRUD/overbought/oversold, pane
resize, and selection/viewport/pane-size preservation during actual `ka10080 + 0B`.

## M8

Not started: `ka20005`, `0I`, synchronized comparison axes, normalized return,
relative strength, beta/correlation, multi-workspace shared data.

## M9

Not started: one strategy evaluator for replay/backtest/live and generic
signal/order/fill/result chart contributions.

## Continuous verification

Every milestone runs repository policy, architecture boundaries, real-data-only
checks, MSVC build, full headless suite, clean-tree, and artifact publication. User
testing is reserved for actual data, visual/GPU interaction, reconnect, orders,
fills, and soak.
