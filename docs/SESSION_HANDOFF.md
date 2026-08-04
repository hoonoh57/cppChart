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

Pull and use live HEAD:

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
git status
git rev-parse HEAD
```

M1-M6 remain complete. M7 dynamic indicator management is user-accepted as highly
stabilized. Indicator catalog: SMA, EMA, JMA, Bollinger Bands, RSI, MACD, DMI,
SuperTrend, VWAP, OBV, and Wilder ADX.

The docked `비교` editor supports stock/index sources, arbitrary code/label,
KOSPI `001` and KOSDAQ `101`, separate lower pane or price-pane secondary axis,
style/divisor/precision/pane settings, and add/reload/hide/show/delete.

Data and rendering contracts:

- stock minutes plus multiple stock `0B` live tails;
- `ka20005` index minutes plus multiple `0I` live tails;
- reconnect restoration, explicit unsubscribe, isolated source errors;
- decimal/x100 index-value normalization;
- generic `axisId`, multiple left axes, and document-wide shared axis width;
- completed comparison render-point reuse.

Verified code: CI #1016 passed all policies, architecture gates, new indicator and
comparison tests, index normalization, dual-axis geometry, MSVC x64 build, complete
suite, clean-tree, and artifact publication.

Actual-screen acceptance: verify new indicators; KOSPI/KOSDAQ separate panes;
stock/index price overlays; pane/crosshair alignment; comparison style controls;
reload/hide/show/delete; `0B`/`0I` live state preservation; and failed-source
isolation. Return screenshots and visible logs only for failures.

Next after acceptance: code/name search and normalized relative-strength modes.
PR #1 remains Draft until order/account, physical and multi-source reconnect, and
intraday soak acceptance are complete.
