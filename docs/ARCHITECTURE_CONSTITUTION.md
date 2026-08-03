# cppChart Architecture Constitution

## 1. Product objective

cppChart is not a clone of a fixed HTS chart. The product must let a user:

1. connect normalized real market and account data;
2. create arbitrary indicators without editing the renderer;
3. compare indices, sectors, and many stocks on synchronized charts;
4. run the same strategy code in replay, backtest, monitoring, and live execution;
5. inspect multi-symbol trade results directly on charts;
6. save validated parameter sets and reuse them without translation.

The user defines the analytical and trading result. The implementation must absorb protocol, concurrency, rendering, storage, performance, and recovery complexity without exposing accidental technical work to the user.

## 2. Non-negotiable invariants

### 2.1 Real-data-only production runtime

Production code must never hide a real data error with synthetic prices, positions, fills, rankings, or fallback candles. Missing or invalid data produces an explicit state and blocks dependent actions.

Deterministic fixtures are allowed only in tests and cannot enter production snapshots.

### 2.2 Major-feature modularity, not object-per-value design

Only units with independent state, lifetime, execution level, or failure policy become feature modules.

Examples:

- MarketData
- ChartWorkspace
- Indicators
- Compare
- Strategy
- Trading
- Replay/Backtest

High-frequency values remain compact value types in contiguous storage:

- Tick
- Bar
- PricePoint
- IndicatorValue
- DrawPoint
- Marker

No heap allocation, virtual dispatch, or ownership graph is introduced per tick, bar, or rendered point.

### 2.3 One renderer contract

Feature modules never draw directly and the renderer never knows indicator, strategy, broker, or API names.

Every visual feature publishes the same renderer-facing contract:

```text
RenderDocument
  Pane[]
    Axis[]
    CandleSeries[]
    LineSeries[]
    HistogramSeries[]
    MarkerSeries[]
    ReferenceLine[]
    TextAnnotation[]
```

A new indicator or strategy contributes standard series. It must not add feature-specific branches to the renderer.

### 2.4 Slim renderer hot path

The renderer is responsible only for:

- pane layout;
- time and value axes;
- visible-range calculation;
- clipping and downsampling;
- GPU buffer and off-screen render-target management;
- dirty-region decisions;
- interaction such as zoom, pan, crosshair, and synchronized cursors;
- rendering standard series types.

The renderer must not:

- parse Kiwoom JSON;
- aggregate `0B` ticks into bars;
- calculate SMA, JMA, VWAP, RSI, or strategy logic;
- calculate positions or PnL;
- submit orders;
- rank symbols.

The render loop must not allocate per point, wait for network locks, or recalculate indicators.

### 2.5 Command and snapshot discipline

User actions flow through CommandBus. Worker threads publish normalized events. UI state changes occur through explicit module APIs. The UI renders snapshots and does not directly mutate broker, strategy, or market-data state.

```text
UI intent -> CommandBus -> feature/application coordinator
worker event -> normalized event -> feature module
feature module -> snapshot/render contribution -> UI renderer
```

### 2.6 One calculation implementation

An indicator or strategy has one implementation shared by:

- historical batch calculation;
- live incremental update;
- replay;
- backtest;
- parameter sweep;
- real-time monitoring and execution.

Given identical normalized input, batch and incremental results must be equivalent within the declared numerical contract.

### 2.7 Normalized broker boundary

Broker-specific fields end inside the broker adapter.

```text
Kiwoom REST/WebSocket -> Kiwoom adapter -> normalized Tick/Bar/Order/Fill/Position
```

Chart, indicator, strategy, replay, and risk modules do not depend on Kiwoom JSON keys or HTTP/WebSocket details.

### 2.8 Fail closed

Missing configuration, authentication failure, malformed response, stale event, timestamp conflict, sequence gap, reconciliation mismatch, or unsupported data disables only the dependent capability and reports the exact fault. It never invents substitute state.

Emergency liquidation remains available when the broker account is reconciled even if chart data is unavailable.

## 3. Feature execution levels

Every major feature supports an explicit execution level.

### Off

- no subscription;
- no calculation;
- no rendering;
- release optional caches and GPU resources.

### Standby

- preserve minimal state and cache;
- stop expensive calculations and rendering;
- remain ready for fast activation.

### Visible

- acquire data required for display;
- calculate and render;
- do not execute strategies or orders.

### Active

- acquire data;
- calculate;
- render;
- allow the module's execution behavior, subject to risk and readiness gates.

Hiding a window is not equivalent to disabling a feature. Off must stop its upstream work.

## 4. Performance policy

### 4.1 Hot-path data

- integer won prices and integer quantities;
- contiguous vectors, ring buffers, or chunked stores;
- no per-event `new/delete`;
- no per-point virtual calls;
- bounded queues and explicit overflow policy;
- incremental updates for the live bar and dependent indicator window.

### 4.2 Rendering

- render only visible ranges;
- share market series across charts;
- rebuild GPU buffers only when the series revision or viewport changes;
- separate static history from the mutable live tail;
- render off-screen charts only when dirty;
- background or hidden workspaces use reduced or Off execution levels.

### 4.3 Backpressure

When event rate exceeds UI refresh capacity, normalized events may be coalesced only under an explicit semantic rule. Order, fill, balance, and fault events are never silently dropped. Quote/tick coalescing records the number of merged events.

## 5. Feature registry contract

The application owns a registry of major features. Each registered feature has:

- stable ID;
- display name;
- dependencies;
- current execution level;
- readiness and health state;
- performance metrics;
- optional renderer contribution capability.

Initial implementation is statically linked. A binary DLL ABI is deferred until the contract is stable. Runtime attach/detach means registering, changing execution level, and removing contributions without changing renderer source.

## 6. Required performance metrics

Each major feature reports at least:

- latest processing time;
- maximum processing time;
- event count;
- queue depth;
- merged/dropped event count;
- approximate retained bytes;
- calculated symbol count;
- renderer series count;
- last error and readiness.

Optimization decisions must use measurements rather than guessed bottlenecks.

## 7. Source-code boundaries

Recommended top-level responsibilities:

```text
core/       normalized domain contracts and pure calculations
app/        major feature modules and application coordination
platform/   Kiwoom, WinHTTP, operating-system adapters
render/     broker-independent RenderDocument and builders
ui/         ImGui/D3D presentation and interaction
shell_main  process composition, window/device lifecycle, main loop only
```

`core/` must not include Win32, D3D, ImGui, or WinHTTP.

## 8. Complexity controls

A feature must be split before more functionality is added when any of these are true:

- one file owns UI, network, calculation, and state mutation;
- a small change requires editing unrelated sections of a large file;
- a function implements multiple workflow stages;
- testing requires a real D3D window or live network unnecessarily;
- the same validation or transformation appears twice;
- adding a new indicator or symbol requires editing central switches;
- a module's responsibility cannot be stated in one sentence.

File size is a warning, not the sole rule. A split is valid only when ownership, input, output, and tests become clearer.

## 9. Change acceptance rules

A change is not complete until:

1. production and test boundaries are explicit;
2. the relevant module contract is documented;
3. headless tests cover success and failure paths;
4. Windows MSVC build succeeds;
5. CI prevents regression of the new invariant;
6. one-shot migration scripts and temporary workflows are removed;
7. the session handoff records exact HEAD, completed behavior, remaining faults, and next order.

## 10. User-intervention rule

Remote implementation, refactoring, fixtures, CI, and static verification continue without asking the user to perform repetitive local checks.

The user is requested to test only when a result depends on something unavailable remotely, such as:

- real Kiwoom credentials;
- actual intraday REST/WebSocket payloads;
- physical network disconnection and reconnection;
- visual interaction or GPU behavior;
- real-account/mock-account order and fill behavior;
- long-running intraday soak.

At that point the request must be a minimal, exact acceptance procedure with the expected visible result and the evidence to return on failure.
