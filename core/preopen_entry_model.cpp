#include "preopen_entry_model.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace trading::stock_pool::preopen
{
    namespace
    {
        constexpr double kEpsilon = 1.0e-12;

        double SafePercent(double current, double previous)
        {
            if (!std::isfinite(current) || !std::isfinite(previous) ||
                std::abs(previous) <= kEpsilon)
            {
                return 0.0;
            }
            return (current / previous - 1.0) * 100.0;
        }

        double LinearSlopePercentPerBar(
            const std::vector<Bar>& bars,
            std::size_t start,
            bool useHigh)
        {
            if (start >= bars.size() || bars.size() - start < 2U) return 0.0;
            const std::size_t count = bars.size() - start;
            double sumX = 0.0;
            double sumY = 0.0;
            double sumXX = 0.0;
            double sumXY = 0.0;
            double meanPrice = 0.0;
            for (std::size_t offset = 0U; offset < count; ++offset) {
                const double x = static_cast<double>(offset);
                const Bar& bar = bars[start + offset];
                const double y = useHigh ? bar.high : bar.close;
                sumX += x;
                sumY += y;
                sumXX += x * x;
                sumXY += x * y;
                meanPrice += y;
            }
            meanPrice /= static_cast<double>(count);
            const double denominator =
                static_cast<double>(count) * sumXX - sumX * sumX;
            if (std::abs(denominator) <= kEpsilon || meanPrice <= kEpsilon) {
                return 0.0;
            }
            const double slope =
                (static_cast<double>(count) * sumXY - sumX * sumY) /
                denominator;
            return slope / meanPrice * 100.0;
        }

        double RecentLow(const std::vector<Bar>& bars, std::size_t lookback)
        {
            if (bars.empty()) return 0.0;
            const std::size_t start = bars.size() > lookback
                ? bars.size() - lookback
                : 0U;
            double value = std::numeric_limits<double>::infinity();
            for (std::size_t index = start; index < bars.size(); ++index) {
                if (bars[index].low > 0.0) value = (std::min)(value, bars[index].low);
            }
            return std::isfinite(value) ? value : 0.0;
        }

        double NearestResistanceAbove(
            const std::vector<Bar>& bars,
            std::size_t lookback,
            double entry)
        {
            if (bars.empty() || entry <= 0.0) return 0.0;
            const std::size_t start = bars.size() > lookback
                ? bars.size() - lookback
                : 0U;
            double nearest = std::numeric_limits<double>::infinity();
            for (std::size_t index = start; index < bars.size(); ++index) {
                const double high = bars[index].high;
                if (high > entry && high < nearest) nearest = high;
            }
            return std::isfinite(nearest) ? nearest : 0.0;
        }
    }

    StructureAssessment AnalyzePreOpenStructure(
        const std::vector<Bar>& dailyBars,
        const std::vector<Bar>& mediumBars,
        double expectedEntryPrice,
        const StructureConfig& config)
    {
        StructureAssessment result;
        result.expectedEntryPrice = expectedEntryPrice;

        const std::size_t trendLookback = (std::max)(
            static_cast<std::size_t>(2U),
            config.mediumTrendLookback);
        const std::size_t stopLookback = (std::max)(
            static_cast<std::size_t>(2U),
            config.mediumStopLookback);
        if (dailyBars.empty() || mediumBars.size() < trendLookback ||
            expectedEntryPrice <= 0.0)
        {
            return result;
        }
        result.dataReady = true;

        const std::size_t trendStart = mediumBars.size() - trendLookback;
        result.mediumCloseSlopePercentPerBar =
            LinearSlopePercentPerBar(mediumBars, trendStart, false);
        result.mediumHighSlopePercentPerBar =
            LinearSlopePercentPerBar(mediumBars, trendStart, true);
        result.mediumDowntrend =
            result.mediumCloseSlopePercentPerBar < 0.0 &&
            result.mediumHighSlopePercentPerBar < 0.0;
        if (result.mediumDowntrend) {
            result.decision = StructureDecision::RejectMediumDowntrend;
            return result;
        }

        result.structuralStopPrice = RecentLow(mediumBars, stopLookback);
        if (result.structuralStopPrice <= 0.0 ||
            result.structuralStopPrice >= expectedEntryPrice)
        {
            result.decision = StructureDecision::RejectInvalidRisk;
            return result;
        }

        result.structuralRiskPercent =
            (expectedEntryPrice - result.structuralStopPrice) /
            expectedEntryPrice * 100.0;

        result.nearestOverheadResistance = NearestResistanceAbove(
            dailyBars,
            (std::max)(
                static_cast<std::size_t>(1U),
                config.dailyResistanceLookback),
            expectedEntryPrice);

        if (result.nearestOverheadResistance <= 0.0) {
            result.breakoutWatch = true;
            result.headroomPercent = 0.0;
            result.structuralRewardRisk =
                std::numeric_limits<double>::infinity();
            result.decision = StructureDecision::BreakoutWatch;
            return result;
        }

        result.headroomPercent =
            (result.nearestOverheadResistance - expectedEntryPrice) /
            expectedEntryPrice * 100.0;
        result.structuralRewardRisk = result.structuralRiskPercent > kEpsilon
            ? result.headroomPercent / result.structuralRiskPercent
            : 0.0;

        if (result.structuralRewardRisk <
            (std::max)(0.0, config.minimumStructuralRewardRisk))
        {
            result.decision = StructureDecision::RejectPoorRewardRisk;
            return result;
        }

        result.decision = StructureDecision::Ready;
        return result;
    }

    void GapEntryState::Reset(
        double previousClose,
        double sessionOpen,
        const GapEntryConfig& config)
    {
        snapshot_ = {};
        snapshot_.gapPercent = SafePercent(sessionOpen, previousClose);
        snapshot_.gapUp = previousClose > 0.0 && sessionOpen > 0.0 &&
            snapshot_.gapPercent > (std::max)(0.0, config.gapUpThresholdPercent);
        if (snapshot_.gapUp) {
            snapshot_.phase = GapEntryPhase::GapLocked;
            snapshot_.entryReady = false;
        }
        else {
            snapshot_.phase = GapEntryPhase::NormalReady;
            snapshot_.entryReady = true;
        }
    }

    GapEntrySnapshot GapEntryState::Step(
        bool bullishRegime,
        bool crossUp,
        bool crossDown,
        double fastJmaSlopePercent)
    {
        if (!snapshot_.gapUp || snapshot_.phase == GapEntryPhase::Rearmed) {
            snapshot_.entryReady = true;
            return snapshot_;
        }

        if (snapshot_.phase == GapEntryPhase::GapLocked) {
            if (!bullishRegime || crossDown || fastJmaSlopePercent <= 0.0) {
                snapshot_.pullbackSeen = true;
                snapshot_.phase = GapEntryPhase::PullbackSeen;
            }
            snapshot_.entryReady = false;
            return snapshot_;
        }

        if (snapshot_.phase == GapEntryPhase::PullbackSeen) {
            if (crossUp && bullishRegime && fastJmaSlopePercent > 0.0) {
                snapshot_.phase = GapEntryPhase::Rearmed;
                snapshot_.entryReady = true;
            }
            else {
                snapshot_.entryReady = false;
            }
        }
        return snapshot_;
    }

    const char* StructureDecisionName(StructureDecision value) noexcept
    {
        switch (value) {
        case StructureDecision::InsufficientData: return "insufficient-data";
        case StructureDecision::RejectMediumDowntrend: return "reject-medium-downtrend";
        case StructureDecision::RejectInvalidRisk: return "reject-invalid-risk";
        case StructureDecision::RejectPoorRewardRisk: return "reject-poor-reward-risk";
        case StructureDecision::Ready: return "ready";
        case StructureDecision::BreakoutWatch: return "breakout-watch";
        }
        return "unknown";
    }

    const char* GapEntryPhaseName(GapEntryPhase value) noexcept
    {
        switch (value) {
        case GapEntryPhase::NormalReady: return "normal-ready";
        case GapEntryPhase::GapLocked: return "gap-locked";
        case GapEntryPhase::PullbackSeen: return "pullback-seen";
        case GapEntryPhase::Rearmed: return "rearmed";
        }
        return "unknown";
    }
}
