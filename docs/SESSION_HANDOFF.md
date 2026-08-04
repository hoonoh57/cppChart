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
- verified implementation HEAD: `8ed98e94abd32fdd4e5d1e93604da7708a2f9bc0`
- successful Windows CI: `30864225906` (`Windows CI #857`)
- successful CI artifact id: `8875539604`
- artifact digest: `sha256:af4026373e47bc77f7b6f3420887a19156639378beb174cff8a56b98487ffc9d`
- production policy: real Kiwoom mock data only; no synthetic fallback

The verified implementation passed repository policy, architecture gates, M6/M7
and interactive-legend integration gates, MSVC x64 shell build, the complete
headless suite, clean-tree verification, and executable artifact publication.
The current branch may be ahead because documentation was updated after the verified
implementation; always use the live branch HEAD after pulling.

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
- Legends are explicit generic metadata; selection resolves to a feature-owned
  instance ID, not an individual output line.
- Parameter schema, validation, Apply/Revert, and recalculation stay outside the
  renderer.

## Verified real-data and M1-M6 baseline

- fail-closed `TRADING_MODE=KIWOOM_MOCK`
- OAuth token, WebSocket login, account registration, and reconciliation
- `ka10080` real stock minute bars
- selected-symbol `0B` subscription, unsubscribe, and reconnect restoration
- same-minute live-tail OHLCV/tick-count replacement
- actual account positions and PnL
- generic `RenderDocument` and generic ImGui renderer
- ordinal trading-time axis and common multi-pane slot geometry
- synchronized timestamp crosshair and pane-local value crosshair
- left/right drag pan, mouse-anchored wheel zoom, double-click latest reset
- actual `0B` updates preserve a manually panned viewport

M6 visual/GPU acceptance was completed successfully by the user. M6 is closed.

## M7 reusable indicator engine — implementation and CI complete

### Implemented indicators

- SMA: `Value`
- JMA: `Value / Up / Down / Slope`
- OBV: `Value / Signal / Direction`
- ADX: Wilder `ADX`
- VWAP: `Value / Upper1 / Lower1 / Upper2 / Lower2`

### Calculation and data contract

- one `IndicatorInstance` implementation shared by batch and incremental paths
- deterministic `IndicatorSpec` JSON serialization/deserialization
- maximum eight fixed output channels with readiness mask
- same-timestamp live-bar replacement with state restoration
- invalid or descending input fails closed without partial batch publication
- explicit `Bar.TradingDateYmd` from REST `cntr_tm` and `0B` new bars
- VWAP resets by the explicit KST trading-date key

### Module, renderer, and production wiring

- `IndicatorModule` owns execution level, revisions, metrics, completed-history
  reuse, and live-tail incremental calculation.
- `IndicatorRenderAdapter` publishes generic lines, histograms, references, and
  legend contributions only.
- `DefaultIndicatorRenderPlan` maps the initial indicators outside the renderer.
- stable pane IDs `price` and `volume` prevent duplicate price panes.
- market and indicator revisions compose into one immutable chart document.
- initial specs and render plan are configured once during startup.
- shared completed bars and the live tail feed `IndicatorMarketSource`.
- calculation runs only while the feature is `Visible` or `Active`.
- diagnostics receives timing, memory, events, symbols, outputs, readiness, and
  faults.
- `Off` releases calculation state and adapter caches.
- indicator failure retains the real market-only chart and reports the fault.
- renderer source contains no indicator-name or calculation branches.

The supplied real-screen screenshot confirmed that price overlays, JMA Slope, OBV,
ADX, and reference lines render on the actual chart. The missing identification and
edit interaction reported from that screen is addressed below.

## Interactive pane legends and indicator properties — implementation complete

### Generic render and selection contract

- every pane can carry explicit `LegendEntry` metadata
- every render series/reference can carry a generic `ownerId`
- selectable legends require a non-empty feature-owned owner ID
- market price and volume legends are non-selectable
- all outputs of one indicator resolve to the same indicator instance

### Presentation and interaction

- legends render at the upper-left of every pane and wrap when needed
- price-pane JMA outputs and VWAP bands are grouped by indicator instance
- lower panes expose JMA Slope, OBV Signal, and ADX labels with parameters
- single click selects the complete indicator instance
- double click selects it and focuses the docked `프로퍼티` window
- selected outputs are highlighted generically across their panes
- legend hit-testing suppresses conflicting pan, zoom, and latest-reset gestures

Default labels include:

- `SMA 20`
- `JMA 20 P0 Pow2`
- `JMA Slope 20`
- `VWAP 1/2`
- `OBV Signal 20`
- `ADX 14`

### Property-grid contract

`app/indicator_properties.*` owns editable metadata and validation:

- SMA: `period`
- JMA: `period`, `phase`, `power`
- VWAP: `std_dev_1`, `std_dev_2`
- OBV: `signal_period`
- ADX: `period`

The docked `프로퍼티` window provides Apply and Revert. Apply validates a complete
candidate spec set, rebuilds the default plan, reconfigures the indicator module,
and recalculates from the same shared real market history. Invalid values are
rejected without publishing a partial configuration.

### Verification

Windows CI `30864225906` (`#857`) passed:

- temporary implementation files removed and workflow permissions restored to read
- architecture and real-data-only boundaries
- generic legend/owner validation and renderer selection gates
- renderer no-indicator-name gate
- property metadata and range-validation tests
- grouped legend and owner adapter tests
- MSVC x64 `shell.exe` build
- complete legacy and M7 headless suite
- clean source-tree verification
- executable artifact publication

## Current boundary: focused visual interaction acceptance remains

Run the actual shell and confirm:

1. the price pane shows stock, SMA, JMA, and VWAP legends;
2. JMA Slope, OBV, and ADX panes show their own legends;
3. single click highlights the complete indicator instance;
4. double click activates the docked `프로퍼티` tab/window;
5. changing one parameter and pressing Apply updates calculation and legend;
6. Revert restores the current applied values before Apply;
7. clicking a legend does not pan, zoom, or reset the chart;
8. live `0B` replacement preserves selection and manual viewport.

## Exact local acceptance commands

```powershell
Set-Location "E:\2026\gpt\cpp\shell"

git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
.\build.bat
.\shell.exe
```

If this focused interaction test is normal, record the legend/property extension as
accepted and close M7. The next product milestone is M8 index and multi-symbol
comparison unless the user explicitly reprioritizes another feature.

## PR policy

PR #1 remains Draft. Do not merge before the remaining real account/order,
physical reconnect, and intraday soak acceptance described by the PR policy.
