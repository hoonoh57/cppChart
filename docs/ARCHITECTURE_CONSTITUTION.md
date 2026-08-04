# cppChart Architecture Constitution

## 1. Product objective

cppChart is a modular chart-based trading workbench. Users must be able to connect
normalized real market/account data, add and manage indicator instances, compare
symbols and indices, reuse one strategy implementation across replay/backtest/live,
and inspect trading results without changing the renderer.

## 2. Non-negotiable invariants

### 2.1 Real-data-only production

Production never replaces missing or invalid real data with synthetic prices,
bars, positions, fills, rankings, or indicator values. Dependent capabilities fail
closed and expose the exact fault. Deterministic fixtures remain test-only.

### 2.2 Major-feature modules and compact values

Only units with independent state, lifecycle, execution level, or failure policy
become modules: MarketData, ChartWorkspace, Indicators, Compare, Strategy, Trading,
and Replay/Backtest. Tick, Bar, IndicatorValue, render points, and markers remain
compact value types in contiguous storage.

### 2.3 One generic renderer contract

All visual features publish `RenderDocument` containing generic panes, legends,
candles, lines, histograms, markers, references, and annotations. Generic color,
width, line style, pane scale, pane weight, and owner identity are metadata.

The renderer never calculates or identifies indicators, strategies, brokers, or
account objects. A new indicator must not add a branch to renderer source.

### 2.4 Indicator-instance boundary

`IndicatorSpec` is the pure calculation contract. Application-owned
`IndicatorInstanceDefinition` combines one spec with:

- stable unique instance ID;
- visible/hidden state;
- output visibility and pane placement;
- colors, widths, and line styles;
- editable reference lines, including overbought/oversold levels.

The application owns the indicator catalog, insertion, duplication, hide/show,
deletion, parameter validation, and render-plan construction. Multiple instances
of the same type may share one pane while retaining independent parameters and
presentation. Hiding removes calculation and render contributions; deletion removes
the definition. A complete candidate set validates before active calculation or
render plans are replaced. Invalid or duplicate state never publishes partially.

### 2.5 Legend and selection boundary

Legends are explicit metadata. Child outputs and references carry a generic
`ownerId`; every selectable legend resolves to one feature-owned instance. A
multi-output indicator remains one selectable/editable object. The renderer emits
generic click/double-click events and highlighting but never edits parameters.
Legend hit-testing suppresses conflicting viewport gestures.

### 2.6 Pane layout boundary

Pane default weights come from the document. Drag-resized weights belong to the
render surface. Separators expose a vertical-resize cursor and highlighted hit
area. Adjacent resizing preserves total weight and minimum visible height. Live
market revisions do not reset a user's layout; an explicit configured default
height change may replace the prior default.

### 2.7 One calculation implementation

Batch history, incremental live updates, replay, backtest, parameter sweep, and
live monitoring/execution share one indicator or strategy implementation. Identical
normalized inputs must produce equivalent declared results.

### 2.8 Command, snapshot, and broker boundaries

UI intent flows through CommandBus or explicit application APIs. Workers publish
normalized events. Modules publish immutable snapshots/render contributions.
Broker-specific fields end inside the broker adapter; chart, indicator, strategy,
replay, and risk modules do not depend on transport payload fields.

### 2.9 Fail closed

Malformed input, stale events, time conflicts, sequence gaps, reconciliation
mismatch, unsupported data, and invalid parameter/presentation changes disable only
the dependent capability and preserve the last valid state where appropriate.

### 2.10 Scoped UI state

Paired UI stack operations use one captured condition for both begin and end.
Callbacks must not invalidate pointers or change the condition used by a later
matching end operation in the same frame.

## 3. Execution levels

Every major feature supports `Off`, `Standby`, `Visible`, and `Active`.

- Off stops subscriptions/calculation/rendering and releases optional caches.
- Standby preserves minimal state while stopping expensive work.
- Visible calculates and renders without execution behavior.
- Active permits execution subject to readiness and risk gates.

Instance visibility is separate from the Indicators feature execution level.

## 4. Performance policy

- integer prices/quantities and contiguous storage on hot paths;
- no per-event heap ownership graph or per-point virtual dispatch;
- bounded queues with explicit merge/drop accounting;
- immutable completed history plus a mutable live tail;
- visible-range-only rendering and shared completed render caches;
- viewport, selection, and pane-size state preserved across live-tail replacement.

## 5. Source boundaries

```text
core/       normalized contracts and pure calculations
app/        feature modules, indicator configuration, validation, coordination
platform/   broker, transport, and operating-system adapters
render/     generic documents, builders, geometry, pure layout helpers
ui/         ImGui/D3D presentation and feature-owned editors
shell_main  process composition, device/window lifecycle, main loop
```

`core/` has no Win32, D3D, ImGui, or transport dependency. Indicator-type switches
may exist in the feature-owned catalog/factory but not in the renderer or shell.

## 6. Verification and acceptance

A change is complete only when its boundary is documented, success/failure paths
are covered headlessly, Windows MSVC build succeeds, CI guards the new invariant,
temporary migration logic and elevated workflow permissions are removed, and the
handoff records the exact verified state. User testing is reserved for actual
market payloads, physical reconnection, visual/GPU interaction, order/fill behavior,
and long-running soak.
