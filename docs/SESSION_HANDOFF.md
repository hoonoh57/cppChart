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
- verified implementation: `dba98bb7bb4690aeee170b2c6e971f1fa5aa1bc1`
- Windows CI: `30868440522` (`#922`)
- artifact: `8877052838`
- digest: `sha256:9f2e83f83f807b198a7a68c1d1743b4da857dfa69fccd9335e31f8a57c6976c7`
- workflow: read-only
- production: real Kiwoom mock data only; no synthetic fallback

Use the live branch HEAD after pulling; documentation commits follow the verified
implementation baseline.

## Opening commands

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
git status
git rev-parse HEAD
```

Do not alter `.env` without a concrete configuration fault. Never expose keys,
tokens, or account-sensitive payloads.

## Permanent invariants

- normalized real data only; dependent features fail closed;
- generic renderer, with no indicator/broker/strategy calculation branches;
- one calculation implementation for batch/incremental/replay/backtest;
- immutable completed history plus mutable live tail;
- owner-based instance selection;
- lifecycle/presentation in app/UI modules;
- complete candidate validation before publication;
- viewport, selection, and pane sizes survive live updates;
- stable ImGui scope conditions and no dangling same-frame pointers.

## Completed baseline

M1-M6 are complete and M6 is visually accepted. Real `ka10080`, selected-symbol
`0B`, account reconciliation, positions/PnL, generic renderer, ordinal axis,
crosshair, pan/zoom/latest reset, and viewport preservation remain intact.

M7 calculation is complete for SMA, JMA Value/Up/Down/Slope, VWAP bands,
OBV/Signal/Direction, and Wilder ADX, including session dates, parity, cache reuse,
execution levels, metrics, and generic render contributions.

## Dynamic indicator management — implemented

- top selector and Add dialog;
- default/price/new/existing pane insertion;
- duplicate with unique ID/color and preserved pane placement;
- independent same-type parameters and same-pane overlay;
- hide/show and permanent delete;
- per-output visibility, pane, color, width, and style;
- pane height and axis-scale settings;
- reference CRUD and overbought/oversold creation;
- generic styled lines/reference labels;
- drag-resizable separators with cursor/highlight/minimum height;
- persistent pane weights and configured-default synchronization;
- market-only chart when no indicator is visible;
- JMA Apply assertion fixed;
- vector replacement uses copied selection state;
- temporary migration logic removed and CI read-only.

## Verification

CI #922 passed repository/real-data policy, architecture isolation, dynamic
instance lifecycle/shared-pane/color tests, style/reference/pane-layout tests,
MSVC x64 build, full legacy/M7 suite, clean-tree, and artifact publication.

## Focused actual-screen acceptance

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
.\build.bat
.\shell.exe
```

Using actual `ka10080 + 0B`, confirm:

1. top selector and Add dialog work;
2. duplicate creates a differently colored instance that can share a lower pane;
3. parameters, color, width, style, and pane assignment apply;
4. hide/show and delete work;
5. references/overbought/oversold can be created, edited, hidden, deleted;
6. separator hover/drag works without pane collapse;
7. live updates retain viewport, selection, and pane sizes;
8. no assertion appears during management actions.

Return a screenshot only for a visible failure. If normal, close M7 dynamic
indicator management and choose the next priority.

## PR policy

PR #1 remains Draft until actual account/order, physical reconnect, and intraday
soak acceptance are complete.
