#pragma once

#include "stock_pool_engine.h"

#include <cstddef>
#include <vector>

namespace trading::stock_pool::preopen
{
    enum class StructureDecision
    {
        InsufficientData,
        RejectMediumDowntrend,
        RejectInvalidRisk,
        RejectPoorRewardRisk,
        Ready,
        BreakoutWatch
    };

    struct StructureConfig final
    {
        std::size_t dailyResistanceLookback = 40U;
        std::size_t mediumTrendLookback = 12U;
        std::size_t mediumStopLookback = 8U;
        double minimumStructuralRewardRisk = 1.5;
    };

    struct StructureAssessment final
    {
        StructureDecision decision = StructureDecision::InsufficientData;
        bool dataReady = false;
        bool mediumDowntrend = false;
        bool breakoutWatch = false;
        double mediumCloseSlopePercentPerBar = 0.0;
        double mediumHighSlopePercentPerBar = 0.0;
        double expectedEntryPrice = 0.0;
        double structuralStopPrice = 0.0;
        double nearestOverheadResistance = 0.0;
        double structuralRiskPercent = 0.0;
        double headroomPercent = 0.0;
        double structuralRewardRisk = 0.0;
    };

    StructureAssessment AnalyzePreOpenStructure(
        const std::vector<Bar>& dailyBars,
        const std::vector<Bar>& mediumBars,
        double expectedEntryPrice,
        const StructureConfig& config);

    enum class GapEntryPhase
    {
        NormalReady,
        GapLocked,
        PullbackSeen,
        Rearmed
    };

    struct GapEntryConfig final
    {
        // Threshold is intentionally configurable; validation can compare
        // multiple definitions without changing the state machine.
        double gapUpThresholdPercent = 0.0;
    };

    struct GapEntrySnapshot final
    {
        GapEntryPhase phase = GapEntryPhase::NormalReady;
        bool gapUp = false;
        bool pullbackSeen = false;
        bool entryReady = true;
        double gapPercent = 0.0;
    };

    class GapEntryState final
    {
    public:
        void Reset(
            double previousClose,
            double sessionOpen,
            const GapEntryConfig& config);

        GapEntrySnapshot Step(
            bool bullishRegime,
            bool crossUp,
            bool crossDown,
            double fastJmaSlopePercent);

        const GapEntrySnapshot& Snapshot() const noexcept { return snapshot_; }

    private:
        GapEntrySnapshot snapshot_;
    };

    const char* StructureDecisionName(StructureDecision value) noexcept;
    const char* GapEntryPhaseName(GapEntryPhase value) noexcept;
}
