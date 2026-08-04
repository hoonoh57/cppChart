# cppChart Modularization and Performance Plan

## Goal

Preserve verified real Kiwoom minute-bar, `0B`, account, and order paths while
building detachable feature modules joined to one slim generic renderer.

## Baseline

- fail-closed `TRADING_MODE=KIWOOM_MOCK`
- OAuth/WebSocket session, account registration/reconciliation, order readiness
- `ka10080` stock minute bars and selected-symbol `0B` restoration
- immutable completed history plus mutable live OHLCV/tick-count tail
- actual positions and PnL; no synthetic production data
- Windows MSVC build and complete headless suite

## Permanent rules

- X coordinates use ordinal bar order and all panes share bar-slot geometry.
- Renderer consumes only generic documents, series, styles, references, and pane metadata.
- Indicator catalog, instance lifecycle, parameters, and presentation stay outside renderer/shell.
- Multiple instances of one type may share a pane with independent parameters/colors.
- Pane separators are generic, draggable, and minimum-height constrained.
- Viewport, selection, and pane sizes remain stable during real-time live-tail updates.

## M1-M6

Status: complete. M6 was accepted on the actual screen/GPU path.

Delivered: architecture/real-data gates, `MarketDataModule`,
`ChartWorkspaceModule`, `FeatureRegistry`, execution levels, generic renderer,
ordinal axis, shared history/live tail, value grids, synchronized crosshairs,
pan/zoom/latest reset, boundaries/tooltips, and real `0B` viewport preservation.

## M7 — reusable and dynamically managed indicators

Status: engine, production wiring, legends, dynamic instance manager, pane resizing,
styles, reference-line editing, MSVC build, and full headless regression are
implemented. Focused actual-screen acceptance remains.

### Engine

- one batch/incremental implementation per indicator
- deterministic `IndicatorSpec` serialization
- maximum eight outputs/readiness mask
- same-timestamp live replacement and fail-closed error handling
- explicit `Bar.TradingDateYmd` and session VWAP reset
- SMA, JMA Value/Up/Down/Slope, VWAP bands, OBV/Signal/Direction, Wilder ADX

### Dynamic instances

`IndicatorInstanceDefinition` combines one spec with visibility, output bindings,
pane placement, colors, widths, styles, and references.

Implemented:

- property-grid top selector and `지표 추가` dialog
- default/price/new-lower/existing-lower target selection
- duplicate with unique ID and distinguishable color
- independent parameters for same-type instances
- same-pane overlay through preserved/remappable pane IDs
- per-output visibility and pane assignment
- hide/show that stops/restores calculation and rendering
- permanent delete and real market-only fallback when none are visible
- complete candidate validation before module/render-plan replacement

### Presentation

- per-output primary/secondary colors, width, solid/dashed/dotted style
- pane default height, auto/fixed/symmetric scale, min/max, decimals
- create/edit/hide/delete references and quick overbought/oversold levels
- generic reference labels and owner-based selection/highlighting
- draggable pane splitters with resize cursor, hover highlight, minimum height
- user drag state retained across live revisions
- explicit configured default-height changes applied without resetting every minute
- stable ImGui scopes and copied selection state across vector replacement

### Verification baseline

- implementation HEAD: `dba98bb7bb4690aeee170b2c6e971f1fa5aa1bc1`
- Windows CI: `30868440522` (`#922`)
- artifact: `8877052838`
- digest: `sha256:9f2e83f83f807b198a7a68c1d1743b4da857dfa69fccd9335e31f8a57c6976c7`
- workflow permission: read-only

CI passed repository/real-data policy, architecture boundaries, generic-renderer
no-indicator-name gates, instance lifecycle/shared-pane/color tests,
style/reference/pane-layout tests, MSVC x64 build, full legacy/M7 suite, clean-tree,
and artifact publication.

### Focused actual-screen acceptance

Confirm add, duplicate, same-pane overlay with different parameter/color, hide/show,
delete, output styling, reference CRUD/overbought/oversold, pane drag-resize, and
selection/viewport/pane-size preservation during actual `ka10080 + 0B` updates.

## M8 — index and multi-symbol comparison

Status: not started. Planned: `ka20005`, `0I`, synchronized axes, normalized return,
relative strength, beta/correlation, and multiple workspaces sharing source data.

## M9 — strategy, replay, and trade results

Status: not started. Planned: one evaluator for replay/backtest/live decisions and
generic signal/order/fill/result chart contributions.

## Continuous verification

Every milestone runs repository policy, architecture boundaries, real-data-only
checks, MSVC x64 build, full headless suite, clean-tree verification, and artifact
publication. User testing is reserved for visual/GPU behavior, actual market data,
physical reconnect, order/fill behavior, and soak performance.
