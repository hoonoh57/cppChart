#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "../core/preopen_entry_model.h"

namespace
{
    using trading::stock_pool::Bar;
    using trading::stock_pool::preopen::AnalyzePreOpenStructure;
    using trading::stock_pool::preopen::GapEntryConfig;
    using trading::stock_pool::preopen::GapEntryPhase;
    using trading::stock_pool::preopen::GapEntryState;
    using trading::stock_pool::preopen::StructureConfig;
    using trading::stock_pool::preopen::StructureDecision;

    void Require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    Bar MakeBar(double open, double high, double low, double close)
    {
        Bar bar;
        bar.open = open;
        bar.high = high;
        bar.low = low;
        bar.close = close;
        return bar;
    }

    std::vector<Bar> BuildDaily(double resistance)
    {
        std::vector<Bar> bars;
        for (int index = 0; index < 30; ++index) {
            const double center = 92.0 + index * 0.20;
            bars.push_back(MakeBar(center, center + 1.0, center - 1.0, center + 0.2));
        }
        bars.back().high = resistance;
        return bars;
    }

    std::vector<Bar> BuildMediumUp()
    {
        std::vector<Bar> bars;
        for (int index = 0; index < 16; ++index) {
            const double center = 94.0 + index * 0.35;
            bars.push_back(MakeBar(center, center + 0.8, center - 0.7, center + 0.3));
        }
        return bars;
    }

    std::vector<Bar> BuildMediumDown()
    {
        std::vector<Bar> bars;
        for (int index = 0; index < 16; ++index) {
            const double center = 104.0 - index * 0.45;
            bars.push_back(MakeBar(center, center + 0.6, center - 0.8, center - 0.2));
        }
        return bars;
    }
}

int main()
{
    StructureConfig config;
    config.dailyResistanceLookback = 30U;
    config.mediumTrendLookback = 12U;
    config.mediumStopLookback = 6U;
    config.minimumStructuralRewardRisk = 1.5;

    {
        const auto result = AnalyzePreOpenStructure(
            BuildDaily(110.0),
            BuildMediumDown(),
            100.0,
            config);
        Require(result.dataReady, "downtrend case must have enough data");
        Require(result.mediumDowntrend, "falling medium structure must be detected");
        Require(result.decision == StructureDecision::RejectMediumDowntrend,
                "falling medium structure must be rejected before entry");
    }

    {
        auto medium = BuildMediumUp();
        for (std::size_t index = medium.size() - 6U; index < medium.size(); ++index) {
            medium[index].low = 98.0;
        }
        const auto result = AnalyzePreOpenStructure(
            BuildDaily(101.5),
            medium,
            100.0,
            config);
        Require(!result.mediumDowntrend, "rising medium structure must not be rejected");
        Require(result.structuralRiskPercent > 1.9 && result.structuralRiskPercent < 2.1,
                "structural risk must use entry-to-structure-low distance");
        Require(result.headroomPercent > 1.4 && result.headroomPercent < 1.6,
                "headroom must use nearest overhead daily resistance");
        Require(result.decision == StructureDecision::RejectPoorRewardRisk,
                "poor headroom/risk must be rejected");
    }

    {
        auto medium = BuildMediumUp();
        for (std::size_t index = medium.size() - 6U; index < medium.size(); ++index) {
            medium[index].low = 99.0;
        }
        const auto result = AnalyzePreOpenStructure(
            BuildDaily(104.0),
            medium,
            100.0,
            config);
        Require(result.decision == StructureDecision::Ready,
                "rising pullback with enough headroom must be ready");
        Require(result.structuralRewardRisk > 3.9 && result.structuralRewardRisk < 4.1,
                "reward/risk must be computed from structural stop and resistance");
    }

    {
        const auto result = AnalyzePreOpenStructure(
            BuildDaily(99.0),
            BuildMediumUp(),
            100.0,
            config);
        Require(result.breakoutWatch,
                "price above all recent daily highs must become breakout watch");
        Require(result.decision == StructureDecision::BreakoutWatch,
                "new-high candidate must not be discarded only for missing overhead resistance");
    }

    {
        GapEntryState state;
        GapEntryConfig gap;
        gap.gapUpThresholdPercent = 2.0;
        state.Reset(100.0, 101.0, gap);
        Require(state.Snapshot().phase == GapEntryPhase::NormalReady,
                "sub-threshold opening gap must be immediately tradable");
        Require(state.Snapshot().entryReady,
                "normal opening must not wait for JMA rebreak");
    }

    {
        GapEntryState state;
        GapEntryConfig gap;
        gap.gapUpThresholdPercent = 2.0;
        state.Reset(100.0, 104.0, gap);
        Require(state.Snapshot().phase == GapEntryPhase::GapLocked,
                "large gap-up must start locked");
        Require(!state.Snapshot().entryReady,
                "large gap-up must not buy initial bullish continuation");

        auto snapshot = state.Step(true, false, false, +0.4);
        Require(snapshot.phase == GapEntryPhase::GapLocked && !snapshot.entryReady,
                "bullish gap continuation must stay locked without pullback");

        snapshot = state.Step(true, false, false, 0.0);
        Require(snapshot.phase == GapEntryPhase::PullbackSeen,
                "JMA slope loss must mark a post-open pullback");
        Require(!snapshot.entryReady,
                "pullback alone must not unlock a gap entry");

        snapshot = state.Step(true, true, false, +0.3);
        Require(snapshot.phase == GapEntryPhase::Rearmed,
                "new positive JMA cross after pullback must rearm gap entry");
        Require(snapshot.entryReady,
                "rearmed gap entry must become tradable");
    }

    std::cout << "preopen_entry_model_tests passed\n";
    return 0;
}
