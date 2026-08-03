# cppChart Session Handoff

## Mandatory first read

Read these before changing code:

1. `docs/ARCHITECTURE_CONSTITUTION.md`
2. `docs/MODULARIZATION_PLAN.md`
3. this handoff

## Repository state

- repository: `hoonoh57/cppChart`
- local repository root: `E:\2026\gpt\cpp\shell`
- development branch: `p2/kiwoom-mock-gateway`
- PR: `#1`, Draft
- protected baseline: `main` at `f1a7d8db7a5d1b145781bfcb6ce11c2e24ef6683`
- verified branch HEAD: `2f9cea39551b8f484d22ecd834738532ba4478a5`
- successful Windows CI: `30853400640` (`Windows CI #816`)
- successful CI artifact id: `8871604239`
- artifact digest: `sha256:47623bb63fae35d4049dc1b85b0cc1b5b3495cd8380319f1ae275f1766c1af00`
- production policy: real Kiwoom mock data only; no synthetic fallback

The verified HEAD above passed architecture gates, MSVC x64 shell build, the complete
headless suite, clean-tree verification, and executable artifact publication.
Always read the live branch HEAD after pulling in case a later commit exists.

## Exact next-session opening commands

```powershell
Set-Location "E:\2026\gpt\cpp\shell"

git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
git status
git rev-parse HEAD
```

Do not alter `.env` unless a concrete configuration error is shown. Never expose
App Key, Secret Key, bearer token, or account-sensitive response data.

## Product objective

Build a fast chart-based trading workbench supporting freely developed indicators
and strategies, index and multi-symbol comparison, replay, backtest, and
multi-symbol trading-result visualization without returning to a monolithic
`shell_main.cpp` or adding feature-specific branches to the renderer.

## Permanent architecture invariants

- Production uses normalized real Kiwoom data only and fails closed.
- Objectify only major features with independent state and lifecycle.
- Tick, Bar, indicator values, and render points remain compact value types in
  contiguous storage.
- UI publishes commands and renders immutable snapshots.
- The generic renderer consumes only `RenderDocument`; it never calculates or
  identifies indicators, strategies, brokers, or accounts.
- Batch, incremental, replay, backtest, and live paths share one calculation
  implementation.
- Completed history is immutable and shared; only the live tail is replaced.
- Financial X coordinates use ordinal bar order rather than elapsed wall-clock
  milliseconds.
- `Off / Standby / Visible / Active` must control real work and retained state.

## Verified real-data and M1-M6 baseline

- fail-closed `TRADING_MODE=KIWOOM_MOCK`
- OAuth token, WebSocket login, account registration, and reconciliation
- `ka10080` real stock minute bars
- selected-symbol `0B` subscription, unsubscribe, and reconnect restoration
- same-minute live-tail OHLCV/tick-count replacement
- actual account positions and PnL
- `FeatureRegistry`, `MarketDataModule`, and `ChartWorkspaceModule`
- generic `RenderDocument` and generic ImGui renderer
- ordinal trading-time axis
- common candle/histogram slot geometry
- pane-aware `ValueGrid`
- synchronized time crosshair and pane-local value crosshair
- left/right drag pan, mouse-anchored wheel zoom, double-click latest reset
- actual `0B` updates preserve a manually panned viewport

M6 visual/GPU acceptance was completed successfully by the user. M6 is closed.

## M7 reusable indicator foundation — verified complete

### Core calculation contract

- one `IndicatorInstance` implementation shared by batch and incremental paths
- deterministic `IndicatorSpec` JSON serialization/deserialization
- maximum eight fixed output channels with readiness mask
- same-timestamp live-bar replacement with state restoration
- descending timestamp and invalid-input fail-closed handling
- failed batch calculation does not publish partial output

### Implemented indicators

- SMA: `Value`
- JMA: `Value / Up / Down / Slope`
- OBV: `Value / Signal / Direction`
- ADX: Wilder `ADX`
- VWAP: `Value / Upper1 / Lower1 / Upper2 / Lower2`

### Trading-date contract

`Bar` carries explicit `TradingDateYmd`. REST `cntr_tm` minute bars and new bars
created from `0B` preserve the same KST trading date. VWAP resets by that explicit
session key rather than inferring a date from display text or local wall time.

### Module and rendering contract

- `IndicatorModule` implements execution levels, revisions, timing/event metrics,
  completed-history cache reuse, and live-tail incremental calculation.
- `IndicatorRenderAdapter` converts outputs into generic `LineSeries`,
  `HistogramSeries`, and `ReferenceLine` contributions.
- `DefaultIndicatorRenderPlan` maps the five initial indicators without adding
  indicator switches to the renderer.
- ADX publishes 20/25 reference lines; JMA slope publishes a zero reference line.
- `ChartWorkspaceModule` composes market and indicator revisions independently and
  retains the last good immutable document when a contribution fails.
- `IndicatorWorkspaceCoordinator` verifies configuration, execution level,
  calculation, default plan, adapter, and chart composition as one headless flow.
- market chart pane IDs are standardized as `price` and `volume`, allowing price
  overlays to reuse the existing price pane rather than creating a duplicate.

### M7 verification

Windows CI `30853400640` passed:

- repository and secret-file policy
- core and modular architecture boundaries
- real-data-only production policy
- M6 interaction contracts
- MSVC x64 `shell.exe` build
- every indicator parity/live-tail/reset/error fixture
- IndicatorModule cache and monotonic revision fixtures
- generic render adapter and reference-line fixtures
- default render-plan fixture
- indicator-aware ChartWorkspace fixture
- IndicatorWorkspaceCoordinator fixture
- complete legacy headless suite
- clean source-tree verification
- executable artifact publication

## Current boundary: M7 shell runtime wiring remains

The verified engine, module, adapter, plan, workspace composition, and coordinator
are headless-complete. The production `shell_main.cpp` still builds the market-only
`ChartWorkspaceModule` path. Therefore the five indicators are not yet displayed
on the actual Kiwoom chart screen.

Do not describe M7 as visually complete until the following wiring is finished and
locally accepted.

## Exact next implementation order

1. Register an `indicators` feature depending on `market-data`.
2. Configure `InitialIndicatorSpecs()` once during application startup.
3. Route indicator execution-level changes through `IndicatorModule` or the
   verified coordinator; `Off` must release calculation and adapter caches.
4. In the existing market snapshot path, build one `IndicatorMarketSource` from
   the shared completed bars and live tail.
5. Calculate only when the indicator feature is `Visible` or `Active`.
6. Call the indicator-aware `ChartWorkspaceModule::UpdateMarketChart` overload so
   market and indicator contributions form one immutable document.
7. Record indicator processing time, retained bytes, event/merge counts, symbol
   count, render-series count, readiness, and last error in `FeatureRegistry`.
8. Add all required M7 sources to `build.bat`; no indicator calculation may be
   copied into `shell_main.cpp`.
9. Add a shell-integration architecture gate requiring the indicator feature and
   coordinator/module markers while continuing to prohibit indicator names in
   `ui/render_document_renderer.cpp`.
10. Run the existing Windows CI and require complete success.
11. Perform one focused local visual test using actual `ka10080 + 0B` data:
    price overlays, OBV/ADX/JMA-slope panes, ADX/JMA reference lines, shared
    crosshair/viewport alignment, and stable live-tail updates.

## PR policy

PR #1 remains Draft. Do not merge before the remaining real account/order,
physical reconnect, and intraday soak acceptance described by the PR policy.
M7 runtime wiring and its focused real-screen acceptance must also be recorded
before claiming the indicator milestone is closed.
