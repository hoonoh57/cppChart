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
- PR: `#1`, Draft; do not merge before real-data, visual, account, and order acceptance
- protected baseline: `main` at `f1a7d8db7a5d1b145781bfcb6ce11c2e24ef6683`
- production policy: real Kiwoom mock data only; no synthetic fallback
- verified pane crosshair-label implementation: `5436c1988f80365e8b91d78384bb6da3f5261e8a`
- last successful Windows CI: `30821367012`
- successful CI artifact id: `8859035969`
- artifact digest: `sha256:b212d2beb3ff7fbd6d93d70d9f21f6767597bd52c8e6904d79ee056cbb9724c8`

The branch may contain a later documentation-only commit. Treat the implementation
commit and successful CI above as the verified code baseline, then read the live
branch HEAD with `git rev-parse HEAD` after pulling.

## Exact next-session opening commands

```powershell
Set-Location "E:\2026\gpt\cpp\shell"

git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
git status
git rev-parse HEAD
```

Do not alter `.env` unless a real configuration error is shown. Do not expose App
Key, Secret Key, bearer token, or account-sensitive response data.

## Product objective

Build a fast chart-based trading workbench that supports freely developed
indicators and strategies, index and multi-symbol comparison, replay, backtest,
and multi-symbol trading-result visualization without returning to a monolithic
`shell_main.cpp` or adding feature-specific branches to the renderer.

## Architecture invariants

- Objectify only major features with independent state and lifecycle.
- Keep Tick, Bar, render points, and indicator values as compact value types in
  contiguous storage.
- UI publishes commands and renders immutable snapshots; it does not mutate
  market or account state directly.
- The generic renderer consumes only `RenderDocument` and interaction state. It
  must not know Kiwoom API IDs, indicators, strategies, or accounts.
- Pane-specific cursor snapping is supplied as generic `ValueGrid` data by the
  document builder; the renderer contains no Korean-market special branch.
- Candle and histogram body widths derive from the same visible ordinal-axis
  slot geometry.
- Crosshair time labels are generic pane geometry and must work unchanged for
  price, volume, and future indicator panes.
- `Off` stops subscriptions, calculation, rendering, and retained memory;
  `Standby` retains state but stops expensive work; `Visible` renders without
  strategy execution; `Active` enables the complete feature.
- Real errors remain visible and fail closed. Never restore synthetic data to
  make the screen appear complete.
- Batch, replay, and real-time indicator/strategy paths share the same
  calculation implementation.
- Financial bars use the ordinal trading-time axis. Wall-clock elapsed
  milliseconds must never be mapped directly to horizontal pixels.

## Verified real-data baseline

- fail-closed `TRADING_MODE=KIWOOM_MOCK`
- OAuth token and Kiwoom mock WebSocket login
- account event registration and reconciliation
- `ka10080` real stock minute bars
- selected-symbol `0B` subscription, unsubscribe, and reconnect restoration
- immutable completed history plus mutable live tail
- same-minute `0B` OHLCV/tick-count update without copying all history
- actual account position and PnL display
- no synthetic market, position, fill, or ranking data

## Completed modularization

- `FeatureRegistry`
- `MarketDataModule`
- `ChartWorkspaceModule`
- generic `RenderDocument`
- generic ImGui render-document renderer
- feature-level `Off / Standby / Visible / Active`
- WebSocket `0B` subscription lifecycle
- compressed ordinal trading-time axis
- shared candle/histogram slot geometry
- pane-aware `ValueGrid`
- pane-aware crosshair timestamp labels

## M6 real-screen findings and corrections

### 1. Calendar gaps consumed X-axis width

Absolute timestamps created huge overnight and weekend blank regions.
`OrdinalTimeAxis` now assigns one horizontal slot per real bar while preserving
real timestamps for labels, tooltips, and session boundaries.

### 2. Candle and volume widths differed

The volume pane inferred body width independently and produced overlapping bars.
`SeriesBodyWidth` now derives both candle and histogram bodies from one shared
visible-axis slot pitch.

### 3. Drag did not pan

The chart surface did not reliably capture the intended button. Both left and
right horizontal drag now pan the same viewport; wheel zoom remains anchored at
the mouse; double-click returns to the latest range.

### 4. Horizontal crosshair was forced to candle close

The mouse-derived Y value was overwritten by the nearest candle close. The
horizontal line now follows mouse Y in the hovered pane. Price uses its legal
quotation grid; volume uses an integer grid; future indicator panes may define
independent decimals and grids.

### 5. Crosshair time had no floating label

The vertical line was visible but its selected time appeared only in the candle
tooltip. The renderer now draws a floating `MM/DD HH:mm` label in every hovered
pane:

- bottom pane: inside its reserved time-axis band;
- middle/upper pane: inside the pane's lower edge;
- left/right edge: clamped within the active pane;
- price/volume value label remains visible simultaneously.

Implementation files:

- `render/cursor_label_layout.h`
- `ui/render_document_renderer.cpp`
- `tests/cursor_label_layout_tests.cpp`
- `tests/run_all.bat`
- `scripts/verify_modular_architecture.ps1`

## Verification result

Windows CI `30821367012` passed:

- repository and secret-file policy
- core dependency boundary
- real-data-only production policy
- modular renderer boundary
- ordinal trading-time-axis contract
- left/right drag interaction markers
- pane-aware `ValueGrid`
- shared candle/volume body geometry
- pane crosshair time-label placement and edge clamping
- MSVC x64 `shell.exe` build
- complete headless test suite
- clean source-tree verification
- executable artifact publication

## Current user-intervention point

M6 is not accepted until one focused real-screen test confirms the latest
crosshair-label behavior.

Validate only:

1. price-pane hover shows both the quotation-grid price label and floating time;
2. volume-pane hover shows both integer volume and the same floating time;
3. the floating time remains inside the pane near both horizontal edges;
4. left/right drag, wheel zoom, and double-click latest reset still work;
5. candle and volume bars remain horizontally aligned;
6. actual `0B` updates do not reset a manually panned viewport.

Local pull/build/launch:

```powershell
Get-Process shell -ErrorAction SilentlyContinue | Stop-Process -Force

powershell -NoProfile `
    -ExecutionPolicy Bypass `
    -File E:\2026\gpt\cpp\shell\scripts\pull_and_verify.ps1 `
    -Branch p2/kiwoom-mock-gateway `
    -Launch
```

Do not start M7 until this visual acceptance succeeds.

## Next milestone after M6 acceptance

M7 reusable indicator engine:

- one batch/incremental parity contract
- indicator registry and parameter serialization
- SMA, JMA, VWAP, OBV, and ADX
- standard Line/Histogram/ReferenceLine render contributions
- no renderer changes per added indicator
- real-time last-bar updates without full-history recomputation

## Session-efficiency rule

The crosshair-label request was a small renderer change and should not require a
long multi-workflow migration. Future small UI defects must use this sequence:

1. inspect the exact renderer path once;
2. patch the smallest responsible module;
3. add one focused regression test;
4. use the existing Windows CI only;
5. report status immediately instead of repeatedly polling silently;
6. do not create temporary one-shot workflows or repair scripts unless direct
   repository writes are technically impossible;
7. remove any unavoidable temporary automation before closing the session.

Conversation length must not be compensated for by speculative edits. When
context becomes expensive to reconstruct, stop feature work, refresh this
handoff, and continue in a new session from the exact verified baseline above.
