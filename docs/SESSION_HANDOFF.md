# cppChart Session Handoff

Read `ARCHITECTURE_CONSTITUTION.md`, `MODULARIZATION_PLAN.md`, then this file.

Repository: `hoonoh57/cppChart`
Branch: `p2/kiwoom-mock-gateway`
Local: `E:\2026\gpt\cpp\shell`
PR #1: Draft
Verified implementation: `1e0d0d7dcfd91de05435d69b954e75b573a93bad`
Windows CI #1016: `30875659339`
Artifact: `8879511685`
Digest: `sha256:70c9f9e8d36a212d33da3c591121844ff05748170748b48b69fd8112130bf599`
Workflow: read-only

Pull live HEAD before work. M1-M6 are complete. M7 dynamic indicator management is
user-accepted as highly stabilized. The 11-type catalog is SMA, EMA, JMA,
Bollinger Bands, RSI, MACD, DMI, SuperTrend, VWAP, OBV, and Wilder ADX.

The `비교` editor now supports arbitrary stock/index codes, KOSPI `001`, KOSDAQ
`101`, separate lower panes, price-pane secondary axes, styling, reload,
hide/show, and delete. Data paths are stock minutes + multiple `0B`, `ka20005`
index minutes + multiple `0I`, reconnect restoration, source-error isolation, and
decimal/x100 index normalization. Rendering uses generic `axisId`, multiple left
axes, document-wide shared axis width, and completed-point cache reuse.

CI #1016 passed policy/architecture gates, new indicator and comparison tests,
index normalization, dual-axis geometry, MSVC build, full suite, clean-tree, and
artifact publication.

Actual-screen acceptance remains for new indicators, KOSPI/KOSDAQ panes,
stock/index overlays, common pane/crosshair alignment, comparison controls,
`0B`/`0I` live preservation, and failed-source isolation. Return evidence only for
failed items. Next after acceptance: code/name search and normalized relative
strength. Keep PR Draft until order/account, physical multi-source reconnect, and
intraday soak acceptance are complete.
