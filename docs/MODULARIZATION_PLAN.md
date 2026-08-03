# cppChart Modularization and Performance Plan

## Goal

Preserve the verified real Kiwoom minute-bar, `0B`, account, and order paths while converting the shell from a growing monolith into detachable major feature modules joined to a slim generic renderer contract.

The plan deliberately avoids an object-per-value design and does not introduce a DLL plugin ABI yet.

## Baseline to preserve

The following behavior is already a verified baseline and must remain unchanged throughout refactoring:

- `TRADING_MODE=KIWOOM_MOCK` fail-closed configuration;
- OAuth, WebSocket login, account-event registration, reconciliation, and order readiness;
- `ka10080` real stock minute bars;
- selected-symbol `0B` subscription with `refresh=1`;
- live current-bar OHLCV and tick-count update;
- actual account position and PnL display;
- no synthetic production data;
- emergency/position liquidation gated by broker readiness;
- Windows MSVC build and all headless tests.

## Milestone M1 — architecture contracts and regression gates

Deliverables:

- `ARCHITECTURE_CONSTITUTION.md`;
- feature execution levels and registry;
- generic renderer document contract;
- module performance metrics;
- CI checks preventing forbidden dependencies and central feature-specific renderer branches;
- headless tests for level transitions, dependencies, metrics, and render contracts.

Acceptance:

- no user-visible behavior change;
- all existing tests pass;
- new contracts compile without Win32, ImGui, D3D, or Kiwoom dependencies.

## Milestone M2 — extract MarketDataModule

Move from `shell_main.cpp` into a major application module:

- request state;
- current selected code and minute unit;
- REST page application;
- `0B` tick merge;
- latest quote access;
- continuation state;
- subscription status;
- event and timing metrics;
- visible-range bar copy.

The module exposes snapshots and command/event methods. It does not render and does not know ImGui/D3D.

Acceptance:

- `shell_main.cpp` no longer owns market-data mutexes, bars, tick counters, or tick-to-bar merge logic;
- existing real-data tests remain valid;
- new tests cover state transitions and thread-safe snapshots;
- visible bar copying is bounded to the requested range instead of copying all history each frame.

## Milestone M3 — extract generic chart renderer

Create:

- broker-independent `RenderDocument`;
- price and volume panes;
- candle and histogram series;
- generic ImGui renderer consuming only that contract;
- renderer state containing viewport, dirty revision, and interaction state.

Acceptance:

- renderer source contains no Kiwoom API IDs, indicator names, strategy names, or account concepts;
- current stock chart looks and updates as before;
- a fixture document renders without a live broker;
- document builder and renderer are separately testable where possible.

## Milestone M4 — FeatureRegistry and execution-level controls

Register initial major features:

- market data;
- chart workspace;
- trading/account;
- diagnostics.

Add Off/Standby/Visible/Active controls and dependency validation.

Acceptance:

- Off market data blocks subscriptions and requests;
- hidden chart workspace does not rebuild render documents;
- trading/account remains independently available for liquidation when chart data is Off;
- feature metrics are visible in diagnostics.

## Milestone M5 — application coordinator and thin shell

Move command routing and runtime callbacks into an application coordinator.

`shell_main.cpp` retains only:

- Win32/D3D lifecycle;
- ImGui frame lifecycle;
- top-level composition;
- device-loss handling;
- calls to application/UI objects.

Acceptance:

- no market, strategy, order, or chart-domain mutation remains in `shell_main.cpp`;
- command routing is headless-testable;
- shell process startup and shutdown remain deterministic.

## Milestone M6 — chart viewport foundation

Implemented on the generic renderer contract:

- full verified history remains available to the viewport;
- visible time range;
- wheel zoom anchored at the mouse position;
- right-button horizontal pan;
- double-click reset and latest-bar auto-follow;
- visible-range automatic value scale;
- current-price line and label;
- synchronized vertical crosshair across all panes in the same frame;
- OHLCV and tick-count tooltip;
- time and value axes;
- headless viewport state tests registered in the complete test suite.

Remaining before the local visual acceptance request:

- session/date boundary rendering;
- immutable completed-history plus mutable live-tail sharing so `0B` does not rebuild or copy the complete history on every tick;
- final Windows CI verification after the live-tail split;
- one focused visual/GPU test covering zoom, pan, crosshair, latest-bar follow, feature levels, and real `0B` updates.

## Milestone M7 — reusable indicator engine

Create one batch/incremental engine and registry. Initial indicators:

- SMA;
- JMA;
- VWAP;
- OBV;
- ADX.

Acceptance:

- batch and incremental parity fixtures;
- indicators publish standard Line/Histogram/ReferenceLine series;
- renderer code does not change when indicators are added or removed;
- feature execution level controls data acquisition, calculation, and rendering work.

## Milestone M8 — index and multi-symbol comparison

Connect:

- `ka20005` index minute bars;
- `0I` real-time index values;
- synchronized symbol/index time axes;
- normalized return, relative strength, beta, and correlation series;
- multiple chart workspaces sharing source data.

Acceptance:

- missing index data does not corrupt stock data;
- comparison can be toggled Off without changing the main chart;
- multi-symbol load and subscription limits are measured and enforced.

## Milestone M9 — strategy/replay/trade-result integration

Use the same indicator and strategy code for replay, backtest, and real-time decisions. Publish:

- signals;
- order markers;
- fills;
- average-price lines;
- stop/target lines;
- per-symbol and portfolio results.

Acceptance:

- deterministic replay equals batch backtest under the same event stream;
- live events use the same strategy evaluator;
- chart result visualization is a renderer contribution, not renderer-specific strategy code.

## Continuous verification

Every milestone runs:

- core dependency boundary checks;
- real-data-only production checks;
- architecture regression checks;
- MSVC x64 shell build;
- full headless suite;
- clean working-tree check;
- executable artifact upload.

## User test policy

Do not request a local pull merely because files changed. Request user testing only when the current milestone depends on visual interaction, actual Kiwoom payloads, physical reconnection, order/fill behavior, or soak performance.
