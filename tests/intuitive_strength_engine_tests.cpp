#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "../core/intuitive_strength_engine.h"

namespace
{
    using trading::stock_pool::Bar;
    using trading::stock_pool::MemberSeries;
    using trading::stock_pool::intuitive::BuildStrengthSnapshot;
    using trading::stock_pool::intuitive::CalculateStrengthSeries;
    using trading::stock_pool::intuitive::StrengthConfig;

    void Require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    MemberSeries BuildSeries()
    {
        MemberSeries member;
        member.code = "TEST";
        member.name = "TEST";
        member.market = "KOSDAQ";

        double cumulativeTurnover = 0.0;
        double previous = 100.0;
        for (int index = 0; index < 60; ++index) {
            double close = previous;
            if (index < 24) close = 100.0 - index * 0.05;
            else if (index < 34) close = 98.8 + (index - 23) * 0.75;
            else if (index < 46) close = 106.3 + (index - 33) * 0.10;
            else close = 107.5 - (index - 45) * 0.70;

            Bar bar;
            bar.closeTimestampMs = 20260807090000LL * 1000LL + index;
            bar.open = previous;
            bar.close = close;
            bar.high = (std::max)(previous, close) + 0.2;
            bar.low = (std::min)(previous, close) - 0.2;
            cumulativeTurnover += close * 1000.0;
            bar.cumulativeTurnover = cumulativeTurnover;
            member.bars.push_back(bar);
            previous = close;
        }
        return member;
    }
}

int main()
{
    StrengthConfig config;
    config.fastJmaPeriod = 7;
    config.slowJmaPeriod = 20;
    config.jmaPhase = 50;
    config.jmaPower = 2;
    config.maxFreshBars = 2;

    std::vector<MemberSeries> members{BuildSeries()};
    auto calculated = CalculateStrengthSeries(members, config);
    Require(calculated.size() == 1U, "one member must be calculated");
    Require(calculated[0].points.size() == members[0].bars.size(),
            "point count must match bar count");

    int crossIndex = -1;
    int downIndex = -1;
    for (std::size_t index = 0; index < calculated[0].points.size(); ++index) {
        const auto& point = calculated[0].points[index];
        if (crossIndex < 0 && point.crossUp) crossIndex = static_cast<int>(index);
        if (crossIndex >= 0 && point.crossDown) {
            downIndex = static_cast<int>(index);
            break;
        }
    }

    Require(crossIndex >= 0, "strict JMA7/JMA20 upward cross must occur");
    const auto& cross = calculated[0].points[static_cast<std::size_t>(crossIndex)];
    Require(cross.barsSinceCross == 0, "cross bar age must be zero");
    Require(cross.fresh, "cross bar must be fresh");
    Require(cross.crossJmaSlopePercent > 0.0,
            "cross strength must be positive JMA slope");

    if (static_cast<std::size_t>(crossIndex + 3) < calculated[0].points.size()) {
        const auto& stale = calculated[0].points[
            static_cast<std::size_t>(crossIndex + 3)];
        if (stale.bullishRegime) {
            Require(!stale.fresh,
                    "bars beyond maxFreshBars must expire from buy eligibility");
        }
    }

    Require(downIndex >= 0, "downward reset cross must occur");
    const auto& down = calculated[0].points[static_cast<std::size_t>(downIndex)];
    Require(down.crossDown, "down point must report crossDown");
    Require(down.barsSinceCross == -1,
            "downward cross must reset wave age");
    Require(!down.fresh, "downward cross must not remain fresh");

    auto snapshot = BuildStrengthSnapshot(
        calculated,
        static_cast<std::size_t>(crossIndex),
        config);
    Require(snapshot.rows.size() == 1U, "snapshot must contain member");
    Require(snapshot.rows[0].buyEligible,
            "fresh positive cross must be buy-eligible");
    Require(snapshot.rows[0].buyPriority == 1,
            "fresh positive cross must receive first priority");
    Require(!snapshot.rows[0].point.tickAvailable,
            "tick participation must remain unavailable without real tick data");

    std::cout << "intuitive_strength_engine_tests passed\n";
    return 0;
}
