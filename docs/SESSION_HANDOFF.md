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
- verification-only CI configuration commit: `24cb554db13b3f8f61ca228a977aadd44641ac36`
- successful Windows CI run: `30807173805`
- artifact digest: `sha256:20a820a2082117f263aa837c879ec86e0f261b68270a0e9826e8000cf65892bb`

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

The user loaded real `005930` one-minute data. The response contained 900 real
`ka10080` rows, but absolute timestamp mapping created huge overnight/weekend
blank regions and compressed valid candles into clusters.

Correction:

- `OrdinalTimeAxis` maps each actual bar to one equal horizontal slot;
- overnight and session gaps consume no empty pixel span;
- actual timestamps remain available for labels, crosshair, tooltip, and date
  boundaries;
- the initial viewport opens a recent usable bar window.

### Finding 2 — price candles and volume bars used different widths

The price pane derived body width from its visible candle count. The volume pane
contains no candle series, so it fell back to one item and produced maximum-width
histograms. The volume bars therefore overlapped and appeared crushed.

Correction:

- `SeriesBodyWidth` derives one body width from plot width and the shared visible
  ordinal-axis span;
- candle and histogram panes use the same slot pitch and fill ratio;
- `series_geometry_tests` prevents pane-local width calculations from returning.

### Finding 3 — drag did not pan

The invisible chart surface captured only the left mouse button while panning
was implemented only for the right button. The intended drag path was therefore
unreliable and did not match normal chart interaction.

Correction:

- the surface explicitly captures left and right mouse buttons;
- either left-button or right-button horizontal drag pans the viewport;
- manual pan disables auto-follow until the viewport returns to the latest edge
  or the user double-clicks to reset.

### Finding 4 — horizontal crosshair was forced to candle close

The cursor Y value was initially calculated from the mouse, but the renderer
later replaced the horizontal line with the nearest candle close. This made the
line jump vertically and prevented subpanels from reporting their own values.

Correction:

- the horizontal line remains at the mouse-derived value in the hovered pane;
- the price pane snaps that value to its configured legal quotation ladder;
- the volume pane snaps to an integer volume value;
- future indicator panes may supply their own decimals or value grid;
- only the vertical time crosshair is synchronized across panes;
- the OHLCV tooltip still resolves the nearest candle independently.

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
- pane-local horizontal value crosshair and right-axis label
- KRX equity quotation-step snapping configured by the stock chart builder
- volume integer cursor value
- OHLCV and tick-count tooltip
- date and abnormal session-gap boundaries
- immutable completed history plus mutable live tail

## Verification result

Windows CI run `30807173805` passed:

- repository and secret-file policy
- core dependency boundary
- real-data-only production policy
- modular renderer boundary
- ordinal trading-time axis contract
- left/right drag interaction markers
- pane-aware `ValueGrid` contract
- price/volume shared body-width geometry
- Korean equity tick-boundary fixtures
- MSVC x64 `shell.exe` build
- complete headless test suite
- clean source-tree check
- executable artifact publication

## Current user-intervention point

A focused real-screen test is required before M6 is declared complete.

Validate:

1. candle and volume histogram bodies have identical horizontal width and center;
2. left-button drag pans horizontally;
3. right-button drag also pans horizontally;
4. wheel zoom remains functional;
5. double-click returns to the latest bars;
6. the price-pane horizontal crosshair follows mouse Y and snaps to the legal
   Korean equity quotation unit for that price band;
7. the volume-pane horizontal crosshair follows mouse Y and displays that
   panel's integer volume value;
8. the vertical crosshair remains aligned between price and volume panes;
9. the OHLCV tooltip remains tied to the nearest candle rather than the cursor
   value line;
10. real `0B` updates do not reset a manually panned viewport.

Do not proceed to M7 until these visual interactions are confirmed.

## Next remote milestone after acceptance

M7 reusable indicator engine:

- batch and incremental parity contract
- indicator registry and parameter serialization
- SMA, JMA, VWAP, OBV, and ADX
- standard Line/Histogram/ReferenceLine renderer contributions
- no renderer changes per added indicator
