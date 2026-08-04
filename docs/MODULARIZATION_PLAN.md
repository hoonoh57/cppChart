# cppChart Modularization and Performance Plan

## Goal

Preserve verified real Kiwoom minute-bar, `0B`, account, and order paths while
building detachable major feature modules joined to one slim generic renderer.

## Baseline to preserve

- fail-closed `TRADING_MODE=KIWOOM_MOCK`
- OAuth/WebSocket session, account-event registration, reconciliation, and order readiness
- `ka10080` stock minute bars and selected-symbol `0B` restoration
- mutable live OHLCV/tick-count tail over immutable completed history
- actual positions and PnL; no synthetic production data
- Windows MSVC build and complete headless suite

## Permanent rendering and interaction rules

- horizontal coordinates use ordinal bar order;
- every pane shares one bar-slot geometry;
- renderer consumes only generic document metadata and standard series;
- indicator catalog, parameters, instance lifecycle, and style editing stay outside the renderer;
- viewport, crosshair, selection, and pane sizes remain stable during live-tail updates;
- pane separators are generic, draggable, minimum-height constrained controls;
- multiple indicator instances may share one pane with independent colors and parameters.

## M1-M6

Status: complete. M6 was accepted on the actual screen/GPU path.

Delivered:

- architecture and real-data-only gates
- `MarketDataModule`, `ChartWorkspaceModule`, `FeatureRegistry`
- generic `RenderDocument` and ImGui renderer
- `Off / Standby / Visible / Active`
- ordinal axis, shared history/live tail, pane value grids
- synchronized crosshairs, pan/zoom, latest reset, boundaries/tooltips
- actual `0B` updates preserving manually panned viewport state

## M7 — reusable and dynamically managed indicators

Status: engine, production wiring, legends, instance manager, pane resizing,
styles, reference-line editing, MSVC build, and complete headless regression are
implemented. Focused actual-screen acceptance remains.

### Calculation engine

- one batch/incremental implementation per indicator
- deterministic `IndicatorSpec` serialization
- maximum eight outputs and readiness mask
- same-timestamp live-tail replacement
- reset, invalid-input, descending-time, and fail-closed contracts
- explicit `Bar.TradingDateYmd` and session VWAP reset

Implemented indicators:

- SMA: Value
- JMA: Value / Up / Down / Slope
- VWAP: Value / Upper1 / Lower1 / Upper2 / Lower2
- OBV: Value / Signal / Direction
- ADX: Wilder ADX

### Dynamic instance configuration

`IndicatorInstanceDefinition` owns one calculation spec plus presentation and
lifecycle metadata. The active definition set supports:

- top-of-property-grid indicator selection;
- Add dialog for SMA/JMA/VWAP/OBV/ADX;
- duplicate with unique ID and distinguishable color variant;
- visible/hidden state and permanent deletion;
- independent parameter sets for multiple instances of one type;
- per-output visibility and pane assignment;
- insertion into default, price, new lower, or existing lower pane;
- same-pane overlay of duplicated/differently parameterized instances;
- complete candidate validation before module/plan replacement.

Hidden instances do not calculate or render. Removing the final visible instance
falls back to the real market-only chart rather than introducing dummy values.

### Presentation and property editing

The feature-owned manager supports:

- output primary/secondary colors;
- line width and solid/dashed/dotted style;
- pane default height, auto/fixed/symmetric value scale, fixed min/max, decimals;
- create/edit/hide/delete reference lines;
- quick overbought and oversold reference creation;
- Apply/Revert with stable ImGui begin/end scope;
- safe vector replacement using copied selection state rather than invalid pointers.

The generic renderer supports:

- explicit pane legends and owner-based selection/highlighting;
- grouped multi-output indicator legends;
- styled lines/references without indicator-name branches;
- reference labels;
- drag-resizable pane separators with resize cursor and highlighted border;
- preservation of user-resized weights across live revisions;
- adoption of an explicitly changed configured pane default.

### Verification

Final code and read-only workflow baseline:

- implementation HEAD: `dba98bb7bb4690aeee170b2c6e971f1fa5aa1bc1`
- Windows CI: `30868440522` (`#922`)
- artifact: `8877052838`
- digest: `sha256:9f2e83f83f807b198a7a68c1d1743b4da857dfa69fccd9335e31f8a57c6976c7`

CI passed:

- repository/temporary-file and real-data-only policy
- core/module/workspace/dynamic-indicator architecture gates
- generic renderer no-indicator-name gate
- instance hide/duplicate/shared-pane/color tests
- style/reference and pane-weight tests
- MSVC x64 `shell.exe` build
- complete legacy and M7 headless suite
- clean-tree verification and executable publication

### Focused M7 visual acceptance

Confirm on actual `ka10080 + 0B` data:

1. add a new indicator from the property-grid top button;
2. duplicate one indicator and overlay both instances in the same pane;
3. change parameters and assign distinguishable colors/styles/widths;
4. hide/show and delete an instance;
5. create/edit/delete a reference or overbought/oversold line;
6. drag pane separators and verify the resize cursor, highlight, and minimum height;
7. verify selection, viewport, and pane sizes remain stable during `0B` updates.

## M8 — index and multi-symbol comparison

Status: not started.

Planned: `ka20005`, `0I`, synchronized symbol/index axes, normalized return,
relative strength, beta/correlation, and multiple workspaces sharing source data.

## M9 — strategy, replay, and trade results

Status: not started.

Planned: one evaluator for replay/backtest/live decisions and generic signal/order/
fill/result chart contributions.

## Continuous verification

Every milestone runs repository policy, architecture boundaries, real-data-only
checks, MSVC x64 build, full headless suite, clean-tree verification, and artifact
publication. User testing is requested only for visual/GPU behavior, actual market
payloads, physical reconnect, orders/fills, or soak performance.
