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
- corrected chart-axis implementation commit: `aa55b0bbd1e6b396553c45da777b4030a9f4bd7e`
- successful Windows CI run: `30803999147`
- artifact digest: `sha256:a35f64979593c8ddd37341a3458e28dae7d1d02fa123c9c94b62aa85a44ac709`

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
- Financial bars must use a trading-time coordinate axis. Wall-clock elapsed
  milliseconds must never be mapped directly to horizontal pixels.

## Completed modularization

- `FeatureRegistry` and feature metrics
- `MarketDataModule`
- `ChartWorkspaceModule`
- generic `RenderDocument`, builder, and ImGui renderer
- market-data and chart-workspace feature-level controls
- WebSocket `0B` unsubscribe and reconnect behavior
- actual `ka10080` plus `0B` selected-symbol chart path
- verification-only CI; one-shot migration files removed

## First local M6 acceptance result

The user loaded real `005930` one-minute data and exposed a critical visual
defect. The header and logs confirmed 900 real `ka10080` rows, but the chart
showed a few dense candle clusters separated by huge blank regions from 07/30
to 08/03.

Root cause:

- the renderer mapped absolute wall-clock timestamp differences directly to X;
- overnight, weekend, and session gaps consumed most of the screen width;
- all 900 loaded bars were forced into the initial viewport;
- this made a valid response look like corrupted market data.

This was a renderer-coordinate and initial-window defect. It was not repaired by
changing market data, inventing bars, deleting gaps, or adding synthetic values.

## Corrected M6 chart foundation

- `OrdinalTimeAxis` maps each actual bar to one equally spaced horizontal slot;
- overnight, weekend, lunch, halt, and session gaps consume no empty pixel span;
- real timestamps remain available for labels, crosshair, tooltip, and boundary
  annotations;
- the initial viewport opens the latest screen-sized bar window rather than the
  complete 900-bar history;
- right-button pan reaches all retained history;
- wheel zoom remains anchored at the mouse position;
- double-click returns to the latest window and resumes auto-follow;
- a manually panned viewport is not moved by same-minute `0B` updates;
- visible-range price scaling ignores off-screen bars;
- direct wall-clock X mapping is blocked by architecture verification;
- `time_axis_tests` proves a 17-hour overnight gap occupies one adjacent bar
  slot, not a proportional blank region.

## M6 performance structure

- completed candle history is immutable shared storage;
- current live candle is a separate value tail;
- same-minute `0B` updates reuse all completed candle history;
- completed volume history is also shared and reused;
- a new minute promotes the old live bar and rebuilds completed volume once;
- ordinal time axis and boundary calculation are cached by completed-history
  structure revision;
- renderer receives generic data only and has no Kiwoom, indicator, strategy,
  or account-specific branch.

## Verification result

Windows CI run `30803999147` passed after the coordinate correction:

- repository and secret-file policy;
- core dependency boundary;
- real-data-only production policy;
- modular architecture boundary;
- compressed trading-time renderer gate;
- MSVC x64 `shell.exe` build;
- complete headless test suite, including viewport, time-axis, time-boundary,
  market-data, workspace, runtime, order, and reconciliation tests;
- clean source-tree check;
- executable artifact publication.

## Current user-intervention point

Pull and retest the corrected chart only. The expected initial result is a normal
contiguous candlestick chart of recent bars, not three clusters separated by
blank calendar time.

Validate:

1. real `005930` one-minute history loads with adjacent candles across sessions;
2. the initial screen shows a usable recent window, not all 900 bars compressed;
3. date/session changes are indicated by lines without reserving blank width;
4. right-button drag pans back through the complete 900-bar history;
5. wheel zoom changes candle density around the mouse position;
6. double-click returns to the latest window and resumes auto-follow;
7. real `0B` updates the final candle without moving a manually panned viewport;
8. price and volume crosshair timestamps remain aligned;
9. current-price line, OHLCV tooltip, and visible-range price scale remain correct.

Do not proceed to M7 until this visual defect is confirmed fixed.

## Next remote milestone after acceptance

M7 reusable indicator engine:

- batch and incremental parity contract;
- indicator registry and parameter serialization;
- SMA, JMA, VWAP, OBV, and ADX;
- standard Line/Histogram/ReferenceLine renderer contributions;
- no renderer changes per added indicator.
