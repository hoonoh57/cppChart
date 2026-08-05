#include "app/chart_workspace_persistence.h"
#include "ui/indicator_manager_ui.h"
#include "ui/render_document_renderer.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iterator>
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

    trading::app::ChartWorkspacePersistenceState g_m91LoadedState;
    bool g_m91LoadAttempted = false;
    bool g_m91Loaded = false;
    bool g_m91PaneHeightsApplied = false;
    bool g_m91Dirty = false;
    bool g_m91ExitHandlerRegistered = false;
    std::string g_m91LastObservedJson;
    std::string g_m91LastSavedJson;
    M91Clock::time_point g_m91ChangedAt{};

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
        if (!g_m91LoadAttempted) return;

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
        if (json == g_m91LastSavedJson) {
            g_m91LastObservedJson = json;
            g_m91Dirty = false;
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
        g_m91LastSavedJson = json;
        g_m91LastObservedJson = json;
        g_m91Dirty = false;
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
        if (g_m91ExitHandlerRegistered) return;
        std::atexit(M91SaveAtExit);
        g_m91ExitHandlerRegistered = true;
    }

    void M91LoadStateIfNeeded()
    {
        if (g_m91LoadAttempted) return;
        g_m91LoadAttempted = true;
        M91RegisterExitHandler();

        bool found = false;
        std::string error;
        if (!trading::app::LoadChartWorkspaceState(
                M91WorkspacePath(),
                g_m91LoadedState,
                found,
                error))
        {
            g_log.Add(
                "FAULT",
                "차트 작업공간 복원 실패, 기본 지표 사용: %s",
                error.c_str());
            return;
        }
        if (!found) {
            g_log.Add(
                "SYS",
                "차트 작업공간 파일 없음: 기본 지표 사용");
            return;
        }

        std::string json;
        if (!trading::app::SerializeChartWorkspaceState(
                g_m91LoadedState,
                json,
                error))
        {
            g_log.Add(
                "FAULT",
                "복원된 차트 작업공간 검증 실패: %s",
                error.c_str());
            return;
        }

        g_m91LastObservedJson = json;
        g_m91LastSavedJson = json;
        g_m91Loaded = true;
        g_log.Add(
            "SYS",
            "차트 작업공간 복원: 지표 %zu개, 패널 높이 %zu개",
            g_m91LoadedState.indicators.size(),
            g_m91LoadedState.paneHeightWeights.size());
    }

    void M91ApplyLoadedPaneHeights(
        const trading::render::RenderDocument& document,
        trading::ui::RenderSurfaceState& surfaceState)
    {
        if (g_m91PaneHeightsApplied) return;
        M91LoadStateIfNeeded();
        if (!g_m91Loaded) {
            g_m91PaneHeightsApplied = true;
            return;
        }

        std::size_t appliedCount = 0U;
        for (const trading::render::Pane& pane : document.panes) {
            const auto found =
                g_m91LoadedState.paneHeightWeights.find(pane.id);
            if (found == g_m91LoadedState.paneHeightWeights.end()) continue;
            if (!std::isfinite(found->second) || found->second <= 0.0f) {
                continue;
            }
            surfaceState.paneHeightWeights[pane.id] = found->second;
            surfaceState.paneDefaultHeightWeights[pane.id] =
                (std::max)(0.01f, pane.heightWeight);
            ++appliedCount;
        }
        surfaceState.dirty = true;
        g_m91PaneHeightsApplied = true;
        g_log.Add(
            "SYS",
            "차트 패널 높이 적용: %zu개",
            appliedCount);
    }

    void M91PersistencePump(const char* source)
    {
        M91LoadStateIfNeeded();

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
                "차트 작업공간 상태 검증 실패: %s",
                error.c_str());
            return;
        }

        const M91Clock::time_point now = M91Clock::now();
        if (json != g_m91LastObservedJson) {
            g_m91LastObservedJson = json;
            g_m91ChangedAt = now;
            g_m91Dirty = true;
        }

        if (!g_m91Dirty) return;

        const bool interactionFinished =
            !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsAnyItemActive();
        const bool debounceExpired =
            now - g_m91ChangedAt >= std::chrono::milliseconds(350);
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
    if (g_m91Loaded) {
        return g_m91LoadedState.indicators;
    }
    return trading::app::InitialIndicatorDefinitions();
}

void trading::ui::M91DrawRenderDocument(
    const render::RenderDocument& document,
    ImVec2 size,
    RenderSurfaceState& surfaceState)
{
    M91ApplyLoadedPaneHeights(document, surfaceState);
    trading::ui::DrawRenderDocument(document, size, surfaceState);
    M91PersistencePump("패널");
}

void trading::ui::M91DrawIndicatorManagerWindow(
    std::vector<app::IndicatorInstanceDefinition>& definitions,
    IndicatorManagerUiState& state,
    ApplyIndicatorDefinitions applyDefinitions)
{
    trading::ui::DrawIndicatorManagerWindow(
        definitions,
        state,
        applyDefinitions);
    M91PersistencePump("지표");
}
