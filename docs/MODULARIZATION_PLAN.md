# cppChart Modularization and Performance Plan

## Goal

Preserve the verified real Kiwoom minute-bar, `0B`, account, and order paths while
converting the shell from a growing monolith into detachable major feature modules
joined to one slim generic renderer contract.

The plan deliberately avoids an object-per-value design and does not introduce a
DLL plugin ABI before the internal contracts stabilize.

## Baseline to preserve

- fail-closed `TRADING_MODE=KIWOOM_MOCK`
- OAuth, WebSocket login, account-event registration, reconciliation, and order readiness
- `ka10080` real stock minute bars
- selected-symbol `0B` subscription with reconnect restoration
- live current-bar OHLCV and tick-count replacement
- actual account positions and PnL
- no synthetic production data
- broker-readiness-gated liquidation
- Windows MSVC build and complete headless suite

## Permanent rendering and interaction rules

- Financial X coordinates use actual bar order, not elapsed wall-clock time.
- Every visible bar slot has one shared horizontal pitch across all panes.
- Candle, volume, indicator histogram, marker, and trade-result series align to that geometry.
- Pane-local Y interaction belongs to the pane; timestamp cursors may synchronize.
- Value snapping is supplied through generic pane metadata such as `ValueGrid`.
- The renderer contains no broker, symbol, indicator, strategy, or parameter-schema branches.
- Drag, zoom, crosshair, and manual viewport state remain stable during real `0B` updates.
- Legend presentation and hit-testing are generic renderer behavior.
- Selection resolves through a generic `ownerId`; child outputs do not become separate editable objects.
- Parameter metadata, validation, Apply/Revert, and recalculation belong to the feature/application layer.

## Milestones M1-M6

Status: complete. M6 was accepted on the actual screen/GPU path.

Delivered across M1-M6:

- architecture constitution and regression gates
- `MarketDataModule`, `ChartWorkspaceModule`, and `FeatureRegistry`
- generic `RenderDocument` and ImGui renderer
- `Off / Standby / Visible / Active`
- ordinal trading-time axis and common multi-pane slot geometry
- immutable completed history plus mutable live tail
- value grids, crosshairs, pan/zoom, latest reset, boundaries, and tooltips
- actual `0B` updates preserving a manually panned viewport

## Milestone M7 — reusable indicator engine

Status: calculation engine, data contract, modules, generic render contribution,
production shell wiring, interactive legends/properties, architecture gates, MSVC
build, and complete headless regression are complete. Focused visual interaction
acceptance remains.

### Verified indicator core

- one batch/incremental implementation per indicator
- deterministic `IndicatorSpec` serialization
- maximum eight output channels and readiness mask
- same-timestamp live-bar replacement
- reset, invalid-input, descending-time, and fail-closed contracts
- no partial batch output after failure

Initial indicators:

- SMA: Value
- JMA: Value / Up / Down / Slope
- VWAP: Value / Upper1 / Lower1 / Upper2 / Lower2
- OBV: Value / Signal / Direction
- ADX: Wilder ADX

### Verified data and application contract

- explicit, validated `Bar.TradingDateYmd`
- REST `cntr_tm` and `0B` new-bar trading-date preservation
- VWAP session reset by `TradingDateYmd`
- `IndicatorModule` owns execution level, cache, revisions, metrics, and calculation
- completed output is shared across live-tail replacements
- `IndicatorRenderAdapter` publishes generic lines, histograms, references, and legends
- stable `price` and `volume` pane IDs prevent duplicate price panes
- `ChartWorkspaceModule` composes market and indicator revisions independently
- failure retains the last good or market-only real document
- all indicator sources are linked into production `shell.exe`

### Interactive legend and property extension

Implemented:

- explicit pane `LegendEntry` collections
- generic `ownerId` on render series and references
- upper-left pane legends with wrapping and color swatches
- grouped legends for multi-output JMA and VWAP instances
- lower-pane JMA Slope, OBV, and ADX labels
- single-click instance selection
- double-click focus of the docked `프로퍼티` window
- generic highlighting of all outputs owned by the selected instance
- legend hit-test isolation from pan, zoom, and latest reset
- feature-owned descriptors and range validation
- Apply/Revert for SMA, JMA, VWAP, OBV, and ADX parameters
- validated plan/module reconfiguration from the same shared real history

Windows CI `30864225906` (`#857`) passed:

- repository and temporary-file policy
- architecture and real-data-only boundaries
- M6/M7 and legend/property integration gates
- generic renderer no-indicator-name gate
- grouped legend/owner adapter tests
- property metadata/range tests
- MSVC x64 shell build
- complete headless suite
- clean-tree verification and artifact publication

### Remaining M7 acceptance

Run one actual `ka10080 + 0B` screen test and confirm:

1. price and lower-pane legends are readable and grouped correctly;
2. single click selects/highlights the complete indicator instance;
3. double click focuses the property window;
4. Apply updates calculation and parameterized legend label;
5. Revert restores applied values before Apply;
6. legend interaction does not alter viewport state;
7. selection and viewport survive live-tail replacement.

After this focused acceptance, M7 can be marked complete.

## Milestone M8 — index and multi-symbol comparison

Status: not started.

Planned:

- `ka20005` index minute bars
- `0I` real-time index values
- synchronized symbol/index axes
- normalized return, relative strength, beta, and correlation
- multiple workspaces sharing source data

Acceptance:

- missing index data does not corrupt stock data
- comparison may be Off without changing the main chart
- measured and enforced multi-symbol limits

## Milestone M9 — strategy, replay, and trade results

Status: not started.

Planned:

- one strategy evaluator for replay, backtest, monitoring, and live decisions
- signal, order, fill, average-price, stop, target, and result contributions
- per-symbol and portfolio result visualization

Acceptance:

- deterministic replay equals batch backtest for the same normalized event stream
- live events use the same evaluator
- results remain generic renderer contributions

## Continuous verification

Every implementation milestone runs:

- repository and secret-file policy
- core dependency boundary
- real-data-only production checks
- modular architecture checks
- MSVC x64 shell build
- complete headless suite
- clean working-tree verification
- executable artifact publication

## User test policy

Do not request a local pull merely because files changed. Request user testing only
when acceptance depends on visual interaction, actual Kiwoom payloads, physical
reconnection, order/fill behavior, or soak performance. The request must be narrow,
state the expected result, and require evidence only on failure.
