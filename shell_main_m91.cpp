#include "app/chart_workspace_bootstrap.h"
#include "app/chart_workspace_persistence.h"
#include "ui/indicator_manager_ui.h"
#include "ui/render_document_renderer.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
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
        bool pendingRestoreLog = false;
        bool loadedFromBootstrap = false;
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

    const std::filesystem::path& M91WorkspaceDirectory()
    {
        static const std::filesystem::path directory = [] {
            wchar_t modulePath[32768]{};
            const DWORD length = GetModuleFileNameW(
                nullptr,
                modulePath,
                32768U);
            if (length == 0U || length >= 32768U) {
                return std::filesystem::path("data");
            }
            return std::filesystem::path(modulePath).parent_path() / L"data";
        }();
        return directory;
    }

    const std::string& M91WorkspacePath()
    {
        static const std::string path =
            (M91WorkspaceDirectory() / L"chart_workspace.json").string();
        return path;
    }

    const std::string& M91BootstrapPath()
    {
        static const std::string path =
            (M91WorkspaceDirectory() /
             L"chart_workspace_bootstrap.json").string();
        return path;
    }

    trading::app::ChartWorkspacePersistenceState M91CurrentState()
    {
        trading::app::ChartWorkspacePersistenceState state;
        state.indicators = g_indicatorDefinitions;
        state.paneHeightWeights = g_mainRenderSurface.paneHeightWeights;
        return state;
    }

    const trading::app::IndicatorInstanceDefinition*
    M91FindDefinition(
        const std::vector<trading::app::IndicatorInstanceDefinition>& values,
        const std::string& id,
        const std::string& type)
    {
        for (const auto& value : values) {
            if (value.spec.id == id && value.spec.type == type) {
                return &value;
            }
        }
        return nullptr;
    }

    trading::app::ChartWorkspacePersistenceState M91ComposeLoadedState(
        const trading::app::ChartWorkspacePersistenceState& primary,
        bool primaryFound,
        const trading::app::ChartWorkspacePersistenceState& bootstrap,
        bool bootstrapFound)
    {
        if (!bootstrapFound) return primary;

        trading::app::ChartWorkspacePersistenceState composed;
        composed.indicators.reserve(bootstrap.indicators.size());
        for (const auto& bootstrapDefinition : bootstrap.indicators) {
            const auto* detailed = primaryFound
                ? M91FindDefinition(
                    primary.indicators,
                    bootstrapDefinition.spec.id,
                    bootstrapDefinition.spec.type)
                : nullptr;
            trading::app::IndicatorInstanceDefinition restored =
                detailed != nullptr
                    ? *detailed
                    : bootstrapDefinition;
            restored.spec.parameters =
                bootstrapDefinition.spec.parameters;
            restored.visible = bootstrapDefinition.visible;
            composed.indicators.push_back(std::move(restored));
        }

        composed.paneHeightWeights = primary.paneHeightWeights;
        for (const auto& pane : bootstrap.paneHeightWeights) {
            composed.paneHeightWeights[pane.first] = pane.second;
        }
        return composed;
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

        std::string primaryError;
        const bool primarySaved =
            trading::app::SaveChartWorkspaceState(
                M91WorkspacePath(),
                state,
                primaryError);
        std::string bootstrapError;
        const bool bootstrapSaved =
            trading::app::SaveChartWorkspaceBootstrap(
                M91BootstrapPath(),
                state,
                bootstrapError);

        if (!primarySaved) {
            g_log.Add(
                "FAULT",
                "차트 작업공간 전체 저장 실패: %s",
                primaryError.c_str());
        }
        if (!bootstrapSaved) {
            g_log.Add(
                "FAULT",
                "차트 작업공간 복원본 저장 실패: %s",
                bootstrapError.c_str());
        }
        if (!primarySaved && !bootstrapSaved) return;

        runtime.lastSavedJson = json;
        runtime.lastObservedJson = json;
        runtime.dirty = false;
        g_log.Add(
            "SYS",
            "차트 작업공간 저장(%s): 지표 %zu개, 패널 높이 %zu개, 복원본 %s",
            reason != nullptr ? reason : "변경",
            state.indicators.size(),
            state.paneHeightWeights.size(),
            bootstrapSaved ? "완료" : "실패");
    }

    void M91LoadStateIfNeeded()
    {
        M91PersistenceRuntime& runtime = M91Runtime();
        if (runtime.loadAttempted) return;
        runtime.loadAttempted = true;

        trading::app::ChartWorkspacePersistenceState primary;
        bool primaryFound = false;
        std::string primaryError;
        const bool primaryOk = trading::app::LoadChartWorkspaceState(
            M91WorkspacePath(),
            primary,
            primaryFound,
            primaryError);

        trading::app::ChartWorkspacePersistenceState bootstrap;
        bool bootstrapFound = false;
        std::string bootstrapError;
        const bool bootstrapOk = trading::app::LoadChartWorkspaceBootstrap(
            M91BootstrapPath(),
            bootstrap,
            bootstrapFound,
            bootstrapError);

        if ((!primaryOk || !primaryFound) &&
            (!bootstrapOk || !bootstrapFound))
        {
            if (!primaryOk || !bootstrapOk) {
                runtime.loadError =
                    "전체=" +
                    (primaryError.empty() ? std::string("없음") : primaryError) +
                    " / 복원본=" +
                    (bootstrapError.empty() ? std::string("없음") : bootstrapError);
            }
            runtime.pendingRestoreLog = true;
            return;
        }

        runtime.loadedState = M91ComposeLoadedState(
            primary,
            primaryOk && primaryFound,
            bootstrap,
            bootstrapOk && bootstrapFound);
        runtime.loadedFromBootstrap = bootstrapOk && bootstrapFound;

        std::string json;
        std::string validationError;
        if (!trading::app::SerializeChartWorkspaceState(
                runtime.loadedState,
                json,
                validationError))
        {
            runtime.loadError = validationError;
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
            "차트 작업공간 복원: 지표 %zu개, 패널 높이 %zu개, 복원본 %s",
            runtime.loadedState.indicators.size(),
            runtime.loadedState.paneHeightWeights.size(),
            runtime.loadedFromBootstrap ? "적용" : "미사용");
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
        runtime.loadedIndicatorsApplied = true;
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
    if (appliedNow) return;

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
