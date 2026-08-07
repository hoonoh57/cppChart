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
}

int main()
{
    using trading::stock_pool::strategy::IsUpwardStrengthCross;

    Require(
        IsUpwardStrengthCross(100.0, 100.01, 100.0),
        "100 to above 100 must be an upward cross");
    Require(
        IsUpwardStrengthCross(99.0, 101.0, 100.0),
        "below 100 to above 100 must be an upward cross");
    Require(
        !IsUpwardStrengthCross(101.0, 102.0, 100.0),
        "remaining above 100 must not re-enter");
    Require(
        !IsUpwardStrengthCross(99.0, 100.0, 100.0),
        "touching 100 without exceeding it must not enter");
    Require(
        !IsUpwardStrengthCross(101.0, 99.0, 100.0),
        "downward crossing must not enter");

    std::cout << "stock_pool_strength_cross_tests passed\n";
    return 0;
}
