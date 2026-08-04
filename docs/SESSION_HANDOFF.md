# cppChart Session Handoff

## Mandatory first read

Read before changing code:

1. `docs/ARCHITECTURE_CONSTITUTION.md`
2. `docs/MODULARIZATION_PLAN.md`
3. this handoff

## Repository state

- repository: `hoonoh57/cppChart`
- local root: `E:\2026\gpt\cpp\shell`
- branch: `p2/kiwoom-mock-gateway`
- PR: `#1`, Draft
- protected `main`: `f1a7d8db7a5d1b145781bfcb6ce11c2e24ef6683`
- verified dynamic-indicator implementation HEAD: `dba98bb7bb4690aeee170b2c6e971f1fa5aa1bc1`
- successful Windows CI: `30868440522` (`#922`)
- artifact ID: `8877052838`
- artifact digest: `sha256:9f2e83f83f807b198a7a68c1d1743b4da857dfa69fccd9335e31f8a57c6976c7`
- workflow permissions: `contents: read`
- production policy: real Kiwoom mock data only; no synthetic fallback

Documentation commits follow the verified code baseline. Always pull and use the
live branch HEAD; do not reset to the implementation SHA.

## Opening commands

```powershell
Set-Location "E:\2026\gpt\cpp\shell"

git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
git status
git rev-parse HEAD
```

Do not alter `.env` unless a concrete configuration fault is shown. Never expose
keys, tokens, or account-sensitive payloads.

## Permanent invariants

- normalized real Kiwoom data only; dependent features fail closed;
- renderer consumes only generic `RenderDocument` metadata and standard series;
- indicator calculations never enter renderer or shell code;
- batch and incremental paths share one indicator implementation;
- completed history is immutable/shared; only the live tail is replaced;
- legend selection resolves to a feature-owned instance ID;
- dynamic instance lifecycle and presentation belong to application/UI modules;
- one complete candidate definition set is validated before publication;
- user viewport, selection, and pane sizes survive live-tail replacement;
- paired ImGui scopes use one captured condition;
- vector replacement must not leave same-frame pointers/references alive.

## Verified real-data and M1-M6 baseline

- OAuth, WebSocket login, account registration/reconciliation
- `ka10080` minute bars and selected-symbol `0B` registration/restoration
- same-minute live-tail OHLCV/tick-count replacement
- actual positions and PnL
- `MarketDataModule`, `ChartWorkspaceModule`, `FeatureRegistry`
- generic renderer, ordinal trading-time axis, common pane slot geometry
- synchronized crosshair, pan/zoom, latest reset, time/session boundaries
- actual `0B` updates preserve a manually panned viewport

M6 real-screen/GPU acceptance is complete.

## M7 calculation engine — complete

Implemented:

- SMA: Value
- JMA: Value / Up / Down / Slope
- VWAP: Value / Upper1 / Lower1 / Upper2 / Lower2
- OBV: Value / Signal / Direction
- ADX: Wilder ADX

Contracts:

- deterministic `IndicatorSpec` serialization
- maximum eight fixed outputs/readiness mask
- batch/incremental parity and same-timestamp live replacement
- invalid/descending input fail closed without partial output
- explicit `Bar.TradingDateYmd` and VWAP session reset
- execution levels, cache reuse, metrics, monotonic revisions
- generic line/histogram/reference/legend contributions

## Dynamic indicator management — implemented and CI verified

### Instance model

`app/indicator_configuration.*` defines `IndicatorInstanceDefinition`, combining
one calculation spec with instance visibility, outputs, pane placement, colors,
widths, styles, and reference lines.

Supported operations:

- select any configured instance from the property-grid top combo;
- add SMA/JMA/VWAP/OBV/ADX through the `지표 추가` dialog;
- choose default, price, new lower, or existing lower pane;
- duplicate an instance with a unique ID and distinguishable color variant;
- preserve the source pane placement when duplicating, enabling same-pane overlay;
- hide/show an instance; hidden instances stop calculation and rendering;
- permanently delete an instance and its presentation configuration;
- run with zero visible indicator instances using the real market-only chart.

### Parameters and presentation

The manager supports:

- calculation parameters for each indicator type;
- per-output visible/hidden state;
- per-output pane assignment;
- primary and histogram secondary colors;
- line width;
- solid, dashed, and dotted line style;
- pane default height weight;
- auto, fixed, or symmetric value scale;
- fixed minimum/maximum and decimal precision;
- create/edit/hide/delete arbitrary reference lines;
- quick overbought/oversold reference creation;
- Apply/Revert with complete validation before replacement.

### Pane interaction

The generic renderer now provides:

- separator hit areas between every adjacent pane;
- vertical resize cursor and highlighted separator on hover/drag;
- adjacent weight adjustment with minimum pane height;
- persistent user drag weights across live updates;
- adoption of an explicitly changed configured default weight;
- generic styled line/reference drawing and reference labels.

### Runtime hardening

- successful JMA parameter Apply no longer causes the ImGui `EndDisabled` assertion;
- duplicate/hide/delete controls snapshot selected state before replacing the vector;
- no same-frame dangling selection pointer remains after configuration replacement;
- temporary migration scripts are absent and workflow permissions are read-only.

## Verification

Windows CI `30868440522` (`#922`) passed:

- repository, temporary-file, and real-data-only policy
- core/module/workspace/dynamic-instance architecture gates
- generic renderer no-indicator-name gate
- instance catalog/add/hide/duplicate/shared-pane/color tests
- line-style/reference render-contract tests
- pane splitter minimum-height and weight-preservation tests
- property validation and existing indicator parity/cache tests
- MSVC x64 `shell.exe` build
- complete legacy and M7 headless suite
- clean-tree verification
- executable artifact publication

## Focused actual-screen acceptance

Run:

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
.\build.bat
.\shell.exe
```

Confirm these in one session using actual `ka10080 + 0B` data:

1. `프로퍼티` top `지표` combo lists all configured instances.
2. `지표 추가` creates a selected indicator in default/new/existing pane as chosen.
3. `복제` creates a second instance; change its parameter and color, then overlay it in the same lower pane.
4. `감추기/표시` removes/restores calculation and rendering; `삭제` removes it permanently.
5. Output color, line width, line style, and pane assignment update after Apply.
6. A reference/overbought/oversold line can be added, edited, hidden, and deleted.
7. Hovering a pane separator shows the vertical-resize cursor/highlight; dragging changes adjacent heights without collapsing either pane.
8. Live `0B` updates preserve viewport, selected instance, and dragged pane sizes.
9. No assertion dialog appears during Apply, duplicate, hide/show, delete, or reference editing.

Return a screenshot only when a visible result is wrong. If all items are normal,
record M7 dynamic indicator management as visually accepted and select the next
product priority.

## PR policy

PR #1 remains Draft until the remaining actual account/order, physical reconnect,
and intraday soak acceptance is complete.
