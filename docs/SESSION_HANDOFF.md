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
- chart interaction implementation commit: `cb29d61c4b768a3c9e85a97434ed4c3e5f17d774`
- pane crosshair time-label implementation commit: `5436c1988f80365e8b91d78384bb6da3f5261e8a`
- verified cleanup source head: `e42c5a38fe994c043fd75a62efee8105e89f30d9`
- successful Windows CI run: `30820961609`
- artifact digest: `sha256:1c601f969e0d496e70d6793ea776bc2d6d318ecb82d287919528d4644f926011`

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
- Pane-specific cursor snapping is supplied as generic `ValueGrid` data by the
  document builder; the renderer does not contain a Korean-market branch.
- Candle and histogram body widths derive from one shared axis-slot geometry;
  a pane must never infer width from only the series types it contains.
- Crosshair time-label placement is generic renderer geometry and is clamped to
  the active pane. It must not depend on a candle or indicator type.
- `Off` stops subscriptions, calculation, rendering, and retained memory;
  `Standby` retains state but stops expensive work; `Visible` renders without
  strategy execution; `Active` enables the complete feature.
- Real errors remain visible and fail closed. Never restore synthetic data to
  make a screen look complete.
- Batch, replay, and real-time indicator/strategy logic must share the same
  calculation implementation.
- Financial bars use the ordinal trading-time coordinate axis. Wall-clock
  elapsed milliseconds must never be mapped directly to horizontal pixels.

## Completed modularization

- `FeatureRegistry` and feature metrics
- `MarketDataModule`
- `ChartWorkspaceModule`
- generic `RenderDocument`, builder, and ImGui renderer
- market-data and chart-workspace feature-level controls
- WebSocket `0B` unsubscribe and reconnect behavior
- actual `ka10080` plus `0B` selected-symbol chart path
- verification-only CI; one-shot migration files removed

## Local M6 acceptance findings and corrections

### Finding 1 — calendar gaps consumed X-axis width

The renderer originally mapped absolute elapsed time to pixels, creating huge
blank overnight and weekend regions. `OrdinalTimeAxis` now maps every real bar
to one equal slot while retaining real timestamps for labels and boundaries.

### Finding 2 — price candles and volume bars used different widths

The volume pane contained no candle series and calculated an unrelated maximum
body width. `SeriesBodyWidth` now derives candle and histogram width from the
same plot width and visible ordinal-axis span.

### Finding 3 — drag did not pan

The chart surface did not reliably capture the button used by the pan path.
Both left and right horizontal drag now pan the same viewport and disable
latest auto-follow until reset or return to the latest edge.

### Finding 4 — horizontal crosshair was forced to candle close

The renderer replaced the mouse Y value with the nearest candle close. The
horizontal line now remains at the mouse-derived value in the hovered pane.
The price pane snaps through its configured legal quotation ladder, while the
volume pane reports an integer volume value. Indicator panes can define their
own value grid and decimals without renderer changes.

### Finding 5 — crosshair timestamp had no visible floating label

The vertical line was synchronized across panes, but the selected timestamp was
only available inside the candle tooltip. Moving over a volume or future
indicator pane therefore showed the pane value but not the time.

Correction:

- every hovered pane draws a floating `MM/DD HH:mm` label at the vertical
  crosshair position;
- the bottom pane uses its reserved time-axis band;
- other panes place the label inside their lower edge;
- `PlaceCenteredHorizontalLabel` clamps the label within pane boundaries;
- the pane-local Y value label remains visible at the same time;
- `cursor_label_layout_tests` covers centered, left-edge, right-edge, and
  oversized label placement.

## Current chart foundation

- ordinal trading-time axis
- screen-sized initial recent window
- wheel zoom anchored at the mouse
- left or right horizontal drag pan
- double-click latest reset and auto-follow
- visible-range automatic value scaling
- aligned candle and histogram widths
- current-price line and label
- vertical timestamp crosshair synchronized across panes
- pane-local floating timestamp label
- pane-local horizontal value crosshair and right-axis label
- KRX equity quotation-step snapping configured by the stock chart builder
- volume integer cursor value
- OHLCV and tick-count tooltip
- date and abnormal session-gap boundaries
- immutable completed history plus mutable live tail

## Verification result

Windows CI run `30820961609` passed:

- repository and secret-file policy
- core dependency boundary
- real-data-only production policy
- modular renderer boundary
- ordinal trading-time axis contract
- left/right drag interaction markers
- pane-aware `ValueGrid` contract
- price/volume shared body-width geometry
- pane crosshair time-label geometry and edge clamping
- MSVC x64 `shell.exe` build
- complete headless test suite
- clean source-tree check
- executable artifact publication

## Current user-intervention point

A focused real-screen test is required before M6 is declared complete.

Validate:

1. price-pane hover shows both the quotation-step-adjusted price label and a
   floating timestamp label at the crosshair X position;
2. volume-pane hover shows both the integer volume label and the same timestamp;
3. the time label remains inside the pane near the left and right chart edges;
4. left and right drag still pan horizontally;
5. wheel zoom and double-click latest reset still work;
6. candle and volume bodies remain aligned;
7. real `0B` updates do not reset a manually panned viewport.

Do not proceed to M7 until these visual interactions are confirmed.

## Next remote milestone after acceptance

M7 reusable indicator engine:

- batch and incremental parity contract
- indicator registry and parameter serialization
- SMA, JMA, VWAP, OBV, and ADX
- standard Line/Histogram/ReferenceLine renderer contributions
- no renderer changes per added indicator
