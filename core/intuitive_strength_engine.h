#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "stock_pool_engine.h"

namespace trading::stock_pool::intuitive
{
    struct StrengthConfig final
    {
        int fastJmaPeriod = 7;
        int slowJmaPeriod = 20;
        int jmaPhase = 50;
        int jmaPower = 2;
        int macdFastPeriod = 12;
        int macdSlowPeriod = 26;
        int macdSignalPeriod = 9;
        int atrPeriod = 14;
        int obvSignalPeriod = 9;
        int obvNormalizationBars = 20;
        int maxFreshBars = 3;

        // Packed YYYYMMDDHHMMSS*1000 values. Bars before sessionStart warm the
        // indicators only. Wave state is reset at sessionStart. Buy priority is
        // valid only inside [evaluationStart, evaluationEnd].
        EpochMillis sessionStart = 0;
        EpochMillis evaluationStart = 0;
        EpochMillis evaluationEnd = 0;

        // Tick-rate acceleration compares current real T<n> rate with this
        // trailing median-sized window. No minute-volume proxy is allowed.
        int tickRateBaselineBars = 20;
    };

    struct StrengthPoint final
    {
        EpochMillis asOf = 0;
        double close = 0.0;
        double sessionReturnPercent = 0.0;

        double fastJma = 0.0;
        double slowJma = 0.0;
        double fastJmaSlopePercent = 0.0;
        double slowJmaSlopePercent = 0.0;

        double macdHistogramAtr = 0.0;
        double obvImpulse = 0.0;

        bool warmupOnly = false;
        bool inSession = false;
        bool inEvaluationWindow = false;

        bool bullishRegime = false;
        bool crossUp = false;
        bool crossDown = false;
        int barsSinceCross = -1;
        double crossJmaSlopePercent = 0.0;
        double waveJmaGainPercent = 0.0;
        double priceExtensionPercent = 0.0;
        bool fresh = false;

        // Real tick participation populated from T<n> candle completion time.
        // Example: a T60 candle completed in 2 seconds => 30 ticks/second.
        bool tickAvailable = false;
        double tickRatePerSecond = std::numeric_limits<double>::quiet_NaN();
        double tickRatePerMinute = std::numeric_limits<double>::quiet_NaN();
        double tickAcceleration = std::numeric_limits<double>::quiet_NaN();
        double tickContinuity = std::numeric_limits<double>::quiet_NaN();
    };

    struct MemberStrengthSeries final
    {
        std::size_t memberIndex = 0U;
        std::string code;
        std::string name;
        std::string market;
        std::vector<StrengthPoint> points;
    };

    struct StrengthRow final
    {
        std::size_t memberIndex = 0U;
        std::string code;
        std::string name;
        std::string market;
        int buyPriority = 0;
        bool buyEligible = false;
        StrengthPoint point;
    };

    struct StrengthSnapshot final
    {
        std::size_t asOfIndex = 0U;
        EpochMillis asOf = 0;
        std::vector<StrengthRow> rows;
    };

    std::vector<MemberStrengthSeries> CalculateStrengthSeries(
        const std::vector<MemberSeries>& members,
        const StrengthConfig& config);

    StrengthSnapshot BuildStrengthSnapshot(
        const std::vector<MemberStrengthSeries>& series,
        std::size_t asOfIndex,
        const StrengthConfig& config);

    const char* BuyStateName(const StrengthRow& row) noexcept;
}
