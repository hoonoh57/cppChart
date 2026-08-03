from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHELL = ROOT / "shell_main.cpp"
ARCH_GATE = ROOT / "scripts" / "verify_modular_architecture.ps1"
CI = ROOT / ".github" / "workflows" / "windows-ci.yml"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


def write(path: Path, text: str) -> None:
    path.write_text("\ufeff" + text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


shell = read(SHELL)

shell = replace_once(
    shell,
    '#include "app/market_data_module.h"\n',
    '#include "app/market_data_module.h"\n'
    '#include "app/chart_workspace_module.h"\n',
    "chart workspace include",
)

shell = replace_once(
    shell,
    '''static trading::app::FeatureRegistry g_featureRegistry;
static trading::app::MarketDataModule g_marketDataModule;
static trading::render::RenderDocument g_mainRenderDocument;
static trading::ui::RenderSurfaceState g_mainRenderSurface;
static std::size_t g_mainRenderVisibleLimit = 0;
''',
    '''static trading::app::FeatureRegistry g_featureRegistry;
static trading::app::MarketDataModule g_marketDataModule;
static trading::app::ChartWorkspaceModule g_chartWorkspaceModule;
static trading::ui::RenderSurfaceState g_mainRenderSurface;
''',
    "chart workspace globals",
)

shell = replace_once(
    shell,
    '''    if (id == "market-data") {
        if (!g_marketDataModule.SetLevel(level, error)) return false;
    }
    return true;
}

static void RecordFeatureWork(
    const std::string& id,
    std::uint64_t elapsedMicros,
    std::size_t renderSeriesCount = 0,
    std::uint64_t mergedEvents = 0,
    std::uint64_t droppedEvents = 0)
{
    const trading::app::MarketDataSnapshot market =
        g_marketDataModule.Snapshot();
    std::string ignored;
    g_featureRegistry.RecordWork(
        id,
        elapsedMicros,
        0,
        market.retainedBytes,
        market.code.empty() ? 0 : 1,
        renderSeriesCount,
        mergedEvents,
        droppedEvents,
        ignored);
}
''',
    '''    if (id == "market-data") {
        if (!g_marketDataModule.SetLevel(level, error)) return false;
    }
    else if (id == "chart-workspace") {
        if (!g_chartWorkspaceModule.SetLevel(level, error)) return false;
    }
    return true;
}

static void RecordFeatureWork(
    const std::string& id,
    std::uint64_t elapsedMicros,
    std::size_t retainedBytes,
    std::size_t symbolCount,
    std::size_t renderSeriesCount,
    std::uint64_t mergedEvents = 0,
    std::uint64_t droppedEvents = 0)
{
    std::string ignored;
    g_featureRegistry.RecordWork(
        id,
        elapsedMicros,
        0,
        retainedBytes,
        symbolCount,
        renderSeriesCount,
        mergedEvents,
        droppedEvents,
        ignored);
}
''',
    "feature work metrics contract",
)

old_chart = '''    const double started = NowSeconds();
    if (
        g_mainRenderDocument.revision != snapshot.revision ||
        g_mainRenderVisibleLimit != visibleLimit)
    {
        const std::vector<trading::Bar> visibleBars =
            g_marketDataModule.CopyVisibleBars(visibleLimit);
        g_mainRenderDocument = trading::render::BuildMarketChartDocument(
            "main-market-chart",
            snapshot.code,
            snapshot.code,
            visibleBars,
            snapshot.revision);
        std::string renderError;
        if (!trading::render::ValidateRenderDocument(
                g_mainRenderDocument,
                renderError))
        {
            g_marketDataModule.SetError(
                "렌더 문서 검증 실패: " + renderError);
            g_log.Add("FAULT", "렌더 문서 검증 실패: %s", renderError.c_str());
            ImGui::End();
            return;
        }
        g_mainRenderVisibleLimit = visibleLimit;
        g_mainRenderSurface.dirty = true;
    }

    trading::ui::DrawRenderDocument(
        g_mainRenderDocument,
        available,
        g_mainRenderSurface);
    const std::uint64_t elapsedMicros = static_cast<std::uint64_t>(
        (NowSeconds() - started) * 1000000.0);
    RecordFeatureWork("chart-workspace", elapsedMicros, 2);
'''
new_chart = '''    const double started = NowSeconds();
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
        {
            g_log.Add(
                "FAULT",
                "차트 워크스페이스 갱신 실패: %s",
                chartError.c_str());
            std::string healthError;
            g_featureRegistry.SetHealth(
                "chart-workspace",
                false,
                chartError,
                healthError);
            ImGui::End();
            return;
        }
        g_mainRenderSurface.dirty = true;
    }

    const trading::app::ChartWorkspaceSnapshot workspace =
        g_chartWorkspaceModule.Snapshot();
    if (workspace.document == nullptr) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
            "차트 렌더 문서가 없습니다.");
        ImGui::End();
        return;
    }

    trading::ui::DrawRenderDocument(
        *workspace.document,
        available,
        g_mainRenderSurface);
    const std::uint64_t elapsedMicros = static_cast<std::uint64_t>(
        (NowSeconds() - started) * 1000000.0);
    RecordFeatureWork(
        "chart-workspace",
        elapsedMicros,
        workspace.retainedBytes,
        snapshot.code.empty() ? 0 : 1,
        workspace.seriesCount);
    std::string healthError;
    g_featureRegistry.SetHealth(
        "chart-workspace",
        workspace.state == trading::app::ChartWorkspaceState::Ready,
        workspace.error,
        healthError);
'''
shell = replace_once(
    shell,
    old_chart,
    new_chart,
    "chart workspace render path",
)

old_market_metrics = '''            RecordFeatureWork(
                "market-data",
                elapsedMicros,
                snapshot.hasLatestBar ? 2 : 0,
                0,
                applied.stale ? 1 : 0);
'''
new_market_metrics = '''            RecordFeatureWork(
                "market-data",
                elapsedMicros,
                snapshot.retainedBytes,
                snapshot.code.empty() ? 0 : 1,
                snapshot.hasLatestBar ? 2 : 0,
                0,
                applied.stale ? 1 : 0);
'''
if shell.count(old_market_metrics) != 2:
    raise RuntimeError(
        f"market metrics: expected two matches, found {shell.count(old_market_metrics)}")
shell = shell.replace(old_market_metrics, new_market_metrics)

shell = replace_once(
    shell,
    '''            int selectedLevel = static_cast<int>(feature.level);
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::Combo(
                    "##level",
                    &selectedLevel,
                    levels,
                    IM_ARRAYSIZE(levels)))
            {
''',
    '''            int selectedLevel = static_cast<int>(feature.level);
            const bool pinnedDiagnostics = feature.id == "diagnostics";
            if (pinnedDiagnostics) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(90.0f);
            const bool levelChanged = ImGui::Combo(
                "##level",
                &selectedLevel,
                levels,
                IM_ARRAYSIZE(levels));
            if (pinnedDiagnostics) ImGui::EndDisabled();
            if (levelChanged)
            {
''',
    "pin diagnostics control",
)

for forbidden in (
    "g_mainRenderDocument",
    "g_mainRenderVisibleLimit",
    "BuildMarketChartDocument(\n            \"main-market-chart\"",
):
    if forbidden in shell:
        raise RuntimeError(f"legacy chart workspace marker remains: {forbidden}")

write(SHELL, shell)

arch_gate = read(ARCH_GATE)
arch_gate = replace_once(
    arch_gate,
    "    '.\\app\\market_data_module.cpp',\n",
    "    '.\\app\\market_data_module.cpp',\n"
    "    '.\\app\\chart_workspace_module.h',\n"
    "    '.\\app\\chart_workspace_module.cpp',\n",
    "architecture gate chart files",
)
arch_gate = replace_once(
    arch_gate,
    "    'MarketDataModule g_marketDataModule',\n",
    "    'MarketDataModule g_marketDataModule',\n"
    "    'ChartWorkspaceModule g_chartWorkspaceModule',\n",
    "architecture gate chart marker",
)
arch_gate = replace_once(
    arch_gate,
    "    'static void DrawRealCandles('\n",
    "    'static void DrawRealCandles(',\n"
    "    'static trading::render::RenderDocument g_mainRenderDocument'\n",
    "architecture gate old chart state",
)
write(ARCH_GATE, arch_gate)

ci = read(CI)
ci = replace_once(
    ci,
    "          $marketModule = Get-Content .\\app\\market_data_module.cpp -Raw\n",
    "          $marketModule = Get-Content .\\app\\market_data_module.cpp -Raw\n"
    "          $chartModule = Get-Content .\\app\\chart_workspace_module.cpp -Raw\n",
    "CI chart module source",
)
ci = replace_once(
    ci,
    "            'MarketDataModule g_marketDataModule',\n",
    "            'MarketDataModule g_marketDataModule',\n"
    "            'ChartWorkspaceModule g_chartWorkspaceModule',\n",
    "CI chart module marker",
)
ci = replace_once(
    ci,
    "          $renderRequired = @(\n",
    "          $chartModuleRequired = @(\n"
    "            'UpdateMarketChart',\n"
    "            'NeedsUpdate',\n"
    "            'shared_ptr<const render::RenderDocument>',\n"
    "            'FeatureLevel::Off'\n"
    "          )\n"
    "          foreach ($marker in $chartModuleRequired) {\n"
    "            if (-not $chartModule.Contains($marker)) {\n"
    "              throw \"Missing chart workspace module marker: $marker\"\n"
    "            }\n"
    "          }\n\n"
    "          $renderRequired = @(\n",
    "CI chart module contract",
)
ci = replace_once(
    ci,
    "            'static void DrawRealCandles(',\n",
    "            'static void DrawRealCandles(',\n"
    "            'static trading::render::RenderDocument g_mainRenderDocument',\n",
    "CI old chart state",
)
write(CI, ci)

print("Integrated ChartWorkspaceModule and pinned diagnostics controls")
