# cppChart Session Handoff

## Read first

1. `docs/ARCHITECTURE_CONSTITUTION.md`
2. `docs/MODULARIZATION_PLAN.md`
3. this file

## Repository

- repo: `hoonoh57/cppChart`
- local: `E:\2026\gpt\cpp\shell`
- branch: `p2/kiwoom-mock-gateway`
- PR #1: Draft
- verified implementation: `dba98bb7bb4690aeee170b2c6e971f1fa5aa1bc1`
- Windows CI #922: `30868440522`
- artifact: `8877052838`
- digest: `sha256:9f2e83f83f807b198a7a68c1d1743b4da857dfa69fccd9335e31f8a57c6976c7`
- workflow: read-only
- production: real Kiwoom mock data only

Pull and use live HEAD:

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
git status
git rev-parse HEAD
```

## Completed

M1-M6 are complete; M6 real-screen acceptance is closed. Existing real `ka10080`,
selected-symbol `0B`, account reconciliation, actual positions/PnL, ordinal chart,
crosshair, pan/zoom, latest reset, and viewport preservation remain verified.

M7 engine is complete for SMA, JMA, VWAP, OBV, and ADX with batch/incremental
parity, session date handling, live-tail cache reuse, execution levels, metrics,
and generic render contributions.

Dynamic indicator management is implemented:

- property-grid top selector and Add dialog;
- default/price/new/existing pane insertion;
- duplicate with unique ID/color and preserved pane placement;
- independent same-type parameters and same-pane overlay;
- hide/show and permanent delete;
- per-output visibility, pane, color, width, solid/dashed/dotted style;
- pane height and auto/fixed/symmetric scale settings;
- reference CRUD and overbought/oversold quick creation;
- generic reference labels and owner-based selection;
- drag-resizable pane separators with cursor/highlight/minimum height;
- pane/viewport/selection preservation across live updates;
- real market-only chart when no indicator is visible;
- fixed ImGui Apply assertion and safe vector replacement.

CI #922 passed architecture/real-data gates, dynamic instance/shared-pane/color
coverage, style/reference/pane-layout tests, MSVC x64 build, full legacy/M7 suite,
clean-tree, and artifact publication.

## Actual-screen acceptance

```powershell
.\build.bat
.\shell.exe
```

Using actual `ka10080 + 0B`, confirm:

1. selector and Add dialog;
2. duplicate and same-lower-pane overlay with different parameter/color;
3. parameter/color/width/style/pane Apply;
4. hide/show/delete;
5. reference and overbought/oversold CRUD;
6. pane separator resize cursor/highlight/drag without collapse;
7. live-update preservation of viewport, selection, and pane sizes;
8. no assertion during management actions.

Return a screenshot only for a visible failure. If normal, close M7 dynamic
indicator management and choose the next priority.

PR #1 remains Draft until actual order/account, physical reconnect, and intraday
soak acceptance are complete.
