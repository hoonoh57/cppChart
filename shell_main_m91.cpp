#include "app/chart_workspace_persistence.h"
#include "ui/indicator_manager_ui.h"
#include "ui/render_document_renderer.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace trading::app
{
    std::vector<IndicatorInstanceDefinition>
    M91InitialIndicatorDefinitions();
}

namespace trading::ui
{
    void M91DrawRenderDocument(
        const render::RenderDocument& document,
        ImVec2 size,
        RenderSurfaceState& surfaceState);

    void M91DrawIndicatorManagerWindow(
        std::vector<app::IndicatorInstanceDefinition>& definitions,
        IndicatorManagerUiState& state,
        ApplyIndicatorDefinitions applyDefinitions);
}

#define InitialIndicatorDefinitions M91InitialIndicatorDefinitions
#define DrawRenderDocument M91DrawRenderDocument
#define DrawIndicatorManagerWindow M91DrawIndicatorManagerWindow
#include "shell_main_m90.cpp"
#undef DrawIndicatorManagerWindow
#undef DrawRenderDocument
#undef InitialIndicatorDefinitions

namespace
{
    using M91Clock = std::chrono::steady_clock;

    struct M91PersistenceRuntime final
    {
        trading::app::ChartWorkspacePersistenceState loadedState;
        bool loadAttempted = false;
        bool loaded = false;
        bool loadedIndicatorsApplied = false;
        bool paneHeightsApplied = false;
        bool dirty = false;
        bool exitHandlerRegistered = false;
        bool pendingRestoreLog = false;
        std::string loadError;
        std::string lastObservedJson;
        std::string lastSavedJson;
        M91Clock::time_point changedAt{};
    };

    M91PersistenceRuntime& M91Runtime()
    {
        static M91PersistenceRuntime runtime;
        return runtime;
    }

    const std::string& M91WorkspacePath()
    {
        static const std::string path = [] {
            wchar_t modulePath[32768]{};
            const DWORD length = GetModuleFileNameW(
                nullptr,
                modulePath,
                static_cast<DWORD>(std::size(modulePath)));
            if (length == 0U || length >= std::size(modulePath)) {
                return std::string("data/chart_workspace.json");
            }
            const std::filesystem::path executable(modulePath);
            return (executable.parent_path() /
                    L"data" /
                    L"chart_workspace.json").string();
        }();
        return path;
    }

    trading::app::ChartWorkspacePersistenceState M91CurrentState()
    {
        trading::app::ChartWorkspacePersistenceState state;
        state.indicators = g_indicatorDefinitions;
        state.paneHeightWeights = g_mainRenderSurface.paneHeightWeights;
        return state;
    }

    void M91SaveNow(const char* reason)
    {
        M91PersistenceRuntime& runtime = M91Runtime();
        if (!runtime.loadAttempted) return;
        if (runtime.loaded && !runtime.loadedIndicatorsApplied) return;

        const trading::app::ChartWorkspacePersistenceState state =
            M91CurrentState();
        std::string json;
        std::string error;
        if (!trading::app::SerializeChartWorkspaceState(
                state,
                json,
                error))
        {
            g_log.Add(
                "FAULT",
                "차트 작업공간 직렬화 실패: %s",
                error.c_str());
            return;
        }
        if (json == runtime.lastSavedJson) {
            runtime.lastObservedJson = json;
            runtime.dirty = false;
            return;
        }
        if (!trading::app::SaveChartWorkspaceState(
                M91WorkspacePath(),
                state,
                error))
        {
            g_log.Add(
                "FAULT",
                "차트 작업공간 저장 실패: %s",
                error.c_str());
            return;
        }
        runtime.lastSavedJson = json;
        runtime.lastObservedJson = json;
        runtime.dirty = false;
        g_log.Add(
            "SYS",
            "차트 작업공간 저장(%s): 지표 %zu개, 패널 높이 %zu개",
            reason != nullptr ? reason : "변경",
            state.indicators.size(),
            state.paneHeightWeights.size());
    }

    void M91SaveAtExit()
    {
        M91SaveNow("종료");
    }

    void M91RegisterExitHandler()
    {
        M91PersistenceRuntime& runtime = M91Runtime();
        if (runtime.exitHandlerRegistered) return;
        std::atexit(M91SaveAtExit);
        runtime.exitHandlerRegistered = true;
    }

    void M91LoadStateIfNeeded()
    {
        M91PersistenceRuntime& runtime = M91Runtime();
        if (runtime.loadAttempted) return;
        runtime.loadAttempted = true;
        M91RegisterExitHandler();

        bool found = false;
        std::string error;
        if (!trading::app::LoadChartWorkspaceState(
                M91WorkspacePath(),
                runtime.loadedState,
                found,
                error))
        {
            runtime.loadError = error;
            runtime.pendingRestoreLog = true;
            return;
        }
        if (!found) {
            runtime.pendingRestoreLog = true;
            return;
        }

        std::string json;
        if (!trading::app::SerializeChartWorkspaceState(
                runtime.loadedState,
                json,
                error))
        {
            runtime.loadError = error;
            runtime.pendingRestoreLog = true;
            return;
        }

        runtime.lastObservedJson = json;
        runtime.lastSavedJson = json;
        runtime.loaded = true;
        runtime.pendingRestoreLog = true;
    }

    void M91FlushRestoreLog()
    {
        M91PersistenceRuntime& runtime = M91Runtime();
        if (!runtime.pendingRestoreLog) return;
        runtime.pendingRestoreLog = false;

        if (!runtime.loadError.empty()) {
            g_log.Add(
                "FAULT",
                "차트 작업공간 복원 실패, 기본 지표 사용: %s",
                runtime.loadError.c_str());
            return;
        }
        if (!runtime.loaded) {
            g_log.Add(
                "SYS",
                "차트 작업공간 파일 없음: 기본 지표 사용 (%s)",
                M91WorkspacePath().c_str());
            return;
        }
        g_log.Add(
            "SYS",
            "차트 작업공간 파일 읽음: 지표 %zu개, 패널 높이 %zu개 (%s)",
            runtime.loadedState.indicators.size(),
            runtime.loadedState.paneHeightWeights.size(),
            M91WorkspacePath().c_str());
    }

    bool M91ApplyLoadedIndicatorsIfNeeded(bool& appliedNow)
    {
        appliedNow = false;
        M91LoadStateIfNeeded();
        M91FlushRestoreLog();

        M91PersistenceRuntime& runtime = M91Runtime();
        if (runtime.loadedIndicatorsApplied) return true;
        if (!runtime.loaded) {
            runtime.loadedIndicatorsApplied = true;
            return true;
        }

        std::string error;
        if (!ApplyIndicatorConfiguration(
                runtime.loadedState.indicators,
                error))
        {
            g_log.Add(
                "FAULT",
                "저장된 지표 구성을 런타임에 적용하지 못했습니다: %s",
                error.c_str());
            return false;
        }

        g_indicatorDefinitions = runtime.loadedState.indicators;
        g_indicatorManagerUi = {};
        g_mainRenderSurface.paneHeightWeights.clear();
        g_mainRenderSurface.paneDefaultHeightWeights.clear();
        runtime.loadedIndicatorsApplied = true;
        runtime.paneHeightsApplied = false;
        appliedNow = true;
        WakeFrames(8);
        g_log.Add(
            "SYS",
            "차트 지표 구성 적용: 지표 %zu개",
            g_indicatorDefinitions.size());
        return true;
    }

    void M91ApplyLoadedPaneHeights(
        const trading::render::RenderDocument& document,
        trading::ui::RenderSurfaceState& surfaceState)
    {
        M91PersistenceRuntime& runtime = M91Runtime();
        if (runtime.paneHeightsApplied) return;
        if (!runtime.loaded || !runtime.loadedIndicatorsApplied) {
            runtime.paneHeightsApplied = true;
            return;
        }

        std::size_t appliedCount = 0U;
        for (const trading::render::Pane& pane : document.panes) {
            const auto found =
                runtime.loadedState.paneHeightWeights.find(pane.id);
            if (found == runtime.loadedState.paneHeightWeights.end()) continue;
            if (!std::isfinite(found->second) || found->second <= 0.0f) {
                continue;
            }
            surfaceState.paneHeightWeights[pane.id] = found->second;
            surfaceState.paneDefaultHeightWeights[pane.id] =
                (std::max)(0.01f, pane.heightWeight);
            ++appliedCount;
        }
        surfaceState.dirty = true;
        runtime.paneHeightsApplied = true;
        g_log.Add(
            "SYS",
            "차트 패널 높이 적용: %zu개",
            appliedCount);
    }

    void M91PersistencePump(const char* source)
    {
        M91LoadStateIfNeeded();
        M91PersistenceRuntime& runtime = M91Runtime();
        if (runtime.loaded && !runtime.loadedIndicatorsApplied) return;

        const trading::app::ChartWorkspacePersistenceState state =
            M91CurrentState();
        std::string json;
        std::string error;
        if (!trading::app::SerializeChartWorkspaceState(
                state,
                json,
                error))
        {
            return;
        }

        const M91Clock::time_point now = M91Clock::now();
        if (json != runtime.lastObservedJson) {
            runtime.lastObservedJson = json;
            runtime.changedAt = now;
            runtime.dirty = true;
        }

        if (!runtime.dirty) return;

        const bool interactionFinished =
            !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsAnyItemActive();
        const bool debounceExpired =
            now - runtime.changedAt >= std::chrono::milliseconds(350);
        if (interactionFinished || debounceExpired) {
            M91SaveNow(source);
        }
        else {
            WakeFrames(4);
        }
    }
}

std::vector<trading::app::IndicatorInstanceDefinition>
trading::app::M91InitialIndicatorDefinitions()
{
    M91LoadStateIfNeeded();
    M91PersistenceRuntime& runtime = M91Runtime();
    if (runtime.loaded) {
        return runtime.loadedState.indicators;
    }
    return trading::app::InitialIndicatorDefinitions();
}

void trading::ui::M91DrawRenderDocument(
    const render::RenderDocument& document,
    ImVec2 size,
    RenderSurfaceState& surfaceState)
{
    bool appliedNow = false;
    if (!M91ApplyLoadedIndicatorsIfNeeded(appliedNow)) {
        trading::ui::DrawRenderDocument(document, size, surfaceState);
        return;
    }
    if (appliedNow) {
        return;
    }

    M91ApplyLoadedPaneHeights(document, surfaceState);
    trading::ui::DrawRenderDocument(document, size, surfaceState);
    M91PersistencePump("패널");
}

void trading::ui::M91DrawIndicatorManagerWindow(
    std::vector<app::IndicatorInstanceDefinition>& definitions,
    IndicatorManagerUiState& state,
    ApplyIndicatorDefinitions applyDefinitions)
{
    bool ignored = false;
    M91ApplyLoadedIndicatorsIfNeeded(ignored);
    trading::ui::DrawIndicatorManagerWindow(
        definitions,
        state,
        applyDefinitions);
    M91PersistencePump("지표");
}