#include "imgui.h"
#include "app/chart_workspace_persistence.h"
#include "ui/indicator_manager_ui.h"
#include "ui/render_document_renderer.h"

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace trading::app
{
    std::vector<IndicatorInstanceDefinition>
    M90InitialIndicatorDefinitions();
}

namespace trading::ui
{
    void M90DrawRenderDocument(
        const render::RenderDocument& document,
        ImVec2 size,
        RenderSurfaceState& surfaceState);

    void M90DrawIndicatorManagerWindow(
        std::vector<app::IndicatorInstanceDefinition>& definitions,
        IndicatorManagerUiState& state,
        ApplyIndicatorDefinitions applyDefinitions);
}

namespace ImGui
{
    bool M90SmallButton(const char* label);
}

#define InitialIndicatorDefinitions M90InitialIndicatorDefinitions
#define DrawRenderDocument M90DrawRenderDocument
#define DrawIndicatorManagerWindow M90DrawIndicatorManagerWindow
#define SmallButton M90SmallButton
#include "shell_main_m89.cpp"
#undef SmallButton
#undef DrawIndicatorManagerWindow
#undef DrawRenderDocument
#undef InitialIndicatorDefinitions

namespace
{
    using M90Clock = std::chrono::steady_clock;

    struct M90WorkspaceRuntime final
    {
        trading::app::ChartWorkspacePersistenceState loadedState;
        bool loadAttempted = false;
        bool loaded = false;
        bool loadedFromSaved = false;
        bool paneHeightsApplied = false;
        bool restoreLogWritten = false;
        bool dirty = false;
        std::string loadError;
        std::string lastObservedJson;
        std::string lastSavedJson;
        M90Clock::time_point changedAt{};
    };

    M90WorkspaceRuntime& M90Workspace()
    {
        static M90WorkspaceRuntime runtime;
        return runtime;
    }

    const std::filesystem::path& M90ExecutableDirectory()
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

    const std::string& M90SavedWorkspacePath()
    {
        static const std::string path =
            (M90ExecutableDirectory() /
             L"data" /
             L"chart_workspace.json").string();
        return path;
    }

    const std::string& M90DefaultWorkspacePath()
    {
        static const std::string path =
            (M90ExecutableDirectory() /
             L"config" /
             L"chart_workspace.default.json").string();
        return path;
    }

    bool M90LoadWorkspace()
    {
        M90WorkspaceRuntime& runtime = M90Workspace();
        if (runtime.loadAttempted) return runtime.loaded;
        runtime.loadAttempted = true;

        bool found = false;
        std::string savedError;
        trading::app::ChartWorkspacePersistenceState state;
        if (trading::app::LoadChartWorkspaceState(
                M90SavedWorkspacePath(),
                state,
                found,
                savedError) &&
            found)
        {
            runtime.loadedState = std::move(state);
            runtime.loaded = true;
            runtime.loadedFromSaved = true;
        }
        else {
            bool defaultFound = false;
            std::string defaultError;
            trading::app::ChartWorkspacePersistenceState defaults;
            if (trading::app::LoadChartWorkspaceState(
                    M90DefaultWorkspacePath(),
                    defaults,
                    defaultFound,
                    defaultError) &&
                defaultFound)
            {
                runtime.loadedState = std::move(defaults);
                runtime.loaded = true;
                runtime.loadedFromSaved = false;
                if (!savedError.empty()) {
                    runtime.loadError =
                        "저장 작업공간을 사용하지 못해 기본 JSON을 적용했습니다: " +
                        savedError;
                }
            }
            else {
                runtime.loadError =
                    "저장 JSON=" +
                    (savedError.empty() ? std::string("없음") : savedError) +
                    " / 기본 JSON=" +
                    (defaultError.empty() ? std::string("없음") : defaultError);
                return false;
            }
        }

        std::string serialized;
        std::string serializeError;
        if (!trading::app::SerializeChartWorkspaceState(
                runtime.loadedState,
                serialized,
                serializeError))
        {
            runtime.loaded = false;
            runtime.loadError = serializeError;
            return false;
        }
        runtime.lastObservedJson = serialized;
        runtime.lastSavedJson = runtime.loadedFromSaved
            ? serialized
            : std::string{};
        return true;
    }

    void M90WriteRestoreLog()
    {
        M90WorkspaceRuntime& runtime = M90Workspace();
        if (runtime.restoreLogWritten) return;
        runtime.restoreLogWritten = true;

        if (!runtime.loaded) {
            g_log.Add(
                "FAULT",
                "차트 작업공간 JSON 초기화 실패: %s",
                runtime.loadError.c_str());
            return;
        }
        if (!runtime.loadError.empty()) {
            g_log.Add("FAULT", "%s", runtime.loadError.c_str());
        }
        g_log.Add(
            "SYS",
            "차트 작업공간 JSON 적용: %s, 지표 %zu개, 패널 높이 %zu개",
            runtime.loadedFromSaved ? "직전 저장값" : "기본값",
            runtime.loadedState.indicators.size(),
            runtime.loadedState.paneHeightWeights.size());
    }

    trading::app::ChartWorkspacePersistenceState M90CurrentWorkspace()
    {
        trading::app::ChartWorkspacePersistenceState state;
        state.indicators = g_indicatorDefinitions;
        state.paneHeightWeights = g_mainRenderSurface.paneHeightWeights;
        return state;
    }

    void M90SaveWorkspaceNow(const char* reason)
    {
        M90WorkspaceRuntime& runtime = M90Workspace();
        if (!runtime.loaded) return;

        const trading::app::ChartWorkspacePersistenceState state =
            M90CurrentWorkspace();
        std::string serialized;
        std::string error;
        if (!trading::app::SerializeChartWorkspaceState(
                state,
                serialized,
                error))
        {
            g_log.Add(
                "FAULT",
                "차트 작업공간 JSON 직렬화 실패: %s",
                error.c_str());
            return;
        }
        if (serialized == runtime.lastSavedJson) {
            runtime.lastObservedJson = serialized;
            runtime.dirty = false;
            return;
        }
        if (!trading::app::SaveChartWorkspaceState(
                M90SavedWorkspacePath(),
                state,
                error))
        {
            g_log.Add(
                "FAULT",
                "차트 작업공간 JSON 저장 실패: %s",
                error.c_str());
            return;
        }

        runtime.loadedState = state;
        runtime.loadedFromSaved = true;
        runtime.lastObservedJson = serialized;
        runtime.lastSavedJson = serialized;
        runtime.dirty = false;
        g_log.Add(
            "SYS",
            "차트 작업공간 JSON 저장(%s): 지표 %zu개, 패널 높이 %zu개",
            reason != nullptr ? reason : "변경",
            state.indicators.size(),
            state.paneHeightWeights.size());
    }

    void M90PersistencePump(const char* reason)
    {
        M90WorkspaceRuntime& runtime = M90Workspace();
        if (!runtime.loaded) return;

        const trading::app::ChartWorkspacePersistenceState state =
            M90CurrentWorkspace();
        std::string serialized;
        std::string error;
        if (!trading::app::SerializeChartWorkspaceState(
                state,
                serialized,
                error))
        {
            return;
        }

        const M90Clock::time_point now = M90Clock::now();
        if (serialized != runtime.lastObservedJson) {
            runtime.lastObservedJson = serialized;
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
            M90SaveWorkspaceNow(reason);
        }
        else {
            WakeFrames(4);
        }
    }

    void M90ApplyLoadedPaneHeights(
        const trading::render::RenderDocument& document,
        trading::ui::RenderSurfaceState& surfaceState)
    {
        M90WorkspaceRuntime& runtime = M90Workspace();
        if (!runtime.loaded || runtime.paneHeightsApplied) return;

        std::size_t applied = 0U;
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
            ++applied;
        }
        runtime.paneHeightsApplied = true;
        surfaceState.dirty = true;
        g_log.Add(
            "SYS",
            "차트 패널 높이 JSON 적용: %zu개",
            applied);
    }

    bool M90ShiftDateText(int days)
    {
        if (std::strlen(g_m85AnchorDate) != 10U) return false;
        std::tm date{};
        if (std::sscanf(
                g_m85AnchorDate,
                "%d-%d-%d",
                &date.tm_year,
                &date.tm_mon,
                &date.tm_mday) != 3)
        {
            return false;
        }
        date.tm_year -= 1900;
        date.tm_mon -= 1;
        date.tm_hour = 12;
        date.tm_mday += days;
        const std::time_t shifted = std::mktime(&date);
        if (shifted == static_cast<std::time_t>(-1)) return false;
#if defined(_WIN32)
        localtime_s(&date, &shifted);
#else
        localtime_r(&shifted, &date);
#endif
        std::snprintf(
            g_m85AnchorDate,
            sizeof(g_m85AnchorDate),
            "%04d-%02d-%02d",
            date.tm_year + 1900,
            date.tm_mon + 1,
            date.tm_mday);
        return true;
    }

    void M90SetTodayText()
    {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        std::snprintf(
            g_m85AnchorDate,
            sizeof(g_m85AnchorDate),
            "%04d-%02d-%02d",
            local.tm_year + 1900,
            local.tm_mon + 1,
            local.tm_mday);
    }
}

std::vector<trading::app::IndicatorInstanceDefinition>
trading::app::M90InitialIndicatorDefinitions()
{
    M90LoadWorkspace();
    M90WriteRestoreLog();
    M90WorkspaceRuntime& runtime = M90Workspace();
    if (!runtime.loaded) {
        return {};
    }
    return runtime.loadedState.indicators;
}

void trading::ui::M90DrawRenderDocument(
    const render::RenderDocument& document,
    ImVec2 size,
    RenderSurfaceState& surfaceState)
{
    M90ApplyLoadedPaneHeights(document, surfaceState);
    trading::ui::DrawRenderDocument(document, size, surfaceState);
    M90PersistencePump("패널");
}

void trading::ui::M90DrawIndicatorManagerWindow(
    std::vector<app::IndicatorInstanceDefinition>& definitions,
    IndicatorManagerUiState& state,
    ApplyIndicatorDefinitions applyDefinitions)
{
    trading::ui::DrawIndicatorManagerWindow(
        definitions,
        state,
        applyDefinitions);
    M90PersistencePump("지표");
}

bool ImGui::M90SmallButton(const char* label)
{
    if (label == nullptr) return ImGui::SmallButton(label);

    if (std::strcmp(label, "이동##date_m87") == 0) {
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        return false;
    }
    if (std::strcmp(label, "<##date_m87") == 0) {
        const bool pressed = ImGui::SmallButton(label);
        if (pressed) M90ShiftDateText(-1);
        return false;
    }
    if (std::strcmp(label, ">##date_m87") == 0) {
        const bool pressed = ImGui::SmallButton(label);
        if (pressed) M90ShiftDateText(1);
        return false;
    }
    if (std::strcmp(label, "오늘/최신##date_m87") == 0) {
        const bool pressed = ImGui::SmallButton(label);
        if (pressed) M90SetTodayText();
        return false;
    }

    return ImGui::SmallButton(label);
}
