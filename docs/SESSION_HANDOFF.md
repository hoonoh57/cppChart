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
- verified M7 implementation HEAD: `da0154fae1784709caf19eb5a10932832385174e`
- production shell wiring commit: `b5c3b31c07ef790f68ec4c7ec56d66294c93607f`
- successful Windows CI: `30855098423` (`Windows CI #829`)
- successful CI artifact id: `8872240396`
- artifact digest: `sha256:b0a730d28afea64d4db83c113eb43cda4a260ffcbe77af238f54952e7f82a830`
- production policy: real Kiwoom mock data only; no synthetic fallback

The verified implementation passed repository policy, architecture gates, M6/M7
shell integration gates, MSVC x64 shell build, the complete headless suite,
clean-tree verification, and executable artifact publication. Later branch commits
may be documentation-only; always read the live HEAD after pulling.

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

## M7 reusable indicator engine — implementation and CI complete

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

### Module and generic rendering contract

- `IndicatorModule` owns execution levels, revisions, metrics, completed-history
  reuse, and live-tail incremental calculation.
- `IndicatorRenderAdapter` converts outputs into generic `LineSeries`,
  `HistogramSeries`, and `ReferenceLine` contributions.
- `DefaultIndicatorRenderPlan` maps the five initial indicators without adding
  indicator switches to the renderer.
- ADX publishes 20/25 reference lines; JMA slope publishes a zero reference line.
- `ChartWorkspaceModule` composes market and indicator revisions independently and
  retains the last good immutable document when a contribution fails.
- price and volume use stable pane IDs `price` and `volume`, so price overlays reuse
  the existing price pane rather than creating a duplicate pane.

### Production shell wiring

- `indicators` is registered in `FeatureRegistry` with `market-data` dependency.
- `InitialIndicatorSpecs()` and the default render plan are configured once at
  startup.
- the actual shared completed bars and live tail are passed to
  `IndicatorMarketSource`.
- indicator calculation runs only while the feature is `Visible` or `Active`.
- the indicator-aware `ChartWorkspaceModule::UpdateMarketChart` overload publishes
  one immutable market-plus-indicator document.
- indicator processing time, retained bytes, event/merge counts, symbol count,
  output-series count, readiness, and errors are published to diagnostics.
- `Off` releases calculated state and render-adapter caches.
- indicator initialization or calculation failure retains a usable market-only
  chart and exposes the fault instead of adding synthetic values.
- all M7 core and application sources are linked into `shell.exe` by `build.bat`.
- `ui/render_document_renderer.cpp` remains feature-agnostic.

### M7 verification

Windows CI `30855098423` (`#829`) passed:

- repository and temporary-script policy
- core and modular architecture boundaries
- real-data-only production policy
- M6 interaction contracts
- M7 production shell integration markers and source links
- generic renderer no-indicator-branch gate
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

## Current boundary: focused M7 real-screen acceptance remains

The code, production wiring, and automated verification are complete. M7 is not
marked visually accepted until the actual Kiwoom screen confirms the following:

1. a selected symbol loads from real `ka10080` and continues through `0B`;
2. SMA, JMA, and VWAP appear in the existing price pane with no duplicate price
   pane;
3. JMA Slope, OBV, and ADX appear in separate lower panes;
4. ADX 20/25 and JMA slope zero reference lines are visible;
5. timestamp crosshair and ordinal bar alignment remain synchronized across panes;
6. manual pan/zoom remains stable while the live tail is replaced;
7. turning the Indicators feature Off removes indicator work/contributions, and
   returning it to Visible or Active restores them.

## Exact local acceptance commands

```powershell
Set-Location "E:\2026\gpt\cpp\shell"

git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
call build.bat
.\shell.exe
```

When the focused screen test is normal, record M7 as closed and select the next
milestone from M8 index/multi-symbol comparison or the next explicitly prioritized
product requirement.

## PR policy

PR #1 remains Draft. Do not merge before the remaining real account/order,
physical reconnect, and intraday soak acceptance described by the PR policy.
