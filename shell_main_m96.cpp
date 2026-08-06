#include "app/comparison_workspace_store.h"
#include "ui/comparison_manager_ui.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace trading::ui
{
    void M96DrawComparisonManagerWindow(
        std::vector<app::ComparisonDefinition>& definitions,
        const app::ComparisonModuleSnapshot& snapshot,
        const std::vector<SymbolCatalogEntry>& symbolCatalog,
        ComparisonManagerUiState& state,
        ApplyComparisonDefinitions applyDefinitions,
        RequestComparisonData requestData,
        RefreshSymbolCatalog refreshCatalog);
}

#define DrawComparisonManagerWindow M96DrawComparisonManagerWindow
#include "shell_main_m95.cpp"
#undef DrawComparisonManagerWindow

namespace
{
    using M96Clock = std::chrono::steady_clock;

    struct M96ComparisonStoreRuntime final
    {
        bool loadAttempted = false;
        bool loaded = false;
        bool paneDirty = false;
        trading::app::ComparisonWorkspaceSource source =
            trading::app::ComparisonWorkspaceSource::None;
        trading::app::ComparisonWorkspaceState authoritativeState;
        std::string savedPath;
        std::string defaultPath;
        std::string lastPersistedJson;
        std::string lastObservedPaneJson;
        M96Clock::time_point paneChangedAt{};
        M96Clock::time_point lastRequestAt{};
        std::uint64_t primaryCompletedRevision = 0;
        std::set<std::string> requestedIds;
    };

    M96ComparisonStoreRuntime& M96Runtime()
    {
        static M96ComparisonStoreRuntime runtime;
        return runtime;
    }

    trading::ui::ApplyComparisonDefinitions g_m96BaseApply = nullptr;

    const std::filesystem::path& M96ExecutableDirectory()
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

    void M96ResolvePaths(M96ComparisonStoreRuntime& runtime)
    {
        if (!runtime.savedPath.empty()) return;
        const std::filesystem::path root = M96ExecutableDirectory();
        runtime.savedPath =
            (root / L"data" / L"comparison_workspace.json").string();
        runtime.defaultPath =
            (root / L"config" /
             L"comparison_workspace.default.json").string();
    }

    std::map<std::string, float> M96CurrentPaneHeights(
        const std::vector<trading::app::ComparisonDefinition>& definitions)
    {
        std::map<std::string, float> result;
        for (const auto& definition : definitions) {
            if (definition.placement !=
                    trading::app::ComparisonPlacement::SeparatePane ||
                definition.paneId.empty())
            {
                continue;
            }
            const auto found =
                g_mainRenderSurface.paneHeightWeights.find(definition.paneId);
            result[definition.paneId] =
                found == g_mainRenderSurface.paneHeightWeights.end()
                    ? definition.paneHeightWeight
                    : found->second;
        }
        return result;
    }

    std::string M96Summary(
        const std::vector<trading::app::ComparisonDefinition>& definitions)
    {
        std::ostringstream stream;
        bool first = true;
        for (const auto& definition : definitions) {
            if (!first) stream << ',';
            first = false;
            stream << definition.id << '=' << definition.code << ':'
                   << (definition.visible ? "Visible" : "Off") << ':'
                   << static_cast<int>(definition.valueMode);
        }
        return first ? std::string("없음") : stream.str();
    }

    void M96ApplyPaneHeights(
        const trading::app::ComparisonWorkspaceState& state)
    {
        for (const auto& pane : state.paneHeightWeights) {
            g_mainRenderSurface.paneHeightWeights[pane.first] = pane.second;
        }
        g_mainRenderSurface.paneDefaultHeightWeights.clear();
        g_mainRenderSurface.dirty = true;
    }

    bool M96LoadWorkspace()
    {
        M96ComparisonStoreRuntime& runtime = M96Runtime();
        if (runtime.loadAttempted) return runtime.loaded;
        runtime.loadAttempted = true;
        M96ResolvePaths(runtime);

        std::string diagnostic;
        if (!trading::app::LoadVerifiedComparisonWorkspace(
                runtime.savedPath,
                runtime.defaultPath,
                runtime.authoritativeState,
                runtime.source,
                diagnostic))
        {
            g_log.Add(
                "FAULT",
                "비교 저장소 초기화 실패: %s | path=%s",
                diagnostic.c_str(),
                runtime.savedPath.c_str());
            return false;
        }
        if (g_m96BaseApply == nullptr) {
            g_log.Add("FAULT", "비교 저장소 적용 함수가 없습니다.");
            return false;
        }

        std::string applyError;
        if (!g_m96BaseApply(
                runtime.authoritativeState.definitions,
                applyError))
        {
            g_log.Add(
                "FAULT",
                "저장된 비교 구성을 적용하지 못했습니다: %s",
                applyError.c_str());
            return false;
        }

        g_comparisonDefinitions = runtime.authoritativeState.definitions;
        g_comparisonManagerUi = {};
        M96ApplyPaneHeights(runtime.authoritativeState);

        std::string serialized;
        std::string error;
        if (!trading::app::SerializeComparisonWorkspaceState(
                runtime.authoritativeState,
                serialized,
                error))
        {
            g_log.Add(
                "FAULT",
                "비교 저장소 초기 직렬화 실패: %s",
                error.c_str());
            return false;
        }

        runtime.lastPersistedJson = serialized;
        runtime.lastObservedPaneJson = serialized;
        runtime.loaded = true;
        if (!diagnostic.empty()) g_log.Add("SYS", "%s", diagnostic.c_str());
        g_log.Add(
            "SYS",
            "비교 저장소 적용: %s | definitions=%s | path=%s",
            trading::app::ComparisonWorkspaceSourceName(runtime.source),
            M96Summary(runtime.authoritativeState.definitions).c_str(),
            runtime.savedPath.c_str());
        return true;
    }

    bool M96Persist(
        const trading::app::ComparisonWorkspaceState& requested,
        const char* reason,
        std::string& error)
    {
        M96ComparisonStoreRuntime& runtime = M96Runtime();
        if (!runtime.loaded) {
            error = "비교 저장소가 초기화되지 않았습니다.";
            return false;
        }

        trading::app::ComparisonWorkspaceState candidate = requested;
        if (!trading::app::ValidateComparisonWorkspaceState(candidate, error)) {
            return false;
        }

        std::string serialized;
        if (!trading::app::SerializeComparisonWorkspaceState(
                candidate,
                serialized,
                error))
        {
            return false;
        }
        if (serialized == runtime.lastPersistedJson) {
            runtime.authoritativeState = std::move(candidate);
            runtime.lastObservedPaneJson = serialized;
            runtime.paneDirty = false;
            error.clear();
            return true;
        }

        if (!trading::app::SaveVerifiedComparisonWorkspace(
                runtime.savedPath,
                candidate,
                error))
        {
            return false;
        }

        runtime.authoritativeState = std::move(candidate);
        runtime.source = trading::app::ComparisonWorkspaceSource::Saved;
        runtime.lastPersistedJson = serialized;
        runtime.lastObservedPaneJson = serialized;
        runtime.paneDirty = false;
        runtime.requestedIds.clear();
        g_log.Add(
            "SYS",
            "비교 저장소 커밋(%s): definitions=%s | path=%s | readback=OK",
            reason != nullptr ? reason : "변경",
            M96Summary(runtime.authoritativeState.definitions).c_str(),
            runtime.savedPath.c_str());
        error.clear();
        return true;
    }

    void M96RemoveObsoletePaneState(
        const std::vector<trading::app::ComparisonDefinition>& previous,
        const std::vector<trading::app::ComparisonDefinition>& next)
    {
        std::set<std::string> retained;
        for (const auto& definition : next) {
            if (!definition.paneId.empty()) retained.insert(definition.paneId);
        }
        for (const auto& definition : previous) {
            if (!definition.paneId.empty() &&
                retained.find(definition.paneId) == retained.end())
            {
                g_mainRenderSurface.paneHeightWeights.erase(definition.paneId);
                g_mainRenderSurface.paneDefaultHeightWeights.erase(
                    definition.paneId);
            }
        }
    }

    bool M96ApplyAndCommit(
        const std::vector<trading::app::ComparisonDefinition>& requested,
        std::string& error)
    {
        if (g_m96BaseApply == nullptr) {
            error = "비교 구성 적용 함수가 없습니다.";
            return false;
        }

        M96ComparisonStoreRuntime& runtime = M96Runtime();
        const trading::app::ComparisonWorkspaceState previous =
            runtime.authoritativeState;
        trading::app::ComparisonWorkspaceState next = previous;
        next.definitions = requested;
        next.paneHeightWeights = M96CurrentPaneHeights(requested);

        if (!trading::app::ValidateComparisonWorkspaceState(next, error)) {
            return false;
        }
        if (!g_m96BaseApply(next.definitions, error)) return false;

        std::string saveError;
        if (!M96Persist(next, "비교 적용", saveError)) {
            std::string rollbackError;
            if (!g_m96BaseApply(previous.definitions, rollbackError)) {
                g_log.Add(
                    "FAULT",
                    "비교 저장 실패 후 런타임 롤백 실패: %s",
                    rollbackError.c_str());
            }
            error = "비교 저장소 커밋 실패: " + saveError;
            return false;
        }

        M96RemoveObsoletePaneState(previous.definitions, next.definitions);
        error.clear();
        return true;
    }

    void M96PersistPaneHeights()
    {
        M96ComparisonStoreRuntime& runtime = M96Runtime();
        if (!runtime.loaded) return;

        trading::app::ComparisonWorkspaceState candidate =
            runtime.authoritativeState;
        candidate.paneHeightWeights =
            M96CurrentPaneHeights(candidate.definitions);

        std::string serialized;
        std::string error;
        if (!trading::app::SerializeComparisonWorkspaceState(
                candidate,
                serialized,
                error))
        {
            return;
        }

        const M96Clock::time_point now = M96Clock::now();
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
            if (!M96Persist(candidate, "비교 패널 높이", error)) {
                g_log.Add(
                    "FAULT",
                    "비교 패널 높이 저장 실패: %s",
                    error.c_str());
            }
        }
        else {
            WakeFrames(4);
        }
    }

    void M96RequestVisibleComparisonsForPrimary()
    {
        M96ComparisonStoreRuntime& runtime = M96Runtime();
        if (!runtime.loaded || !g_runtimeRunner ||
            !g_runtimeRunner->IsRunning())
        {
            return;
        }

        const trading::app::MarketDataSeriesSnapshot primary =
            g_marketDataModule.SeriesSnapshot();
        if (primary.state != trading::app::MarketDataState::Ready ||
            primary.minuteUnit <= 0 || primary.code.empty())
        {
            return;
        }

        if (runtime.primaryCompletedRevision != primary.completedRevision) {
            runtime.primaryCompletedRevision = primary.completedRevision;
            runtime.requestedIds.clear();
        }

        const auto now = M96Clock::now();
        if (now - runtime.lastRequestAt < std::chrono::milliseconds(1200)) {
            return;
        }

        for (const auto& definition : runtime.authoritativeState.definitions) {
            if (!definition.visible ||
                runtime.requestedIds.find(definition.id) !=
                    runtime.requestedIds.end())
            {
                continue;
            }

            std::string error;
            runtime.lastRequestAt = now;
            if (!RequestComparisonData(definition.id, error)) {
                g_log.Add(
                    "FAULT",
                    "비교 동기 조회 실패 %s: %s",
                    definition.code.c_str(),
                    error.c_str());
                return;
            }
            runtime.requestedIds.insert(definition.id);
            g_log.Add(
                "DATA",
                "비교 동기 조회: %s %d분",
                definition.code.c_str(),
                primary.minuteUnit);
            return;
        }
    }
}

void trading::ui::M96DrawComparisonManagerWindow(
    std::vector<app::ComparisonDefinition>& definitions,
    const app::ComparisonModuleSnapshot& snapshot,
    const std::vector<SymbolCatalogEntry>& symbolCatalog,
    ComparisonManagerUiState& state,
    ApplyComparisonDefinitions applyDefinitions,
    RequestComparisonData requestData,
    RefreshSymbolCatalog refreshCatalog)
{
    g_m96BaseApply = applyDefinitions;
    M96LoadWorkspace();

    trading::ui::DrawComparisonManagerWindow(
        definitions,
        snapshot,
        symbolCatalog,
        state,
        M96ApplyAndCommit,
        requestData,
        refreshCatalog);

    M96PersistPaneHeights();
    M96RequestVisibleComparisonsForPrimary();
}
