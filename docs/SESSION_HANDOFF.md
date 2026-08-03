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

## Current M6 status

Implemented:

- viewport state and tests
- wheel zoom, horizontal pan, double-click reset, auto-follow
- value/time axes
- current-price line
- OHLCV/tick tooltip
- same-frame synchronized vertical crosshair across panes
- full loaded history available for viewport navigation

Completed after the first M6 checkpoint:

- completed immutable history is separate from the mutable live bar;
- render documents share completed candle and volume history;
- same-minute `0B` updates do not copy the complete loaded history;
- a new minute promotes the old live bar and rebuilds completed volume history once.

M6 remote implementation is complete:

- calendar-date and abnormal session-gap boundaries are generic renderer data;
- boundary calculation is cached by completed-history structure revision;
- same-minute `0B` events do not recompute boundaries or copy completed history;
- viewport, boundary, market-data, workspace, and runtime tests are in the complete suite.

Still required before asking the user to test:

1. run the final Windows MSVC build and complete headless suite;
2. restore verification-only CI and remove one-shot migration files;
3. record the verified HEAD and CI run below.

## User-test policy

Do not request a local pull yet. Ask for a focused local test only after the
remaining M6 items are remotely verified. The focused test must cover real
`ka10080` history, `0B` last-bar updates, wheel zoom, pan, double-click reset,
crosshair synchronization, current-price line, and feature Off/Standby/Visible.

## Verification record

- verified HEAD: pending final M6 verification
- Windows CI run: pending
- local acceptance: not requested
