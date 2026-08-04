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

Use live HEAD after pulling:

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
git status
git rev-parse HEAD
```

## Completed

M1-M6 are complete and M6 real-screen acceptance is closed. Real `ka10080`,
selected-symbol `0B`, account reconciliation, positions/PnL, ordinal chart,
crosshair, pan/zoom/latest reset, and viewport preservation remain verified.

M7 engine is complete for SMA, JMA, VWAP, OBV, and ADX with batch/incremental
parity, session date handling, live-tail cache reuse, execution levels, metrics,
and generic render contributions.

Dynamic indicator management is implemented:

- property-grid selector and Add dialog;
- default/price/new/existing pane insertion;
- duplicate with unique ID/color and preserved pane placement;
- independent same-type parameters and same-pane overlay;
- hide/show/delete;
- per-output visibility, pane, color, width, line style;
- pane height and axis-scale settings;
- reference CRUD and overbought/oversold creation;
- generic labels/selection and drag-resizable pane separators;
- viewport/selection/pane-size preservation across live updates;
- real market-only chart with no visible indicator;
- fixed Apply assertion and safe vector replacement.

CI #922 passed architecture/real-data gates, dynamic instance/shared-pane/color
coverage, style/reference/pane-layout tests, MSVC x64 build, full legacy/M7 suite,
clean-tree, and artifact publication.

## Actual-screen acceptance

```powershell
.\build.bat
.\shell.exe
```

Using actual `ka10080 + 0B`, confirm selector/Add, duplicate/same-pane overlay,
parameter and style Apply, hide/show/delete, reference CRUD, pane drag-resize,
live-update state preservation, and no assertions.

Return a screenshot only for visible failure. If normal, close M7 dynamic indicator
management and choose the next priority.

PR #1 remains Draft until actual order/account, physical reconnect, and intraday
soak acceptance are complete.
