# cppChart Modularization and Performance Plan

## Goal

Preserve the verified real Kiwoom minute-bar, `0B`, account, and order paths while converting the shell from a growing monolith into detachable major feature modules joined to a slim generic renderer contract.

The plan deliberately avoids an object-per-value design and does not introduce a DLL plugin ABI yet.

## Baseline to preserve

- `TRADING_MODE=KIWOOM_MOCK` fail-closed configuration
- OAuth, WebSocket login, account-event registration, reconciliation, and order readiness
- `ka10080` real stock minute bars
- selected-symbol `0B` subscription with `refresh=1`
- live current-bar OHLCV and tick-count update
- actual account position and PnL display
- no synthetic production data
- emergency/position liquidation gated by broker readiness
- Windows MSVC build and all headless tests

## Permanent rendering rules

- Financial X coordinates use actual bar order, not elapsed wall-clock time.
- Every visible bar slot has one shared horizontal pitch across every pane.
- Candle, volume, indicator histogram, marker, and future trade-result series must align to that common slot geometry.
- Pane-local Y interaction belongs to the pane. The vertical timestamp crosshair may synchronize across panes, but a horizontal value crosshair must display the hovered pane's own value.
- Value snapping is supplied through generic pane metadata such as `ValueGrid`; the renderer must not contain broker, exchange, symbol, or indicator-specific branches.
- Drag, zoom, and crosshair behavior must remain functional while real `0B` updates arrive.

## Milestone M1 — architecture contracts and regression gates

Deliverables:

- `ARCHITECTURE_CONSTITUTION.md`
- feature execution levels and registry
- generic renderer document contract
- module performance metrics
- CI checks preventing forbidden dependencies and feature-specific renderer branches
- headless tests for level transitions, dependencies, metrics, and render contracts

Acceptance:

- no user-visible behavior change
- all existing tests pass
- new contracts compile without Win32, ImGui, D3D, or Kiwoom dependencies

## Milestone M2 — MarketDataModule

Responsibilities:

- request state
- selected code and minute unit
- REST page application
- `0B` tick merge
- latest quote access
- continuation state
- subscription status
- event and timing metrics
- immutable completed history plus live tail

Acceptance:

- `shell_main.cpp` owns no market-data mutex, bars, tick counters, or tick-to-bar merge logic
- real-data tests remain valid
- module state and snapshots are headless-testable
- same-minute updates do not copy full history

## Milestone M3 — generic chart renderer

Create:

- broker-independent `RenderDocument`
- price, volume, and future indicator panes
- candle, line, histogram, marker, reference-line, and annotation series
- generic ImGui renderer consuming only the render contract
- renderer state containing viewport, dirty revision, and interaction state

Acceptance:

- renderer contains no Kiwoom API IDs, indicator names, strategy names, or account concepts
- a fixture document renders without a live broker
- document builder and renderer remain separately testable

## Milestone M4 — FeatureRegistry and execution levels

Register:

- market data
- chart workspace
- trading/account
- diagnostics

Execution levels:

- Off
- Standby
- Visible
- Active

Acceptance:

- Off market data blocks subscriptions and requests
- hidden workspace does not rebuild render documents
- trading/account remains independently available for liquidation
- feature metrics are visible in diagnostics

## Milestone M5 — application coordinator and thin shell

`shell_main.cpp` retains only:

- Win32/D3D lifecycle
- ImGui frame lifecycle
- top-level composition
- device-loss handling
- calls to application/UI objects

Acceptance:

- no market, strategy, order, or chart-domain mutation remains in `shell_main.cpp`
- command routing is headless-testable
- startup and shutdown remain deterministic

## Milestone M6 — chart viewport foundation

Implemented:

- ordinal trading-time axis
- recent screen-sized initial window
- wheel zoom anchored at mouse position
- left or right horizontal drag pan
- double-click reset and latest-bar auto-follow
- visible-range automatic value scale
- current-price line and label
- synchronized vertical time crosshair across panes
- pane-local horizontal value crosshair
- OHLCV and tick-count tooltip
- time and value axes
- date and abnormal session-gap boundaries
- immutable completed history and mutable live tail
- common axis-slot body width for candles and histograms
- generic `ValueGrid` cursor snapping
- Korean stock quotation ladder configured by the market chart builder
- integer value cursor for the volume pane

M6 regression fixtures:

- overnight wall-clock gaps consume one adjacent ordinal slot
- duplicate and descending timestamps fail
- candle and volume body widths are identical for the same visible axis span
- body width respects zoom minimum and maximum
- value-grid boundaries and nearest-step rounding
- Korean equity bands: 1, 5, 10, 50, 100, 500, and 1,000 won
- volume cursor rounds to integer units
- architecture gate rejects forced nearest-close horizontal crosshair
- architecture gate requires left/right drag capture

Remaining M6 acceptance:

- one focused local visual/GPU test covering aligned volume width, left/right pan, wheel zoom, latest reset, price tick snapping, volume-pane cursor value, vertical crosshair alignment, and real `0B` viewport stability

## Milestone M7 — reusable indicator engine

Create one batch/incremental engine and registry. Initial indicators:

- SMA
- JMA
- VWAP
- OBV
- ADX

Acceptance:

- batch and incremental parity fixtures
- indicators publish standard Line/Histogram/ReferenceLine series
- renderer code does not change when indicators are added or removed
- feature level controls acquisition, calculation, and rendering work

## Milestone M8 — index and multi-symbol comparison

Connect:

- `ka20005` index minute bars
- `0I` real-time index values
- synchronized symbol/index time axes
- normalized return, relative strength, beta, and correlation
- multiple workspaces sharing source data

Acceptance:

- missing index data does not corrupt stock data
- comparison can be Off without changing the main chart
- multi-symbol limits are measured and enforced

## Milestone M9 — strategy, replay, and trade results

Use the same indicator and strategy code for replay, backtest, and real-time decisions. Publish:

- signals
- orders and fills
- average-price lines
- stop and target lines
- per-symbol and portfolio results

Acceptance:

- deterministic replay equals batch backtest for the same event stream
- live events use the same evaluator
- chart result visualization remains a renderer contribution

## Continuous verification

Every milestone runs:

- core dependency boundary checks
- real-data-only production checks
- architecture regression checks
- MSVC x64 shell build
- full headless suite
- clean working-tree check
- executable artifact upload

## User test policy

Do not request a local pull merely because files changed. Request testing only when the milestone depends on visual interaction, actual Kiwoom payloads, physical reconnection, order/fill behavior, or soak performance.
