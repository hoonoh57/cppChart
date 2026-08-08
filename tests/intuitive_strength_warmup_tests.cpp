#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "../core/intuitive_strength_engine.h"

namespace
{
    using trading::stock_pool::Bar;
    using trading::stock_pool::EpochMillis;
    using trading::stock_pool::MemberSeries;
    using trading::stock_pool::intuitive::BuildStrengthSnapshotAtTime;
    using trading::stock_pool::intuitive::CalculateStrengthSeries;
    using trading::stock_pool::intuitive::StrengthConfig;

    void Require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    EpochMillis Packed(const char* value)
    {
        return static_cast<EpochMillis>(std::strtoll(value, nullptr, 10)) * 1000LL;
    }

    void AddBar(
        MemberSeries& member,
        EpochMillis timestamp,
        double open,
        double close,
        int tickCount,
        double tickRatePerSecond)
    {
        Bar bar;
        bar.closeTimestampMs = timestamp;
        bar.open = open;
        bar.close = close;
        bar.high = (std::max)(open, close) + 0.1;
        bar.low = (std::min)(open, close) - 0.1;
        bar.cumulativeTurnover =
            (member.bars.empty() ? 0.0 : member.bars.back().cumulativeTurnover) +
            close * 1000.0;
        bar.tickCount = tickCount;
        bar.tickDurationSeconds = tickCount > 0 ?
            static_cast<double>(tickCount) / tickRatePerSecond : 0.0;
        bar.tickRatePerSecond = tickRatePerSecond;
        member.bars.push_back(bar);
    }
}

int main()
{
    MemberSeries member;
    member.code = "WARM";
    member.name = "WARM";

    for (int index = 0; index < 80; ++index) {
        char stamp[32]{};
        const int minute = 14 * 60 + index;
        std::snprintf(
            stamp,
            sizeof(stamp),
            "20260806%02d%02d00",
            minute / 60,
            minute % 60);
        AddBar(member, Packed(stamp), 100.0, 100.0, 360, 2.0);
    }

    AddBar(member, Packed("20260807090030"), 100.0, 108.0, 360, 12.0);
    AddBar(member, Packed("20260807090100"), 108.0, 111.0, 360, 14.0);
    AddBar(member, Packed("20260807090200"), 111.0, 113.0, 360, 15.0);
    AddBar(member, Packed("20260807090310"), 113.0, 115.0, 360, 16.0);
    AddBar(member, Packed("20260807090400"), 115.0, 116.0, 360, 14.0);

    StrengthConfig config;
    config.sessionStart = Packed("20260807090000");
    config.evaluationStart = Packed("20260807090300");
    config.evaluationEnd = Packed("20260807100000");
    config.maxFreshBars = 6;

    const auto calculated = CalculateStrengthSeries({member}, config);
    Require(calculated.size() == 1U, "one warm-start series expected");
    Require(calculated[0].points.size() == member.bars.size(), "point count mismatch");

    const auto& prior = calculated[0].points[79U];
    const auto& open = calculated[0].points[80U];
    Require(prior.warmupOnly, "prior-session point must be warmup-only");
    Require(!prior.inSession, "prior-session point must not be today's session");
    Require(open.inSession, "09:00 point must enter today's session");
    Require(!open.inEvaluationWindow, "09:00 point must not be buy-evaluable");

    Require(
        std::abs(open.fastJma - open.close) > 0.1,
        "09:00 JMA must retain prior-session state instead of resetting to price");

    const auto beforeGate = BuildStrengthSnapshotAtTime(
        calculated,
        Packed("20260807090230"),
        config);
    Require(beforeGate.rows.size() == 1U, "pre-gate snapshot row missing");
    Require(!beforeGate.rows[0].buyEligible,
            "buy priority must be impossible before 09:03");

    const auto afterGate = BuildStrengthSnapshotAtTime(
        calculated,
        Packed("20260807090320"),
        config);
    Require(afterGate.rows.size() == 1U, "post-gate snapshot row missing");
    Require(afterGate.rows[0].point.inEvaluationWindow,
            "latest completed candle after 09:03 must be evaluable");
    Require(afterGate.rows[0].point.tickAvailable,
            "real tick metadata must propagate into strength point");
    Require(afterGate.rows[0].point.tickRatePerMinute > 0.0,
            "tick rate must remain a raw positive participation metric");

    std::cout << "intuitive_strength_warmup_tests passed\n";
    return 0;
}
