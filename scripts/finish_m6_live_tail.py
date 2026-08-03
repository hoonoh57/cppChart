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
old_shell = '''    const ImVec2 available = ImGui::GetContentRegionAvail();
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
new_shell = '''    const ImVec2 available = ImGui::GetContentRegionAvail();
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
if "ChartMarketSource chartSource" not in shell:
    shell = replace_once(
        shell,
        old_shell,
        new_shell,
        "shell live-tail chart source",
    )
    write("shell_main.cpp", shell)
    print("Integrated shared history/live tail into shell_main.cpp")
else:
    print("shell_main.cpp live-tail integration already present")


verify = read("scripts/verify_modular_architecture.ps1")
if "Shared immutable history/live-tail render contract is missing" not in verify:
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
    print("Strengthened modular architecture verification")
else:
    print("modular architecture live-tail verification already present")


plan = read("docs/MODULARIZATION_PLAN.md")
old_plan = '''Remaining before the local visual acceptance request:

- session/date boundary rendering;
- immutable completed-history plus mutable live-tail sharing so `0B` does not rebuild or copy the complete history on every tick;
- final Windows CI verification after the live-tail split;
- one focused visual/GPU test covering zoom, pan, crosshair, latest-bar follow, feature levels, and real `0B` updates.
'''
new_plan = '''Completed performance structure:

- completed candle history is immutable shared storage;
- the current live candle is a separate small tail value;
- same-minute `0B` events update only the live tail and document metadata;
- completed volume history is rebuilt only when a new minute promotes the prior live candle;
- full loaded history remains available for zoom and pan without a per-tick full-vector copy.

Remaining before the local visual acceptance request:

- session/date boundary rendering;
- final Windows CI verification;
- one focused visual/GPU test covering zoom, pan, crosshair, latest-bar follow, feature levels, and real `0B` updates.
'''
if old_plan in plan:
    plan = plan.replace(old_plan, new_plan, 1)
    write("docs/MODULARIZATION_PLAN.md", plan)
    print("Updated M6 performance plan")
else:
    print("M6 performance plan already updated or evolved")


handoff = read("docs/SESSION_HANDOFF.md")
old_handoff = '''Still required before asking the user to test:

1. split completed immutable history from the mutable live bar;
2. share completed history into render documents without copying on every `0B`;
3. add session/date boundary rendering;
4. run Windows MSVC build and the complete headless suite;
5. record the verified HEAD and CI run below.
'''
new_handoff = '''Completed after the first M6 checkpoint:

- completed immutable history is separate from the mutable live bar;
- render documents share completed candle and volume history;
- same-minute `0B` updates do not copy the complete loaded history;
- a new minute promotes the old live bar and rebuilds completed volume history once.

Still required before asking the user to test:

1. add session/date boundary rendering;
2. run Windows MSVC build and the complete headless suite;
3. record the verified HEAD and CI run below.
'''
if old_handoff in handoff:
    handoff = handoff.replace(old_handoff, new_handoff, 1)
    write("docs/SESSION_HANDOFF.md", handoff)
    print("Updated M6 session handoff")
else:
    print("M6 session handoff already updated or evolved")
