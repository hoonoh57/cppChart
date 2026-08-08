#include "imgui.h"
#include "app/indicator_workspace_store.h"
#include "ui/indicator_manager_ui.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace trading::app
{
    std::vector<IndicatorInstanceDefinition>
    M95InitialIndicatorDefinitions();
}

namespace trading::ui
{
    void M95DrawIndicatorManagerWindow(
        std::vector<app::IndicatorInstanceDefinition>& definitions,
        IndicatorManagerUiState& state,
        ApplyIndicatorDefinitions applyDefinitions);
}

namespace ImGui
{
    void M95Render();
    bool M95SmallButton(const char* label);
}

#define InitialIndicatorDefinitions M95InitialIndicatorDefinitions
#define DrawIndicatorManagerWindow M95DrawIndicatorManagerWindow
#define Render M95Render
#define SmallButton M95SmallButton
#include "shell_main_m89.cpp"
#undef SmallButton
#undef Render
#undef DrawIndicatorManagerWindow
#undef InitialIndicatorDefinitions

namespace
{
    using M95Clock = std::chrono::steady_clock;

    struct M95IndicatorWorkspaceRuntime final
    {
        bool loadAttempted = false;
        bool loaded = false;
        bool paneDirty = false;
        trading::app::IndicatorWorkspaceSource source =
            trading::app::IndicatorWorkspaceSource::None;
        trading::app::IndicatorWorkspaceState authoritativeState;
        std::string savedPath;
        std::string defaultPath;
        std::string lastPersistedJson;
        std::string lastObservedPaneJson;
        M95Clock::time_point paneChangedAt{};
    };

    M95IndicatorWorkspaceRuntime& M95Runtime()
    {
        static M95IndicatorWorkspaceRuntime runtime;
        return runtime;
    }

    trading::ui::ApplyIndicatorDefinitions g_m95BaseApplyDefinitions = nullptr;

    const std::filesystem::path& M95ExecutableDirectory()
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

    void M95ResolvePaths(M95IndicatorWorkspaceRuntime& runtime)
    {
        if (!runtime.savedPath.empty()) return;
        const std::filesystem::path root = M95ExecutableDirectory();
        runtime.savedPath =
            (root / L"data" / L"indicator_workspace.json").string();
        runtime.defaultPath =
            (root / L"config" /
             L"indicator_workspace.default.json").string();
    }

    std::string M95VisibleIds(
        const std::vector<trading::app::IndicatorInstanceDefinition>& definitions)
    {
        std::ostringstream stream;
        bool first = true;
        for (const auto& definition : definitions) {
            if (!definition.visible) continue;
            if (!first) stream << ',';
            first = false;
            stream << definition.spec.id;
        }
        return first ? std::string("없음") : stream.str();
    }

    bool M95Serialize(
        const trading::app::IndicatorWorkspaceState& state,
        std::string& json,
        std::string& error)
    {
        return trading::app::SerializeIndicatorWorkspaceState(
            state,
            json,
            error);
    }

    bool M95LoadWorkspace()
    {
        M95IndicatorWorkspaceRuntime& runtime = M95Runtime();
        if (runtime.loadAttempted) return runtime.loaded;
        runtime.loadAttempted = true;
        M89SetTargetBars(600U);
        M95ResolvePaths(runtime);

        std::string diagnostic;
        if (!trading::app::LoadVerifiedIndicatorWorkspace(
                runtime.savedPath,
                runtime.defaultPath,
                runtime.authoritativeState,
                runtime.source,
                diagnostic))
        {
            g_log.Add(
                "FAULT",
                "지표 저장소 초기화 실패: %s | path=%s",
                diagnostic.c_str(),
                runtime.savedPath.c_str());
            return false;
        }

        g_mainRenderSurface.paneHeightWeights =
            runtime.authoritativeState.paneHeightWeights;
        g_mainRenderSurface.paneDefaultHeightWeights.clear();
        g_mainRenderSurface.dirty = true;

        std::string serialized;
        std::string error;
        if (!M95Serialize(runtime.authoritativeState, serialized, error)) {
            g_log.Add(
                "FAULT",
                "지표 저장소 초기 직렬화 실패: %s",
                error.c_str());
            return false;
        }

        runtime.lastPersistedJson = serialized;
        runtime.lastObservedPaneJson = serialized;
        runtime.loaded = true;

        if (!diagnostic.empty()) {
            g_log.Add("SYS", "%s", diagnostic.c_str());
        }
        g_log.Add(
            "SYS",
            "지표 저장소 적용: %s | visible=%s | path=%s",
            trading::app::IndicatorWorkspaceSourceName(runtime.source),
            M95VisibleIds(runtime.authoritativeState.indicators).c_str(),
            runtime.savedPath.c_str());
        g_log.Add(
            "SYS",
            "차트 기본 조회: 600봉 단일 요청, 이전 데이터는 추가데이터로 조회");
        return true;
    }

    bool M95PersistAuthoritativeState(
        const trading::app::IndicatorWorkspaceState& requested,
        const char* reason,
        std::string& error)
    {
        M95IndicatorWorkspaceRuntime& runtime = M95Runtime();
        if (!runtime.loaded) {
            error = "지표 저장소가 초기화되지 않았습니다.";
            return false;
        }

        trading::app::IndicatorWorkspaceState candidate = requested;
        if (!trading::app::NormalizeIndicatorWorkspaceDefinitions(
                candidate.indicators,
                error))
        {
            return false;
        }

        std::string serialized;
        if (!M95Serialize(candidate, serialized, error)) return false;
        if (serialized == runtime.lastPersistedJson) {
            runtime.authoritativeState = std::move(candidate);
            runtime.lastObservedPaneJson = serialized;
            runtime.paneDirty = false;
            error.clear();
            return true;
        }

        if (!trading::app::SaveVerifiedIndicatorWorkspace(
                runtime.savedPath,
                candidate,
                error))
        {
            return false;
        }

        runtime.authoritativeState = std::move(candidate);
        runtime.source = trading::app::IndicatorWorkspaceSource::Saved;
        runtime.lastPersistedJson = serialized;
        runtime.lastObservedPaneJson = serialized;
        runtime.paneDirty = false;
        g_log.Add(
            "SYS",
            "지표 저장소 커밋(%s): visible=%s | path=%s | readback=OK",
            reason != nullptr ? reason : "변경",
            M95VisibleIds(runtime.authoritativeState.indicators).c_str(),
            runtime.savedPath.c_str());
        error.clear();
        return true;
    }

    bool M95ApplyAndCommitIndicatorDefinitions(
        const std::vector<trading::app::IndicatorInstanceDefinition>& requested,
        std::string& error)
    {
        if (g_m95BaseApplyDefinitions == nullptr) {
            error = "지표 구성 적용 함수가 없습니다.";
            return false;
        }

        M95IndicatorWorkspaceRuntime& runtime = M95Runtime();
        const auto previousDefinitions = runtime.authoritativeState.indicators;

        std::vector<trading::app::IndicatorInstanceDefinition> candidate =
            requested;
        if (!trading::app::NormalizeIndicatorWorkspaceDefinitions(
                candidate,
                error))
        {
            return false;
        }
        if (!g_m95BaseApplyDefinitions(candidate, error)) return false;

        trading::app::IndicatorWorkspaceState next =
            runtime.authoritativeState;
        next.indicators = candidate;
        next.paneHeightWeights = g_mainRenderSurface.paneHeightWeights;
        std::string saveError;
        if (!M95PersistAuthoritativeState(next, "지표 적용", saveError)) {
            std::string rollbackError;
            if (!g_m95BaseApplyDefinitions(
                    previousDefinitions,
                    rollbackError))
            {
                g_log.Add(
                    "FAULT",
                    "지표 저장 실패 후 런타임 롤백 실패: %s",
                    rollbackError.c_str());
            }
            error = "지표 저장소 커밋 실패: " + saveError;
            return false;
        }

        error.clear();
        return true;
    }

    void M95PersistPaneHeightsOnly()
    {
        M95IndicatorWorkspaceRuntime& runtime = M95Runtime();
        if (!runtime.loaded) return;

        // Indicator On/Off and parameters always come from the authoritative
        // store state. The renderer contributes pane geometry only.
        trading::app::IndicatorWorkspaceState paneCandidate =
            runtime.authoritativeState;
        paneCandidate.paneHeightWeights =
            g_mainRenderSurface.paneHeightWeights;

        std::string serialized;
        std::string error;
        if (!M95Serialize(paneCandidate, serialized, error)) return;

        const M95Clock::time_point now = M95Clock::now();
        if (serialized != runtime.lastObservedPaneJson) {
            runtime.lastObservedPaneJson = serialized;
            runtime.paneChangedAt = now;
            runtime.paneDirty = true;
        }
        if (!runtime.paneDirty) return;

        const bool interactionFinished =
            !ImGui::IsAnyItemActive() &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const bool timeout =
            now - runtime.paneChangedAt >= std::chrono::milliseconds(300);
        if (interactionFinished || timeout) {
            if (!M95PersistAuthoritativeState(
                    paneCandidate,
                    "패널 높이",
                    error))
            {
                g_log.Add(
                    "FAULT",
                    "패널 높이 저장 실패: %s",
                    error.c_str());
            }
        }
        else {
            WakeFrames(4);
        }
    }

    void M95RestoreMissingBuiltins(
        std::vector<trading::app::IndicatorInstanceDefinition>& definitions)
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
        if (!M95ApplyAndCommitIndicatorDefinitions(normalized, error)) {
            g_log.Add("FAULT", "숨김 지표 Off 복원 실패: %s", error.c_str());
            return;
        }
        definitions = std::move(normalized);
        g_indicatorManagerUi = {};
    }
}

std::vector<trading::app::IndicatorInstanceDefinition>
trading::app::M95InitialIndicatorDefinitions()
{
    if (!M95LoadWorkspace()) return {};
    return M95Runtime().authoritativeState.indicators;
}

void trading::ui::M95DrawIndicatorManagerWindow(
    std::vector<app::IndicatorInstanceDefinition>& definitions,
    IndicatorManagerUiState& state,
    ApplyIndicatorDefinitions applyDefinitions)
{
    g_m95BaseApplyDefinitions = applyDefinitions;
    trading::ui::DrawIndicatorManagerWindow(
        definitions,
        state,
        M95ApplyAndCommitIndicatorDefinitions);
    M95RestoreMissingBuiltins(definitions);
}

bool ImGui::M95SmallButton(const char* label)
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

void ImGui::M95Render()
{
    M95PersistPaneHeightsOnly();
    ImGui::Render();
}
