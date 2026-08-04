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
- verified implementation: `1e0d0d7dcfd91de05435d69b954e75b573a93bad`
- Windows CI #1016: `30875659339`
- artifact: `8879511685`
- digest: `sha256:70c9f9e8d36a212d33da3c591121844ff05748170748b48b69fd8112130bf599`
- workflow: read-only
- production: real Kiwoom mock data only

Documentation commits follow the verified code. Pull and use live HEAD:

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
git status
git rev-parse HEAD
```

## Accepted baseline

M1-M6 remain complete. The user confirmed dynamic indicator management is highly
stabilized, so M7 acceptance is closed unless a new defect is reported.

## Indicator catalog — 11 types

SMA, EMA, JMA, Bollinger Bands, RSI, MACD, DMI, SuperTrend, VWAP, OBV, and Wilder
ADX. New types share batch/incremental calculation, same-timestamp replacement,
fail-closed validation, dynamic instances, styles, references, and pane placement.

## Comparison series — implemented

The docked `비교` editor supports stock or index/industry sources, arbitrary code
and label, KOSPI `001`/KOSDAQ `101` presets, separate lower pane or price-pane
secondary-axis placement, color/width/style/divisor/precision/pane settings, and
add/reload/hide/show/delete.

Data paths:

- stock history and multiple stock `0B` live tails;
- `ka20005` index history and multiple `0I` live tails;
- reconnect restoration and explicit unsubscribe;
- isolated source state/error/revision/cache;
- decimal and x100 index values normalized to one integer domain.

Rendering:

- separate pane primary right axis;
- price overlay independent left secondary axis through generic `axisId`;
- multiple left-axis columns;
- document-wide shared left-axis width preserving candle, volume, indicator,
  comparison, crosshair, and time-axis alignment;
- completed comparison render-point reuse on live-only updates.

## Verification

CI #1016 passed all policies and architecture gates, new indicator tests,
comparison lifecycle/live-tail/render tests, index normalization, dual-axis geometry,
MSVC x64 build, complete legacy/new suite, clean-tree, and artifact publication.

## Actual-screen acceptance

```powershell
.\build.bat
.\shell.exe
```

Using actual data, verify new indicators; KOSPI/KOSDAQ separate panes; stock/index
price overlays with left axes; common pane/crosshair alignment; comparison style
editing; reload/hide/show/delete; `0B`/`0I` live updates preserving viewport,
selection, and pane sizes; and isolation of a failed comparison request.

Return screenshots and the visible log line only for failed items. The next planned
slice after acceptance is code/name search and normalized relative-strength modes.

PR #1 remains Draft until actual order/account, physical and multi-source reconnect,
and intraday soak acceptance are complete.
