#include "../app/indicator_workspace_store.h"

#include <cmath>
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

    void ExpectNear(
        double actual,
        double expected,
        double tolerance,
        const char* message)
    {
        Expect(std::fabs(actual - expected) <= tolerance, message);
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

    const trading::app::IndicatorInstanceDefinition* FindByType(
        const trading::app::IndicatorWorkspaceState& state,
        const std::string& type)
    {
        for (const auto& definition : state.indicators) {
            if (definition.spec.type == type) return &definition;
        }
        return nullptr;
    }

    trading::app::IndicatorOutputBinding* FindOutput(
        trading::app::IndicatorInstanceDefinition& definition,
        const std::string& label)
    {
        for (auto& output : definition.outputs) {
            if (output.label == label) return &output;
        }
        return nullptr;
    }

    const trading::app::IndicatorOutputBinding* FindOutput(
        const trading::app::IndicatorInstanceDefinition& definition,
        const std::string& label)
    {
        for (const auto& output : definition.outputs) {
            if (output.label == label) return &output;
        }
        return nullptr;
    }

    const trading::app::IndicatorReferenceBinding* FindReference(
        const trading::app::IndicatorInstanceDefinition& definition,
        const std::string& id)
    {
        for (const auto& reference : definition.references) {
            if (reference.referenceId == id) return &reference;
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

    bool SameColor(
        const trading::render::ColorRgba& actual,
        const trading::render::ColorRgba& expected)
    {
        return actual.red == expected.red &&
            actual.green == expected.green &&
            actual.blue == expected.blue &&
            actual.alpha == expected.alpha;
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
    if (!loadedDefault) std::cerr << "loadDefaultError: " << diagnostic << '\n';
    Expect(loadedDefault, "default workspace must load");
    Expect(
        initialSource == trading::app::IndicatorWorkspaceSource::Default,
        "first load must use default JSON");
    Expect(VisibleCount(initial) == 1U, "default must show only SMA");

    auto* ema = FindByType(initial, "EMA");
    Expect(ema != nullptr, "EMA definition must exist");
    if (ema != nullptr) ema->visible = true;

    auto* superTrend = FindByType(initial, "SUPERTREND");
    Expect(superTrend != nullptr, "SuperTrend definition must exist");
    if (superTrend != nullptr) {
        superTrend->visible = true;
        superTrend->spec.parameters["multiplier"] = 3.0;
        auto* down = FindOutput(*superTrend, "Down");
        Expect(down != nullptr, "SuperTrend Down output must exist");
        if (down != nullptr) {
            down->visible = true;
            down->paneId = "indicator.supertrend.test.pane";
            down->paneTitle = "SuperTrend Test";
            down->paneHeightWeight = 0.47f;
            down->paneValueScale = trading::render::PaneValueScale::Fixed;
            down->fixedMinimum = 100.25;
            down->fixedMaximum = 900.75;
            down->valueDecimals = 3;
            down->primaryColor = { 17, 34, 51, 221 };
            down->width = 3.5f;
            down->style = trading::render::LineStyle::Dotted;
        }
    }

    auto* macd = FindByType(initial, "MACD");
    Expect(macd != nullptr, "MACD definition must exist");
    if (macd != nullptr) {
        auto* histogram = FindOutput(*macd, "Histogram");
        Expect(histogram != nullptr, "MACD histogram output must exist");
        if (histogram != nullptr) {
            histogram->primaryColor = { 10, 20, 30, 210 };
            histogram->secondaryColor = { 200, 150, 100, 190 };
            histogram->visible = false;
        }
    }

    auto* rsi = FindByType(initial, "RSI");
    Expect(rsi != nullptr, "RSI definition must exist");
    if (rsi != nullptr) {
        Expect(rsi->references.size() >= 2U, "RSI default references must exist");
        if (!rsi->references.empty()) {
            rsi->references.erase(rsi->references.begin());
        }

        trading::app::IndicatorReferenceBinding custom;
        custom.indicatorId = rsi->spec.id;
        custom.referenceId = "indicator.rsi.14.reference.user.test";
        custom.paneId = "indicator.rsi.custom.pane";
        custom.paneTitle = "RSI Custom";
        custom.paneHeightWeight = 0.63f;
        custom.paneValueScale = trading::render::PaneValueScale::Fixed;
        custom.fixedMinimum = -10.0;
        custom.fixedMaximum = 110.0;
        custom.valueDecimals = 4;
        custom.label = "사용자 기준선";
        custom.value = 55.5;
        custom.color = { 91, 81, 71, 201 };
        custom.width = 2.5f;
        custom.style = trading::render::LineStyle::Dashed;
        custom.visible = false;
        rsi->references.push_back(custom);
    }

    Expect(
        VisibleCount(initial) == 3U,
        "SMA, EMA and SuperTrend must be visible before save");

    std::string saveError;
    const bool savedOk = trading::app::SaveVerifiedIndicatorWorkspace(
        saved.string(),
        initial,
        saveError);
    if (!savedOk) std::cerr << "saveError: " << saveError << '\n';
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
    if (!restartedOk) std::cerr << "restartError: " << diagnostic << '\n';
    Expect(restartedOk, "saved workspace must reload");
    Expect(
        restartedSource == trading::app::IndicatorWorkspaceSource::Saved,
        "restart must select saved JSON");
    Expect(
        VisibleCount(restarted) == 3U,
        "restart must restore SMA, EMA and SuperTrend");

    const auto* restartedSuperTrend = FindByType(restarted, "SUPERTREND");
    Expect(restartedSuperTrend != nullptr, "restarted SuperTrend must exist");
    if (restartedSuperTrend != nullptr) {
        const auto parameter =
            restartedSuperTrend->spec.parameters.find("multiplier");
        Expect(
            parameter != restartedSuperTrend->spec.parameters.end(),
            "SuperTrend multiplier must survive restart");
        if (parameter != restartedSuperTrend->spec.parameters.end()) {
            ExpectNear(
                parameter->second,
                3.0,
                1e-12,
                "SuperTrend multiplier value must survive restart");
        }

        const auto* down = FindOutput(*restartedSuperTrend, "Down");
        Expect(down != nullptr, "restarted SuperTrend Down must exist");
        if (down != nullptr) {
            Expect(
                down->style == trading::render::LineStyle::Dotted,
                "SuperTrend Down dotted style must survive restart");
            ExpectNear(down->width, 3.5, 1e-6, "line width must survive restart");
            Expect(
                SameColor(down->primaryColor, { 17, 34, 51, 221 }),
                "line color must survive restart");
            Expect(
                down->paneId == "indicator.supertrend.test.pane" &&
                down->paneTitle == "SuperTrend Test",
                "output pane selection must survive restart");
            ExpectNear(
                down->paneHeightWeight,
                0.47,
                1e-6,
                "pane height must survive restart");
            Expect(
                down->paneValueScale == trading::render::PaneValueScale::Fixed,
                "pane scale must survive restart");
            ExpectNear(
                down->fixedMinimum,
                100.25,
                1e-12,
                "fixed minimum must survive restart");
            ExpectNear(
                down->fixedMaximum,
                900.75,
                1e-12,
                "fixed maximum must survive restart");
            Expect(
                down->valueDecimals == 3,
                "value decimals must survive restart");
        }
    }

    const auto* restartedMacd = FindByType(restarted, "MACD");
    Expect(restartedMacd != nullptr, "restarted MACD must exist");
    if (restartedMacd != nullptr) {
        const auto* histogram = FindOutput(*restartedMacd, "Histogram");
        Expect(histogram != nullptr, "restarted MACD histogram must exist");
        if (histogram != nullptr) {
            Expect(
                SameColor(histogram->primaryColor, { 10, 20, 30, 210 }),
                "histogram positive color must survive restart");
            Expect(
                SameColor(histogram->secondaryColor, { 200, 150, 100, 190 }),
                "histogram negative color must survive restart");
            Expect(
                !histogram->visible,
                "histogram visibility must survive restart");
        }
    }

    const auto* restartedRsi = FindByType(restarted, "RSI");
    Expect(restartedRsi != nullptr, "restarted RSI must exist");
    if (restartedRsi != nullptr) {
        Expect(
            restartedRsi->references.size() == 2U,
            "reference deletion and addition must survive restart");
        const auto* custom = FindReference(
            *restartedRsi,
            "indicator.rsi.14.reference.user.test");
        Expect(custom != nullptr, "custom reference must survive restart");
        if (custom != nullptr) {
            Expect(
                custom->label == "사용자 기준선",
                "reference label must survive restart");
            ExpectNear(
                custom->value,
                55.5,
                1e-12,
                "reference value must survive restart");
            Expect(
                custom->paneId == "indicator.rsi.custom.pane" &&
                custom->paneTitle == "RSI Custom",
                "reference pane must survive restart");
            ExpectNear(
                custom->paneHeightWeight,
                0.63,
                1e-6,
                "reference pane height must survive restart");
            Expect(
                custom->paneValueScale == trading::render::PaneValueScale::Fixed,
                "reference pane scale must survive restart");
            ExpectNear(
                custom->fixedMinimum,
                -10.0,
                1e-12,
                "reference fixed minimum must survive restart");
            ExpectNear(
                custom->fixedMaximum,
                110.0,
                1e-12,
                "reference fixed maximum must survive restart");
            Expect(
                custom->valueDecimals == 4,
                "reference decimals must survive restart");
            Expect(
                SameColor(custom->color, { 91, 81, 71, 201 }),
                "reference color must survive restart");
            ExpectNear(
                custom->width,
                2.5,
                1e-6,
                "reference width must survive restart");
            Expect(
                custom->style == trading::render::LineStyle::Dashed,
                "reference style must survive restart");
            Expect(
                !custom->visible,
                "reference visibility must survive restart");
        }
    }

    auto* restartedRsiMutable = FindByType(restarted, "RSI");
    if (restartedRsiMutable != nullptr) restartedRsiMutable->visible = true;
    saveError.clear();
    const bool secondSaved = trading::app::SaveVerifiedIndicatorWorkspace(
        saved.string(), restarted, saveError);
    if (!secondSaved) std::cerr << "secondSaveError: " << saveError << '\n';
    Expect(secondSaved, "second verified save must create a backup");
    Expect(
        std::filesystem::exists(saved.string() + ".bak"),
        "last-known-good backup must exist");

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
    if (!recoveredOk) std::cerr << "recoveryError: " << diagnostic << '\n';
    Expect(recoveredOk, "corrupt primary must recover from backup");
    Expect(
        recoveredSource == trading::app::IndicatorWorkspaceSource::Saved,
        "backup recovery must still be treated as saved state");
    Expect(
        VisibleCount(recovered) == 3U,
        "backup recovery must preserve prior complete state, not defaults");

    const auto* recoveredSuperTrend = FindByType(recovered, "SUPERTREND");
    const auto* recoveredDown = recoveredSuperTrend != nullptr
        ? FindOutput(*recoveredSuperTrend, "Down")
        : nullptr;
    Expect(
        recoveredDown != nullptr &&
        recoveredDown->style == trading::render::LineStyle::Dotted,
        "backup recovery must preserve SuperTrend Down dotted style");

    std::filesystem::remove_all(root, cleanupError);
    if (failures != 0) {
        std::cerr << failures << " indicator workspace test(s) failed\n";
        return 1;
    }
    std::cout << "indicator workspace complete-property restart roundtrip passed\n";
    return 0;
}
