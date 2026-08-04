# cppChart Session Handoff

## Mandatory first read

1. `docs/ARCHITECTURE_CONSTITUTION.md`
2. `docs/MODULARIZATION_PLAN.md`
3. this handoff

## Repository state

- repository: `hoonoh57/cppChart`
- local root: `E:\2026\gpt\cpp\shell`
- branch: `p2/kiwoom-mock-gateway`
- PR: `#1`, Draft
- protected `main`: `f1a7d8db7a5d1b145781bfcb6ce11c2e24ef6683`
- verified implementation HEAD: `dba98bb7bb4690aeee170b2c6e971f1fa5aa1bc1`
- successful Windows CI: `30868440522` (`#922`)
- artifact ID: `8877052838`
- artifact digest: `sha256:9f2e83f83f807b198a7a68c1d1743b4da857dfa69fccd9335e31f8a57c6976c7`
- workflow permission: `contents: read`
- production: real Kiwoom mock data only; no synthetic fallback

Documentation commits follow the verified code baseline. Pull and use the live
branch HEAD; do not reset to the implementation SHA.

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

- real normalized data only; dependent capabilities fail closed;
- renderer consumes generic `RenderDocument` only;
- indicator calculations and type switches never enter renderer or shell;
- batch/incremental/replay/backtest share one calculation implementation;
- completed history is immutable/shared; only the live tail is replaced;
- legend selection resolves to one feature-owned instance ID;
- dynamic lifecycle/presentation belong to application/UI modules;
- candidate definition sets validate completely before publication;
- viewport, selection, and pane sizes survive live-tail replacement;
- paired ImGui scopes use one captured condition;
- vector replacement does not leave same-frame pointers/references alive.

## Verified baseline

M1-M6 are complete, including real `ka10080`, selected-symbol `0B`, account
reconciliation, actual positions/PnL, generic renderer, ordinal axis, synchronized
crosshair, pan/zoom/latest reset, and manual viewport preservation. M6 real-screen
acceptance is closed.

M7 calculation engine is complete for SMA, JMA Value/Up/Down/Slope, VWAP bands,
OBV/Signal/Direction, and Wilder ADX. Trading-date propagation and session VWAP,
batch/incremental parity, live-tail cache reuse, execution levels, metrics, and
generic render contributions are verified.

## Dynamic indicator management — implemented

### Instance operations

- property-grid top combo selects any configured instance;
- `지표 추가` dialog inserts SMA/JMA/VWAP/OBV/ADX;
- insertion target: default, price, new lower, or existing lower pane;
- duplicate creates a unique ID and distinguishable color while preserving pane placement;
- same-type instances retain independent parameters and may overlay in one pane;
- hide/show stops/restores calculation and rendering;
- delete permanently removes the instance and presentation settings;
- zero visible indicators produces the real market-only chart.

### Parameter and presentation operations

- calculation parameters by indicator type;
- per-output visibility and pane assignment;
- primary/secondary colors;
- line width and solid/dashed/dotted style;
- pane default height and auto/fixed/symmetric value scale;
- fixed min/max and decimal precision;
- create/edit/hide/delete arbitrary reference lines;
- quick overbought/oversold reference creation;
- Apply/Revert with full validation before replacement.

### Generic pane interaction

- separator hit area between adjacent panes;
- vertical resize cursor and hover/drag highlight;
- adjacent weight adjustment with minimum pane height;
- user resize state retained across live updates;
- explicit configured default-height changes adopted without minute-by-minute reset;
- generic styled lines/references and reference labels.

### Runtime hardening

- JMA Apply no longer triggers the ImGui `EndDisabled` assertion;
- selected instance state is copied before duplicate/hide/delete replaces the vector;
- no same-frame dangling selection pointer remains;
- temporary migration scripts are absent and CI is read-only.

## Verification

Windows CI `30868440522` (`#922`) passed repository/real-data policy,
core/module/workspace/dynamic-instance gates, generic renderer isolation,
instance hide/duplicate/shared-pane/color tests, style/reference/pane-layout tests,
MSVC x64 build, full legacy and M7 headless suite, clean-tree, and artifact
publication.

## Focused actual-screen acceptance

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
.\build.bat
.\shell.exe
```

Using actual `ka10080 + 0B` data, confirm:

1. top `지표` combo lists configured instances;
2. `지표 추가` creates an instance in the selected pane;
3. `복제` creates a second instance, which can use different parameters/colors in the same lower pane;
4. hide/show and delete work;
5. output color, width, style, and pane assignment apply;
6. reference/overbought/oversold lines can be added, edited, hidden, and deleted;
7. separator hover shows resize cursor/highlight and drag changes adjacent heights without collapse;
8. live `0B` preserves viewport, selected instance, and dragged pane sizes;
9. no assertion dialog occurs during any management action.

Return a screenshot only if a visible result is wrong. If all are normal, close M7
dynamic indicator management and select the next product priority.

## PR policy

PR #1 remains Draft until actual account/order, physical reconnect, and intraday
soak acceptance are complete.
