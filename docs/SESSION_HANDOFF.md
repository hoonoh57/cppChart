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
- workflow: `contents: read`
- production: real Kiwoom mock data only; no synthetic fallback

Documentation commits follow the verified implementation. Pull and use live HEAD:

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
git status
git rev-parse HEAD
```

Do not alter `.env` without a concrete fault. Never expose credentials, tokens, or
account-sensitive payloads.

## Completed baseline

M1-M6 remain complete and M6 visual acceptance is closed. Real stock minute data,
selected-symbol `0B`, account reconciliation, positions/PnL, generic renderer,
ordinal axis, crosshair, pan/zoom/latest reset, pane resize, and live viewport
preservation remain verified.

The user confirmed the dynamic indicator-management functionality is highly
stabilized. Record M7 management acceptance as complete unless a new visual defect
is reported.

## Indicator catalog — 11 types

- SMA
- EMA
- JMA Value / Up / Down / Slope
- Bollinger Bands
- RSI with editable overbought/oversold lines
- MACD / Signal / Histogram
- DMI +DI / -DI / ADX
- SuperTrend
- VWAP bands
- OBV / Signal / Direction
- Wilder ADX

All new indicators use the existing batch/incremental, same-timestamp replacement,
fail-closed, dynamic instance, style, reference, and pane contracts.

## Comparison series — first production slice

### UI

A docked `비교` editor provides:

- stock or index/industry source type;
- arbitrary code and display name;
- KOSPI `001` and KOSDAQ `101` presets;
- `별도 하단 패널` or `가격 패널 이중축` placement;
- color, width, solid/dashed/dotted style;
- value divisor, decimal precision, pane ID/title/height;
- add/reload/hide/show/delete;
- legend selection and double-click editor focus.

### Data

- stock history: normalized stock-minute request path;
- index history: `ka20005` index-minute request path;
- stock realtime: multiple `0B` subscriptions;
- index realtime: multiple `0I` subscriptions;
- reconnect restoration and explicit unsubscribe;
- independent comparison state/error/revision/cache;
- decimal and already-scaled index values normalized to x100 integers;
- comparison failure does not replace the primary real chart.

### Rendering

- separate lower pane uses its primary right axis;
- price overlay uses an independent left secondary axis;
- each comparison line carries a generic `axisId`;
- multiple left-axis columns are supported;
- every pane reserves the document-wide maximum left-axis width, keeping candle,
  volume, indicator, comparison, crosshair, and time-axis geometry aligned;
- completed comparison render points are reused on live-only updates.

## Verification

CI #1016 passed:

- repository, real-data-only, core/module/renderer boundaries;
- generic renderer no feature-name branches;
- EMA/BB/RSI/MACD/DMI/SuperTrend readiness and replacement tests;
- comparison definition/lifecycle/style/data-preservation tests;
- stock `0B` and index `0I` live-minute replacement tests;
- separate-pane and price-secondary-axis render tests;
- decimal/x100 index normalization tests;
- shared dual-axis geometry gates;
- MSVC x64 `shell.exe` build;
- complete legacy and new headless suite;
- clean-tree and executable artifact publication.

## Focused actual-screen acceptance

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
.\build.bat
.\shell.exe
```

Using actual market data, verify:

1. add EMA, Bollinger, RSI, MACD, DMI, and SuperTrend from the indicator dialog;
2. add KOSPI and KOSDAQ in separate lower panes;
3. add one stock and one index to the price pane with left secondary axes;
4. confirm all pane bar slots and crosshair time coordinates remain aligned;
5. edit comparison color/width/style/divisor/decimals and Apply;
6. reload, hide/show, and delete comparisons;
7. confirm `0B` stock and `0I` index live tails update without resetting viewport,
   selection, or pane sizes;
8. confirm one failed comparison request leaves the primary chart and other series normal.

Return screenshots and the visible error/log line only for failed items. After this
focused acceptance, continue with code/name search and normalized relative-strength
comparison.

## PR policy

PR #1 remains Draft until actual order/account, physical reconnect, multi-source
reconnect restoration, and intraday soak acceptance are complete.
