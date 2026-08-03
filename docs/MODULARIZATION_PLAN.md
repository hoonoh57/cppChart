# cppChart Modularization and Performance Plan

## Goal

Preserve the verified real Kiwoom minute-bar, `0B`, account, and order paths while
converting the shell from a growing monolith into detachable major feature modules
joined to one slim generic renderer contract.

The plan deliberately avoids an object-per-value design and does not introduce a
DLL plugin ABI before the internal contracts stabilize.

## Baseline to preserve

- fail-closed `TRADING_MODE=KIWOOM_MOCK`
- OAuth, WebSocket login, account-event registration, reconciliation, and order
  readiness
- `ka10080` real stock minute bars
- selected-symbol `0B` subscription with reconnect restoration
- live current-bar OHLCV and tick-count replacement
- actual account positions and PnL
- no synthetic production data
- broker-readiness-gated liquidation
- Windows MSVC build and complete headless suite

## Permanent rendering rules

- Financial X coordinates use actual bar order, not elapsed wall-clock time.
- Every visible bar slot has one shared horizontal pitch across all panes.
- Candle, volume, indicator histogram, marker, and trade-result series align to
  that common slot geometry.
- Pane-local Y interaction belongs to the pane. The timestamp crosshair may be
  synchronized, but a horizontal value crosshair displays the hovered pane's own
  value.
- Value snapping is supplied through generic pane metadata such as `ValueGrid`.
- The renderer contains no broker, exchange, symbol, indicator, or strategy
  branches.
- Drag, zoom, crosshair, and manual viewport state remain stable while real `0B`
  updates arrive.

## Milestone M1 — architecture contracts and regression gates

Status: complete.

Delivered:

- `ARCHITECTURE_CONSTITUTION.md`
- feature execution levels and registry
- generic renderer document contract
- module performance metrics
- CI checks for forbidden dependencies and feature-specific renderer branches
- headless execution-level, dependency, metric, and render-contract tests

## Milestone M2 — MarketDataModule

Status: complete.

Responsibilities implemented:

- request state, selected code, and minute unit
- REST page application and continuation state
- `0B` tick merge
- latest quote access
- subscription status
- immutable completed history plus mutable live tail
- event, timing, memory, merge, and drop metrics

The shell owns no market-data mutex, bar store, tick counters, or tick-to-bar
aggregation logic.

## Milestone M3 — generic chart renderer

Status: complete.

Delivered:

- broker-independent `RenderDocument`
- candle, line, histogram, marker, reference-line, and annotation contracts
- generic ImGui renderer consuming only that document
- independent viewport and render-surface state
- separately testable document builder and renderer

## Milestone M4 — FeatureRegistry and execution levels

Status: complete for the current production features.

Registered production features:

- market data
- chart workspace
- trading/account
- diagnostics

Execution levels:

- `Off`
- `Standby`
- `Visible`
- `Active`

The indicator feature is the next production registration required by M7 runtime
wiring.

## Milestone M5 — application coordinator and thin shell

Status: partially complete.

Completed:

- normalized modules and immutable snapshot flow
- generic chart workspace
- headless-tested market and order coordination

Remaining long-term cleanup:

- continue reducing `shell_main.cpp` to Win32/D3D lifecycle, top-level composition,
  and calls into application/UI objects
- keep all indicator, strategy, order, and chart-domain calculations outside the
  shell

## Milestone M6 — chart viewport foundation

Status: complete and accepted on the real screen/GPU path.

Implemented and verified:

- ordinal trading-time axis
- recent initial viewport
- mouse-anchored wheel zoom
- left/right horizontal drag pan
- double-click latest reset and latest-bar auto-follow
- visible-range value scale
- current-price line and label
- synchronized timestamp crosshair across panes
- pane-local value crosshair
- OHLCV/tick-count tooltip
- date and abnormal session-gap boundaries
- immutable completed history and mutable live tail
- common candle/histogram slot width
- generic `ValueGrid`
- Korean quotation ladder supplied by the market chart builder
- integer volume cursor
- pane-local floating timestamp label
- manual pan preservation during actual `0B` updates

The user confirmed the focused M6 real-screen test as normal. No M6 acceptance gate
remains.

## Milestone M7 — reusable indicator engine

Status: core calculation, module lifecycle, generic render contribution, workspace
composition, and coordinator are complete and verified. Production shell wiring and
one focused real-screen indicator acceptance remain.

### Verified core

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

### Verified data contract

- `Bar.TradingDateYmd` is explicit and calendar-validated
- REST `cntr_tm` parsing preserves the KST trading date
- `0B` new-bar creation preserves the same trading date
- VWAP session reset uses `TradingDateYmd`

### Verified application and renderer boundary

- `IndicatorModule` owns execution level, cache, revision, metrics, and calculation
- completed indicator output is shared across live-tail replacements
- `IndicatorRenderAdapter` publishes only generic line, histogram, and reference
  series
- `DefaultIndicatorRenderPlan` maps initial outputs outside the renderer
- ADX 20/25 and JMA slope zero are generic reference lines
- `ChartWorkspaceModule` composes market and indicator revisions independently
- contribution failure retains the last good document
- `IndicatorWorkspaceCoordinator` verifies the complete headless application flow
- standard pane IDs `price` and `volume` prevent duplicate price panes
- adding/removing indicators requires no change to renderer source

### Remaining M7 production acceptance

1. Register `indicators` in `FeatureRegistry`, depending on `market-data`.
2. Configure initial specs once during startup.
3. Wire indicator execution levels and cache release.
4. Feed the shared completed bars and live tail into `IndicatorModule`.
5. Use the indicator-aware ChartWorkspace composition overload.
6. Publish indicator metrics and health to diagnostics.
7. Link all required indicator/module/default-plan sources into `build.bat`.
8. Add a shell-integration architecture gate.
9. Pass the existing Windows CI.
10. Run one actual `ka10080 + 0B` visual acceptance covering overlays, lower panes,
    references, crosshair alignment, viewport stability, and live-tail updates.

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
