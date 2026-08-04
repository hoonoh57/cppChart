# cppChart Session Handoff

Read `ARCHITECTURE_CONSTITUTION.md`, `MODULARIZATION_PLAN.md`, then this file.

Repository: `hoonoh57/cppChart`
Branch: `p2/kiwoom-mock-gateway`
Local: `E:\2026\gpt\cpp\shell`
PR #1: Draft
Acceptance-fix implementation: `25179bd7ba329741e9aa46cb99e22260d50424a5`
Read-only verification workflow commit: `8ae847c9486110ee74d7b6901e6ecbae65487db0`
Windows CI #1063: `30878682349`
Artifact: `8880527526`
Digest: `sha256:e236ec2c0789a4a140b307fd59e6fd1d982a7f9f8a4dd42ac370b0613681a917`
Workflow: read-only

Always pull live HEAD before work:

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
git status
git rev-parse HEAD
```

M1-M6 are complete. M7 dynamic indicator calculation/management is stabilized. The
11-type catalog is SMA, EMA, JMA, Bollinger Bands, RSI, MACD, DMI, SuperTrend,
VWAP, OBV, and Wilder ADX.

## 2026-08-04 actual-screen acceptance failures and fixes

The user confirmed three real UI/runtime failures:

1. KOSPI/KOSDAQ history rendered in both the price overlay and lower pane, but the
   index comparison did not update in real time.
2. EMA, Bollinger Bands, RSI, MACD, DMI, and SuperTrend were implemented but were
   not visible in the top selector because that combo showed only active instances.
3. The output/style editor expanded vertically, placing Apply/Revert below the
   viewport and causing users to mistake the editor for incomplete or inactive.

Fixes now implemented:

- Kiwoom sector-index realtime is registered, removed, decoded, delivered, and
  restored after reconnect using `0J`. The previous `0I` contract was incorrect;
  `0I` is not the sector-index realtime type.
- Runtime tests now cover `0J` registration for KOSPI `001`, decimal payload decode,
  callback delivery, reconnect restoration, explicit removal, and prevention of
  removed-subscription resurrection.
- The top combo is labeled `적용 지표`. It shows current instances first and the
  complete 11-type catalog under `새 지표 추가`, so all available indicators are
  discoverable without opening an unexplained secondary dialog.
- `새 지표` opens the same catalog/pane insertion flow.
- The output line/histogram table has bounded vertical height and its own scrolling.
- The parameter/output/pane/reference editor is a scrolling child region while
  Apply/Revert and errors remain pinned at the bottom of the properties window.

## Comparison implementation

The `비교` editor supports arbitrary stock/index codes, KOSPI `001`, KOSDAQ `101`,
separate lower panes, price-pane secondary axes, styling, reload, hide/show, and
delete. Data paths are stock minutes + multiple `0B`, `ka20005` index minutes +
multiple `0J`, reconnect restoration, source-error isolation, and decimal/x100
index normalization. Rendering uses generic `axisId`, multiple left axes,
document-wide shared axis width, and completed-point cache reuse.

CI #1063 passed repository policy, architecture boundaries, explicit `0J` decoder
and lifecycle guards, indicator-catalog visibility and pinned-footer guards, MSVC
x64 build, the complete headless suite, clean-tree verification, and artifact
publication.

## Remaining actual-screen acceptance

Rebuild and verify only the three corrected paths first:

1. Add KOSPI `001` or KOSDAQ `101`, confirm the log reports `0J` subscription and
   confirm the latest comparison point changes without reload.
2. Open `적용 지표` and confirm all 11 types appear under `새 지표 추가`; add each
   of EMA/Bollinger/RSI/MACD/DMI/SuperTrend as needed.
3. Resize the properties dock and confirm Apply/Revert remain visible while only
   the editor body and bounded output table scroll.

Then complete stock/index overlay alignment, `0B`/`0J` live state preservation,
failed-source isolation, physical multi-source reconnect, and intraday soak.
Return screenshots/log evidence only for failed items. Next product work after
acceptance is code/name search and normalized relative-strength comparison. Keep PR
#1 Draft until actual account/order, physical reconnect, and soak acceptance close.
