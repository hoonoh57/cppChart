from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text("\ufeff" + text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


shell = read("shell_main.cpp")
old = '''    const ImVec2 available = ImGui::GetContentRegionAvail();
    const std::size_t visibleLimit = snapshot.barCount;

    const double started = NowSeconds();
    if (g_chartWorkspaceModule.NeedsUpdate(
            snapshot.revision,
            visibleLimit))
    {
        const std::vector<trading::Bar> visibleBars =
            g_marketDataModule.CopyVisibleBars(visibleLimit);
        std::string chartError;
        if (!g_chartWorkspaceModule.UpdateMarketChart(
                "main-market-chart",
                snapshot.code,
                snapshot.code,
                visibleBars,
                snapshot.revision,
                visibleLimit,
                chartError))
'''
new = '''    const ImVec2 available = ImGui::GetContentRegionAvail();
    const trading::app::MarketDataSeriesSnapshot marketSeries =
        g_marketDataModule.SeriesSnapshot();

    const double started = NowSeconds();
    if (g_chartWorkspaceModule.NeedsUpdate(marketSeries.revision))
    {
        trading::app::ChartMarketSource chartSource;
        chartSource.completedBars = marketSeries.completedBars;
        chartSource.liveBar = marketSeries.liveBar;
        chartSource.hasLiveBar = marketSeries.hasLiveBar;
        chartSource.barCount = marketSeries.barCount;
        chartSource.revision = marketSeries.revision;
        chartSource.completedRevision = marketSeries.completedRevision;
        chartSource.liveRevision = marketSeries.liveRevision;

        std::string chartError;
        if (!g_chartWorkspaceModule.UpdateMarketChart(
                "main-market-chart",
                snapshot.code,
                snapshot.code,
                chartSource,
                chartError))
'''
shell = replace_once(shell, old, new, "shell live-tail chart source")
write("shell_main.cpp", shell)

verify = read("scripts/verify_modular_architecture.ps1")
old_forbidden = '''    'static trading::render::RenderDocument g_mainRenderDocument'
)'''
new_forbidden = '''    'static trading::render::RenderDocument g_mainRenderDocument',
    'CopyVisibleBars(visibleLimit)',
    'const std::vector<trading::Bar> visibleBars'
)'''
verify = replace_once(
    verify,
    old_forbidden,
    new_forbidden,
    "forbid full history copying in shell",
)

contract_anchor = '''foreach ($marker in $forbiddenContractMarkers) {
    if ($renderContract.ToLowerInvariant().Contains($marker)) {
        throw "Render contract contains platform or broker dependency: $marker"
    }
}

Write-Host 'Major-feature modules and generic renderer boundary verified.'
'''
contract_new = '''foreach ($marker in $forbiddenContractMarkers) {
    if ($renderContract.ToLowerInvariant().Contains($marker)) {
        throw "Render contract contains platform or broker dependency: $marker"
    }
}

$requiredSharedTailMarkers = @(
    'class SharedTailSeries final',
    'SetShared(',
    'SharedPrefix()',
    'HasLiveTail()'
)
foreach ($marker in $requiredSharedTailMarkers) {
    if (-not $renderContract.Contains($marker)) {
        throw "Shared immutable history/live-tail render contract is missing: $marker"
    }
}

$marketModule = Get-Content '.\\app\\market_data_module.cpp' -Raw
$requiredMarketMarkers = @(
    'completedBars_',
    'liveBar_',
    'completedRevision_',
    'SeriesSnapshot()',
    'liveWindow.reserve(2)'
)
foreach ($marker in $requiredMarketMarkers) {
    if (-not $marketModule.Contains($marker)) {
        throw "MarketDataModule live-tail split is missing: $marker"
    }
}

Write-Host 'Major-feature modules, generic renderer, and immutable-history live-tail boundary verified.'
'''
verify = replace_once(
    verify,
    contract_anchor,
    contract_new,
    "shared-tail architecture verification",
)
write("scripts/verify_modular_architecture.ps1", verify)

plan = read("docs/MODULARIZATION_PLAN.md")
plan = plan.replace(
    '- immutable completed-history plus mutable live-tail sharing so `0B` does not rebuild or copy the complete history on every tick;\n',
    '- completed immutable history and mutable live tail are shared separately, so same-minute `0B` updates reuse all completed candle and volume storage;\n',
)
plan = plan.replace(
    'Remaining before the local visual acceptance request:\n\n- session/date boundary rendering;\n- completed immutable history and mutable live tail are shared separately, so same-minute `0B` updates reuse all completed candle and volume storage;\n- final Windows CI verification after the live-tail split;\n- one focused visual/GPU test covering zoom, pan, crosshair, latest-bar follow, feature levels, and real `0B` updates.\n',
    'Completed performance structure:\n\n- completed candle history is immutable shared storage;\n- the current live candle is a separate small tail value;\n- same-minute `0B` events update only the live tail and document metadata;\n- completed volume history is rebuilt only when a new minute promotes the prior live candle;\n- full loaded history remains available for zoom and pan without a per-tick full-vector copy.\n\nRemaining before the local visual acceptance request:\n\n- session/date boundary rendering;\n- final Windows CI verification;\n- one focused visual/GPU test covering zoom, pan, crosshair, latest-bar follow, feature levels, and real `0B` updates.\n',
)
write("docs/MODULARIZATION_PLAN.md", plan)

handoff = read("docs/SESSION_HANDOFF.md")
handoff = handoff.replace(
    'Still required before asking the user to test:\n\n1. split completed immutable history from the mutable live bar;\n2. share completed history into render documents without copying on every `0B`;\n3. add session/date boundary rendering;\n4. run Windows MSVC build and the complete headless suite;\n5. record the verified HEAD and CI run below.\n',
    'Completed after the first M6 checkpoint:\n\n- completed immutable history is separate from the mutable live bar;\n- render documents share completed candle and volume history;\n- same-minute `0B` updates do not copy the complete loaded history;\n- a new minute promotes the old live bar and rebuilds completed volume history once.\n\nStill required before asking the user to test:\n\n1. add session/date boundary rendering;\n2. run Windows MSVC build and the complete headless suite;\n3. record the verified HEAD and CI run below.\n',
)
write("docs/SESSION_HANDOFF.md", handoff)

print("Integrated immutable completed history and mutable live tails into the shell")
