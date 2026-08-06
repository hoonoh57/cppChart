#include "../app/indicator_workspace_store.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    int failures = 0;

    void Expect(bool condition, const char* message)
    {
        if (condition) return;
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }

    trading::app::IndicatorInstanceDefinition* FindByType(
        trading::app::IndicatorWorkspaceState& state,
        const std::string& type)
    {
        for (auto& definition : state.indicators) {
            if (definition.spec.type == type) return &definition;
        }
        return nullptr;
    }

    std::size_t VisibleCount(
        const trading::app::IndicatorWorkspaceState& state)
    {
        std::size_t count = 0U;
        for (const auto& definition : state.indicators) {
            if (definition.visible) ++count;
        }
        return count;
    }
}

int main()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "cppchart_indicator_workspace_roundtrip";
    const std::filesystem::path saved = root / "indicator_workspace.json";
    const std::filesystem::path defaults =
        std::filesystem::current_path() /
        "config" /
        "indicator_workspace.default.json";

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    std::filesystem::create_directories(root, cleanupError);

    trading::app::IndicatorWorkspaceState initial;
    trading::app::IndicatorWorkspaceSource initialSource =
        trading::app::IndicatorWorkspaceSource::None;
    std::string diagnostic;
    const bool loadedDefault = trading::app::LoadVerifiedIndicatorWorkspace(
        saved.string(),
        defaults.string(),
        initial,
        initialSource,
        diagnostic);
    Expect(loadedDefault, "default workspace must load");
    Expect(
        initialSource == trading::app::IndicatorWorkspaceSource::Default,
        "first load must use default JSON");
    Expect(VisibleCount(initial) == 1U, "default must show only SMA");

    auto* ema = FindByType(initial, "EMA");
    Expect(ema != nullptr, "EMA definition must exist");
    if (ema != nullptr) ema->visible = true;
    Expect(VisibleCount(initial) == 2U, "SMA and EMA must be visible before save");

    std::string saveError;
    const bool savedOk = trading::app::SaveVerifiedIndicatorWorkspace(
        saved.string(),
        initial,
        saveError);
    Expect(savedOk, "verified workspace save must succeed");
    Expect(std::filesystem::exists(saved), "saved JSON must exist");

    trading::app::IndicatorWorkspaceState restarted;
    trading::app::IndicatorWorkspaceSource restartedSource =
        trading::app::IndicatorWorkspaceSource::None;
    diagnostic.clear();
    const bool restartedOk = trading::app::LoadVerifiedIndicatorWorkspace(
        saved.string(),
        defaults.string(),
        restarted,
        restartedSource,
        diagnostic);
    Expect(restartedOk, "saved workspace must reload");
    Expect(
        restartedSource == trading::app::IndicatorWorkspaceSource::Saved,
        "restart must select saved JSON");
    Expect(VisibleCount(restarted) == 2U, "restart must restore SMA and EMA");
    const auto* restartedEma = FindByType(restarted, "EMA");
    Expect(
        restartedEma != nullptr && restartedEma->visible,
        "EMA visible state must survive restart");

    // Create a last-known-good backup by committing another valid state.
    auto* rsi = FindByType(restarted, "RSI");
    Expect(rsi != nullptr, "RSI definition must exist");
    if (rsi != nullptr) rsi->visible = true;
    saveError.clear();
    Expect(
        trading::app::SaveVerifiedIndicatorWorkspace(
            saved.string(), restarted, saveError),
        "second verified save must create a backup");
    Expect(
        std::filesystem::exists(saved.string() + ".bak"),
        "last-known-good backup must exist");

    // Corrupt the primary file. The loader must use the backup, never defaults.
    {
        std::ofstream output(saved, std::ios::binary | std::ios::trunc);
        output << "{broken-json";
    }
    trading::app::IndicatorWorkspaceState recovered;
    trading::app::IndicatorWorkspaceSource recoveredSource =
        trading::app::IndicatorWorkspaceSource::None;
    diagnostic.clear();
    const bool recoveredOk = trading::app::LoadVerifiedIndicatorWorkspace(
        saved.string(),
        defaults.string(),
        recovered,
        recoveredSource,
        diagnostic);
    Expect(recoveredOk, "corrupt primary must recover from backup");
    Expect(
        recoveredSource == trading::app::IndicatorWorkspaceSource::Saved,
        "backup recovery must still be treated as saved state");
    Expect(
        VisibleCount(recovered) == 2U,
        "backup recovery must preserve prior SMA and EMA, not defaults");

    std::filesystem::remove_all(root, cleanupError);
    if (failures != 0) {
        std::cerr << failures << " indicator workspace test(s) failed\n";
        return 1;
    }
    std::cout << "indicator workspace restart roundtrip passed\n";
    return 0;
}
