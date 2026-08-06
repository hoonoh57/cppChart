#include "imgui.h"
#include "app/indicator_workspace_state.h"
#include "ui/indicator_manager_ui.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace trading::app
{
    std::vector<IndicatorInstanceDefinition>
    M94InitialIndicatorDefinitions();
}

namespace trading::ui
{
    void M94DrawIndicatorManagerWindow(
        std::vector<app::IndicatorInstanceDefinition>& definitions,
        IndicatorManagerUiState& state,
        ApplyIndicatorDefinitions applyDefinitions);
}

namespace ImGui
{
    void M94Render();
    bool M94SmallButton(const char* label);
}

#define InitialIndicatorDefinitions M94InitialIndicatorDefinitions
#define DrawIndicatorManagerWindow M94DrawIndicatorManagerWindow
#define Render M94Render
#define SmallButton M94SmallButton
#include "shell_main_m89.cpp"
#undef SmallButton
#undef Render
#undef DrawIndicatorManagerWindow
#undef InitialIndicatorDefinitions

namespace
{
    using M94Clock = std::chrono::steady_clock;

    struct M94IndicatorWorkspaceRuntime final
    {
        bool loadAttempted = false;
        bool loaded = false;
        bool dirty = false;
        trading::app::IndicatorWorkspaceSource source =
            trading::app::IndicatorWorkspaceSource::None;
        trading::app::IndicatorWorkspaceState loadedState;
        std::string savedPath;
        std::string legacyPath;
        std::string defaultPath;
        std::string lastObservedJson;
        std::string lastSavedJson;
        M94Clock::time_point changedAt{};
    };

    M94IndicatorWorkspaceRuntime& M94Runtime()
    {
        static M94IndicatorWorkspaceRuntime runtime;
        return runtime;
    }

    const std::filesystem::path& M94ExecutableDirectory()
    {
        static const std::filesystem::path directory = [] {
            wchar_t modulePath[32768]{};
            const DWORD length = GetModuleFileNameW(
                nullptr,
                modulePath,
                32768U);
            if (length == 0U || length >= 32768U) {
                return std::filesystem::current_path();
            }
            return std::filesystem::path(modulePath).parent_path();
        }();
        return directory;
    }

    void M94ResolvePaths(M94IndicatorWorkspaceRuntime& runtime)
    {
        if (!runtime.savedPath.empty()) return;
        const std::filesystem::path root = M94ExecutableDirectory();
        runtime.savedPath =
            (root / L"data" / L"indicator_workspace.json").string();
        runtime.legacyPath =
            (root / L"data" / L"chart_workspace.json").string();
        runtime.defaultPath =
            (root / L"config" /
             L"indicator_workspace.default.json").string();
    }

    bool M94LoadWorkspace()
    {
        M94IndicatorWorkspaceRuntime& runtime = M94Runtime();
        if (runtime.loadAttempted) return runtime.loaded;
        runtime.loadAttempted = true;
        M94ResolvePaths(runtime);

        std::string diagnostic;
        if (!trading::app::LoadIndicatorWorkspaceState(
                runtime.savedPath,
                runtime.legacyPath,
                runtime.defaultPath,
                runtime.loadedState,
                runtime.source,
                diagnostic))
        {
            g_log.Add(
                "FAULT",
                "지표 상태 초기화 실패: %s",
                diagnostic.c_str());
            return false;
        }

        g_mainRenderSurface.paneHeightWeights =
            runtime.loadedState.paneHeightWeights;
        g_mainRenderSurface.paneDefaultHeightWeights.clear();
        g_mainRenderSurface.dirty = true;

        std::string serialized;
        std::string error;
        if (!trading::app::SerializeIndicatorWorkspaceState(
                runtime.loadedState,
                serialized,
                error))
        {
            g_log.Add(
                "FAULT",
                "지표 상태 초기 직렬화 실패: %s",
                error.c_str());
            return false;
        }

        runtime.lastObservedJson = serialized;
        runtime.lastSavedJson =
            runtime.source == trading::app::IndicatorWorkspaceSource::Saved
                ? serialized
                : std::string{};
        runtime.dirty =
            runtime.source != trading::app::IndicatorWorkspaceSource::Saved;
        runtime.changedAt = M94Clock::now();
        runtime.loaded = true;

        if (!diagnostic.empty()) {
            g_log.Add("FAULT", "%s", diagnostic.c_str());
        }
        g_log.Add(
            "SYS",
            "지표 상태 적용: %s, 지표 %zu개, 표시 %zu개, 패널 높이 %zu개",
            trading::app::IndicatorWorkspaceSourceName(runtime.source),
            runtime.loadedState.indicators.size(),
            trading::app::VisibleIndicatorSpecs(
                runtime.loadedState.indicators).size(),
            runtime.loadedState.paneHeightWeights.size());
        return true;
    }

    trading::app::IndicatorWorkspaceState M94CurrentState()
    {
        trading::app::IndicatorWorkspaceState state;
        state.indicators = g_indicatorDefinitions;
        state.paneHeightWeights = g_mainRenderSurface.paneHeightWeights;
        return state;
    }

    void M94SaveWorkspace(const char* reason)
    {
        M94IndicatorWorkspaceRuntime& runtime = M94Runtime();
        if (!runtime.loaded) return;

        trading::app::IndicatorWorkspaceState state = M94CurrentState();
        std::string error;
        if (!trading::app::NormalizeIndicatorWorkspaceDefinitions(
                state.indicators,
                error))
        {
            g_log.Add("FAULT", "지표 상태 정규화 실패: %s", error.c_str());
            return;
        }

        std::string serialized;
        if (!trading::app::SerializeIndicatorWorkspaceState(
                state,
                serialized,
                error))
        {
            g_log.Add("FAULT", "지표 상태 직렬화 실패: %s", error.c_str());
            return;
        }
        if (serialized == runtime.lastSavedJson) {
            runtime.lastObservedJson = serialized;
            runtime.dirty = false;
            return;
        }

        if (!trading::app::SaveIndicatorWorkspaceState(
                runtime.savedPath,
                state,
                error))
        {
            g_log.Add("FAULT", "지표 상태 저장 실패: %s", error.c_str());
            return;
        }

        runtime.loadedState = state;
        runtime.source = trading::app::IndicatorWorkspaceSource::Saved;
        runtime.lastObservedJson = serialized;
        runtime.lastSavedJson = serialized;
        runtime.dirty = false;
        g_log.Add(
            "SYS",
            "지표 상태 저장(%s): 지표 %zu개, 표시 %zu개, 패널 높이 %zu개",
            reason != nullptr ? reason : "변경",
            state.indicators.size(),
            trading::app::VisibleIndicatorSpecs(state.indicators).size(),
            state.paneHeightWeights.size());
    }

    void M94PersistencePump()
    {
        M94IndicatorWorkspaceRuntime& runtime = M94Runtime();
        if (!runtime.loaded) return;

        const trading::app::IndicatorWorkspaceState state =
            M94CurrentState();
        std::string serialized;
        std::string error;
        if (!trading::app::SerializeIndicatorWorkspaceState(
                state,
                serialized,
                error))
        {
            return;
        }

        const M94Clock::time_point now = M94Clock::now();
        if (serialized != runtime.lastObservedJson) {
            runtime.lastObservedJson = serialized;
            runtime.changedAt = now;
            runtime.dirty = true;
        }
        if (!runtime.dirty) return;

        const bool interactionFinished =
            !ImGui::IsAnyItemActive() &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const bool timeout =
            now - runtime.changedAt >= std::chrono::milliseconds(300);
        if (interactionFinished || timeout) {
            M94SaveWorkspace("상태");
        }
        else {
            WakeFrames(4);
        }
    }

    void M94RestoreMissingBuiltins(
        std::vector<trading::app::IndicatorInstanceDefinition>& definitions,
        trading::ui::ApplyIndicatorDefinitions applyDefinitions)
    {
        std::vector<trading::app::IndicatorInstanceDefinition> normalized =
            definitions;
        std::string error;
        if (!trading::app::NormalizeIndicatorWorkspaceDefinitions(
                normalized,
                error))
        {
            g_log.Add("FAULT", "지표 스위치 정규화 실패: %s", error.c_str());
            return;
        }
        if (normalized.size() == definitions.size()) return;
        if (applyDefinitions == nullptr ||
            !applyDefinitions(normalized, error))
        {
            g_log.Add("FAULT", "숨김 지표 복원 실패: %s", error.c_str());
            return;
        }
        definitions = std::move(normalized);
        g_indicatorManagerUi = {};
        g_log.Add(
            "SYS",
            "지표 스위치 목록 정규화: 지원 지표는 삭제 대신 Off 상태로 유지");
    }
}

std::vector<trading::app::IndicatorInstanceDefinition>
trading::app::M94InitialIndicatorDefinitions()
{
    if (!M94LoadWorkspace()) return {};
    return M94Runtime().loadedState.indicators;
}

void trading::ui::M94DrawIndicatorManagerWindow(
    std::vector<app::IndicatorInstanceDefinition>& definitions,
    IndicatorManagerUiState& state,
    ApplyIndicatorDefinitions applyDefinitions)
{
    trading::ui::DrawIndicatorManagerWindow(
        definitions,
        state,
        applyDefinitions);
    M94RestoreMissingBuiltins(definitions, applyDefinitions);
}

bool ImGui::M94SmallButton(const char* label)
{
    if (label == nullptr) return ImGui::SmallButton(label);
    if (std::strcmp(label, "이동##date_m87") == 0) {
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        return false;
    }
    if (std::strcmp(label, "<##date_m87") == 0) {
        const bool pressed = ImGui::SmallButton(label);
        if (pressed) M89ShiftDateText(-1);
        return false;
    }
    if (std::strcmp(label, ">##date_m87") == 0) {
        const bool pressed = ImGui::SmallButton(label);
        if (pressed) M89ShiftDateText(1);
        return false;
    }
    if (std::strcmp(label, "오늘/최신##date_m87") == 0) {
        const bool pressed = ImGui::SmallButton(label);
        if (pressed) M89SetTodayText();
        return false;
    }
    return ImGui::SmallButton(label);
}

void ImGui::M94Render()
{
    M94PersistencePump();
    ImGui::Render();
}
