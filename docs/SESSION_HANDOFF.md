# cppChart Session Handoff

## Mandatory first read

Read these before changing code:

1. `docs/ARCHITECTURE_CONSTITUTION.md`
2. `docs/MODULARIZATION_PLAN.md`
3. this handoff

## Repository state

- repository: `hoonoh57/cppChart`
- development branch: `p2/kiwoom-mock-gateway`
- PR: `#1`, Draft; do not merge before real-data and account acceptance
- production policy: real Kiwoom mock data only; no synthetic fallback
- verified M6 implementation commit: `d1c8d1dcd5e6fc8ebc5585af26b3deb20769a96c`
- verification-only Windows CI run: `30797703979`

## Product objective

Build a fast chart-based trading workbench that can add indicators, strategies,
index and multi-symbol comparison, replay, backtest, and multi-symbol trading
results without adding feature-specific branches to the renderer or returning to
a monolithic `shell_main.cpp`.

## Architecture invariants

- Objectify only major features with independent state and lifecycle.
- Keep Tick, Bar, render points, and indicator values as compact value types in
  contiguous storage.
- UI publishes commands and renders immutable snapshots; it does not mutate
  market or account state directly.
- The generic renderer consumes only `RenderDocument` and renderer interaction
  state. It must not know Kiwoom API IDs, indicators, strategies, or accounts.
- `Off` stops subscriptions, calculation, rendering, and retained memory;
  `Standby` retains state but stops expensive work; `Visible` renders without
  strategy execution; `Active` enables the complete feature.
- Real errors remain visible and fail closed. Never restore synthetic data to
  make a screen look complete.
- Batch, replay, and real-time indicator/strategy logic must share the same
  calculation implementation.

## Completed modularization

- `FeatureRegistry` and feature metrics
- `MarketDataModule`
- `ChartWorkspaceModule`
- generic `RenderDocument`, builder, and ImGui renderer
- market-data and chart-workspace feature-level controls
- WebSocket `0B` unsubscribe and reconnect behavior
- actual `ka10080` plus `0B` selected-symbol chart path
- verification-only CI; one-shot migration files removed

## M6 chart foundation completed remotely

- full loaded history remains available for viewport navigation
- wheel zoom anchored at mouse position
- right-button horizontal pan
- double-click reset and latest-bar auto-follow
- visible-range automatic value scale
- current-price line and label
- same-frame vertical crosshair synchronization across panes
- OHLCV and tick-count tooltip
- generic time and value axes
- calendar-date and abnormal session-gap boundaries
- headless viewport and time-boundary fixtures in the complete suite

## M6 performance structure

- completed candle history is immutable shared storage
- current live candle is a separate value tail
- same-minute `0B` updates reuse all completed candle history
- completed volume history is also shared and reused
- a new minute promotes the old live bar and rebuilds completed volume once
- boundary calculation is cached by completed-history structure revision
- renderer receives generic data only; it has no Kiwoom, indicator, strategy,
  or account-specific branch

## Verification result

Windows CI run `30797703979` passed:

- repository and secret-file policy
- core dependency boundary
- real-data-only production policy
- modular architecture boundary
- M6 shared-history and boundary contracts
- MSVC x64 `shell.exe` build
- complete headless test suite
- clean source-tree check
- executable artifact publication

## Current user-intervention point

Remote verification is complete. A focused local visual/GPU and real-`0B`
acceptance test is now required before M6 is declared complete.

Validate:

1. real `ka10080` history loads;
2. real `0B` updates the final candle without viewport reset;
3. wheel zoom centers on the mouse position;
4. right-button drag pans through the complete loaded history;
5. double-click returns to the latest bars and resumes auto-follow;
6. crosshair timestamp aligns across price and volume panes;
7. OHLCV/tick tooltip follows the nearest candle;
8. current-price line and label update with `0B`;
9. date/session boundary lines remain aligned while zooming and panning;
10. Market Data and Chart Workspace Off/Standby/Visible levels perform their
    documented work reduction without affecting broker-account liquidation.

## Next remote milestone after acceptance

M7 reusable indicator engine:

- batch and incremental parity contract
- indicator registry and parameter serialization
- SMA, JMA, VWAP, OBV, and ADX
- standard Line/Histogram/ReferenceLine renderer contributions
- no renderer changes per added indicator
