#include "../core/intuitive_strength_trade_evaluator.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    using trading::stock_pool::Bar;
    using trading::stock_pool::MemberSeries;
    using trading::stock_pool::intuitive::EvaluateCausalJmaTrades;
    using trading::stock_pool::intuitive::MemberStrengthSeries;
    using trading::stock_pool::intuitive::StrengthConfig;
    using trading::stock_pool::intuitive::StrengthPoint;
    using trading::stock_pool::intuitive::TradeCostConfig;

    long long Ts(int hhmm)
    {
        return (20260807000000LL + static_cast<long long>(hhmm) * 100LL) * 1000LL;
    }

    void Require(bool condition, const std::string& message)
    {
        if (condition) return;
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }

    bool Near(double actual, double expected, double tolerance = 1.0e-6)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    Bar MakeBar(int hhmm, double open, double high, double low, double close)
    {
        Bar bar;
        bar.closeTimestampMs = Ts(hhmm);
        bar.open = open;
        bar.high = high;
        bar.low = low;
        bar.close = close;
        bar.tickCount = 360;
        return bar;
    }

    StrengthPoint MakePoint(int hhmm)
    {
        StrengthPoint point;
        point.asOf = Ts(hhmm);
        point.inSession = true;
        point.inEvaluationWindow = true;
        point.bullishRegime = true;
        point.fresh = true;
        point.barsSinceCross = 0;
        point.crossJmaSlopePercent = 0.2;
        return point;
    }

    void TestCrossWaveVsCausalExecution()
    {
        MemberSeries member;
        member.code = "TEST01";
        member.name = "causal";
        member.bars = {
            MakeBar(903, 99.0, 102.0, 98.0, 100.0),
            MakeBar(904, 101.0, 110.0, 99.0, 108.0),
            MakeBar(905, 108.0, 115.0, 107.0, 114.0),
            MakeBar(906, 112.0, 113.0, 110.0, 111.0)
        };

        MemberStrengthSeries strength;
        strength.memberIndex = 0U;
        strength.code = member.code;
        strength.name = member.name;
        strength.points = {
            MakePoint(903),
            MakePoint(904),
            MakePoint(905),
            MakePoint(906)
        };
        strength.points[0].crossUp = true;
        strength.points[0].sessionReturnPercent = 1.0;
        strength.points[1].barsSinceCross = 1;
        strength.points[1].sessionReturnPercent = 9.0;
        strength.points[2].crossDown = true;
        strength.points[2].bullishRegime = false;
        strength.points[2].fresh = false;
        strength.points[2].barsSinceCross = -1;
        strength.points[2].sessionReturnPercent = 15.0;
        strength.points[3].bullishRegime = false;
        strength.points[3].fresh = false;
        strength.points[3].barsSinceCross = -1;
        strength.points[3].sessionReturnPercent = 12.0;

        StrengthConfig config;
        config.evaluationStart = Ts(903);
        config.evaluationEnd = Ts(1000);

        TradeCostConfig costs;
        costs.feePercentEachSide = 0.0;
        costs.sellTaxPercent = 0.0;
        costs.slippageBps = 0.0;

        const auto result = EvaluateCausalJmaTrades(
            member,
            strength,
            config,
            Ts(1000),
            costs);

        Require(result.waves.size() == 1U, "one cross wave expected");
        Require(Near(result.crossWaveCompoundPercent, 14.0),
            "cross wave must use cross-up close to cross-down close");
        Require(result.trades.size() == 1U, "one causal trade expected");
        Require(result.closedTradeCount == 1, "trade should close");
        Require(result.trades[0].entryIndex == 1U, "entry must be next Tn open");
        Require(result.trades[0].exitIndex == 3U, "exit must be next Tn open");
        Require(Near(result.trades[0].entryPrice, 101.0), "entry price mismatch");
        Require(Near(result.trades[0].exitPrice, 112.0), "exit price mismatch");
        Require(Near(
            result.trades[0].grossReturnPercent,
            (112.0 / 101.0 - 1.0) * 100.0),
            "causal gross return mismatch");
        Require(Near(
            result.trades[0].mfePercent,
            (115.0 / 101.0 - 1.0) * 100.0),
            "MFE mismatch");
        Require(Near(
            result.trades[0].maePercent,
            (99.0 / 101.0 - 1.0) * 100.0),
            "MAE mismatch");
        Require(Near(result.executionGrossCompoundPercent,
            result.trades[0].grossReturnPercent),
            "single trade compound mismatch");
    }

    void TestOpenTradeUsesAsOfMarkToMarket()
    {
        MemberSeries member;
        member.code = "TEST02";
        member.name = "open";
        member.bars = {
            MakeBar(903, 100.0, 101.0, 99.0, 100.0),
            MakeBar(904, 101.0, 108.0, 100.0, 107.0),
            MakeBar(905, 107.0, 112.0, 106.0, 110.0)
        };

        MemberStrengthSeries strength;
        strength.memberIndex = 0U;
        strength.points = {MakePoint(903), MakePoint(904), MakePoint(905)};
        strength.points[0].crossUp = true;
        strength.points[1].barsSinceCross = 1;
        strength.points[2].barsSinceCross = 2;
        strength.points[2].sessionReturnPercent = 10.0;

        StrengthConfig config;
        config.evaluationStart = Ts(903);
        config.evaluationEnd = Ts(1000);

        TradeCostConfig costs;
        costs.feePercentEachSide = 0.0;
        costs.sellTaxPercent = 0.0;
        costs.slippageBps = 0.0;

        const auto result = EvaluateCausalJmaTrades(
            member,
            strength,
            config,
            Ts(905),
            costs);

        Require(result.trades.size() == 1U, "open trade expected");
        Require(result.hasOpenTrade, "open position flag expected");
        Require(!result.trades[0].closed, "trade must remain MTM");
        Require(Near(result.trades[0].exitPrice, 110.0),
            "MTM must use latest completed close");
        Require(Near(result.executionGrossCompoundPercent,
            (110.0 / 101.0 - 1.0) * 100.0),
            "MTM gross return mismatch");
    }

    void TestNetReturnIncludesCosts()
    {
        MemberSeries member;
        member.code = "TEST03";
        member.bars = {
            MakeBar(903, 100.0, 100.0, 100.0, 100.0),
            MakeBar(904, 100.0, 101.0, 99.0, 100.0),
            MakeBar(905, 100.0, 101.0, 99.0, 100.0),
            MakeBar(906, 100.0, 100.0, 100.0, 100.0)
        };

        MemberStrengthSeries strength;
        strength.memberIndex = 0U;
        strength.points = {
            MakePoint(903), MakePoint(904), MakePoint(905), MakePoint(906)};
        strength.points[0].crossUp = true;
        strength.points[1].barsSinceCross = 1;
        strength.points[2].crossDown = true;
        strength.points[2].bullishRegime = false;
        strength.points[2].fresh = false;
        strength.points[2].barsSinceCross = -1;
        strength.points[3].bullishRegime = false;
        strength.points[3].fresh = false;
        strength.points[3].barsSinceCross = -1;

        StrengthConfig config;
        config.evaluationStart = Ts(903);
        config.evaluationEnd = Ts(1000);

        TradeCostConfig costs;
        const auto result = EvaluateCausalJmaTrades(
            member,
            strength,
            config,
            Ts(1000),
            costs);

        Require(result.trades.size() == 1U, "cost trade expected");
        Require(Near(result.trades[0].grossReturnPercent, 0.0),
            "flat prices should have zero gross return");
        Require(result.trades[0].netReturnPercent < -0.20,
            "fees/tax/slippage must make flat trade negative");
    }
}

int main()
{
    TestCrossWaveVsCausalExecution();
    TestOpenTradeUsesAsOfMarkToMarket();
    TestNetReturnIncludesCosts();
    std::cout << "intuitive_strength_trade_evaluator_tests passed\n";
    return 0;
}
