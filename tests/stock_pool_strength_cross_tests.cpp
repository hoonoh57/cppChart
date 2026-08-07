#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../app/stock_pool_strength_cross.h"

namespace
{
    void Require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    bool Near(double left, double right, double tolerance = 1.0e-9)
    {
        return std::abs(left - right) <= tolerance;
    }
}

int main()
{
    using trading::stock_pool::Bar;
    using trading::stock_pool::MemberSeries;
    using trading::stock_pool::strategy::CaptureAnchoredReturnPercent;
    using trading::stock_pool::strategy::IsUpwardStrengthCross;

    Require(
        !IsUpwardStrengthCross(100.0, 129.0, 100.0),
        "touching 100 on the previous bar must block entry");
    Require(
        IsUpwardStrengthCross(99.0, 101.0, 100.0),
        "strict below-100 to above-100 crossing must enter");
    Require(
        IsUpwardStrengthCross(71.0, 129.0, 100.0),
        "clear crossing over 100 must enter");
    Require(
        !IsUpwardStrengthCross(101.0, 129.0, 100.0),
        "remaining above 100 must not re-enter");
    Require(
        !IsUpwardStrengthCross(99.0, 100.0, 100.0),
        "touching 100 without exceeding it must not enter");
    Require(
        !IsUpwardStrengthCross(101.0, 99.0, 100.0),
        "downward crossing must not enter");

    MemberSeries member;
    Bar first;
    first.open = 100.0;
    first.close = 120.0;
    member.bars.push_back(first);
    Bar second;
    second.open = 120.0;
    second.close = 110.0;
    member.bars.push_back(second);
    Require(
        Near(CaptureAnchoredReturnPercent(member, 1U), 10.0),
        "session return must remain anchored to the first source-bar open, not the first aggregated close");

    std::cout << "stock_pool_strength_cross_tests passed\n";
    return 0;
}
