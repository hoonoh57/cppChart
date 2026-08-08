# cppChart Architecture Constitution

## 1. Product objective

cppChart is a modular chart-based trading workbench. Users can connect normalized
real market/account data, manage reusable indicators, compare stocks and indices,
reuse one strategy implementation across replay/backtest/live, and inspect trading
results without editing the renderer.

## 2. Non-negotiable invariants

### 2.1 Real-data-only production

Production never replaces missing or invalid real data with synthetic prices,
bars, positions, fills, rankings, indicators, or comparison series. Dependent
capabilities fail closed and expose the exact fault. Fixtures remain test-only.

### 2.2 Major-feature modules and compact values

Only units with independent state, lifecycle, execution level, or failure policy
become modules: MarketData, ChartWorkspace, Indicators, Comparison, Strategy,
Trading, and Replay/Backtest. Tick, Bar, IndicatorValue, LinePoint, and Marker remain
compact values in contiguous storage.

### 2.3 One generic renderer contract

All visual features publish `RenderDocument` containing generic panes, value axes,
legends, candles, lines, histograms, markers, references, and annotations. Color,
width, line style, pane scale/weight, owner identity, and axis identity are generic
metadata.

The renderer never calculates or identifies indicators, comparisons, strategies,
brokers, symbols, or accounts. New features must not add name-based branches to
renderer source.

### 2.4 Indicator-instance boundary

`IndicatorSpec` is the pure calculation contract. Application-owned
`IndicatorInstanceDefinition` combines one spec with stable ID, visibility,
output/pane assignment, colors, widths, styles, and reference lines.

The application owns catalog insertion, duplication, hide/show, deletion,
parameter validation, and render-plan construction. Multiple instances of one type
may share a pane with independent parameters and presentation. A complete candidate
set validates before calculation/render plans are replaced.

Every indicator uses one implementation for batch history, same-timestamp live
replacement, replay, backtest, parameter sweep, and live monitoring.

### 2.5 Comparison-series boundary

`ComparisonDefinition` owns one real stock or index source plus placement and
presentation metadata. `ComparisonModule` owns source state, completed history,
live tail, revisions, errors, and execution level. `ComparisonRenderAdapter`
contributes only generic close lines, legends, panes, and value axes.

- stock history uses the normalized stock-minute path and stock live tail uses `0B`;
- index history uses the normalized index-minute path and index live tail uses `0J`;
- multiple stock and index subscriptions are independently restored after reconnect;
- one comparison failure does not replace or corrupt the primary market chart;
- comparisons may use a separate pane primary axis or a price-pane secondary axis;
- a secondary-axis line carries a generic `axisId`; renderer code never knows its source;
- decimal and already-scaled index values normalize to the same x100 integer domain;
- no elapsed-time resampling or fabricated point is introduced to force alignment.

### 2.6 Multi-axis and pane geometry

The right-side primary axis remains pane-owned. Generic secondary axes may occupy
left-side columns and have independent auto/fixed/symmetric ranges and precision.
The document-wide maximum left-axis width is reserved by every pane so candles,
volume, indicators, comparisons, crosshairs, and time labels retain one horizontal
bar-slot geometry.

Pane default weights come from the document; drag-resized weights belong to the
render surface. Separators expose a resize cursor/highlight, preserve adjacent total
weight, and enforce minimum visible height. Live revisions do not reset viewport,
selection, or pane sizes.

### 2.7 Legend and selection boundary

Legends are explicit metadata. Child outputs carry a generic `ownerId`; every
selectable legend resolves to one feature-owned instance. Renderer click/double-click
and highlighting are generic. Feature editors own configuration changes. Legend
hit-testing suppresses conflicting viewport gestures.

### 2.8 Command, snapshot, and broker boundaries

UI intent flows through CommandBus or explicit application APIs. Workers publish
normalized events. Modules publish immutable snapshots/render contributions.
Broker-specific payload fields end inside adapters; chart, indicator, comparison,
strategy, replay, and risk modules do not depend on transport JSON fields.

### 2.9 Fail closed

Malformed input, stale events, timestamp conflicts, sequence gaps, reconciliation
mismatch, unsupported data, invalid parameters, or invalid presentation metadata
disable only the dependent capability and preserve the last valid state where
appropriate.

### 2.10 Scoped UI state

Paired UI stack operations use one captured condition for begin/end. Callbacks must
not leave pointers or references alive across same-frame vector replacement.

### 2.11 Delivery-plan integrity and acceptance

An implementation plan approved by the user is an execution contract. Its items
must be tracked explicitly and may not be silently removed, reduced, reordered, or
declared complete in aggregate while approved items remain unfinished.

For user-facing work, the primary acceptance unit is the complete user path:

```text
input → candidates/state → selection → confirmation → action → visible result
```

A function, parser, cache, test, build, CI run, or artifact may prove an internal
contract but does not prove the user path is complete. Status terms are constrained:

- implementation in progress: the path is not connected;
- code implementation complete: the path is connected but not visually accepted;
- automated verification complete: focused/full tests and build passed;
- actual-screen acceptance complete: the approved user path was observed working.

The unqualified word `complete` is reserved for actual-screen acceptance when the
milestone is user-visible.

Full CI is a final integration gate, not the inner development loop. Focused source
changes and focused tests precede it. Identical failures may not be retried with the
same method more than twice; the third attempt must change the implementation or
verification method. Workflow write-back and temporary migration scripts are not
normal source-editing mechanisms.

## 3. Execution levels

Every major feature supports `Off`, `Standby`, `Visible`, and `Active`.

- Off stops subscriptions/calculation/rendering and releases optional caches.
- Standby preserves minimal state while stopping expensive work.
- Visible calculates and renders without execution behavior.
- Active permits execution subject to readiness and risk gates.

Instance visibility remains separate from feature execution level.

## 4. Performance policy

- integer prices/quantities and contiguous hot-path storage;
- no per-point ownership graph or virtual dispatch;
- bounded queues with explicit merge/drop accounting;
- immutable completed history plus mutable live tails;
- completed render-point cache reuse;
- visible-range-only rendering;
- explicit subscription limits and no unbounded comparison creation.

## 5. Source boundaries

```text
core/       normalized contracts and pure calculations
app/        feature modules, instance definitions, validation, render adapters
platform/   broker, transport, and operating-system adapters
render/     generic documents, builders, geometry, pure layout helpers
ui/         ImGui/D3D presentation and feature-owned editors
shell_main  process composition, device/window lifecycle, main loop
```

`core/` has no Win32, D3D, ImGui, or transport dependency. Feature-type switches
may exist in feature-owned catalogs/factories but not in the renderer or shell.

## 6. Verification and acceptance

A change is complete only when boundaries are documented, success/failure paths are
covered headlessly, Windows MSVC build succeeds, CI guards the invariant, temporary
migration logic and elevated workflow permissions are removed, the handoff records
exact verified state, and every approved user-visible path has actual-screen
acceptance. User testing is reserved for actual data, visual/GPU interaction,
physical reconnect, orders/fills, and long-running soak.
