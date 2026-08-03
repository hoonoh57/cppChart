#include "../render/value_grid.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::exit(1);
    }

    void Check(bool condition, const char* message)
    {
        if (!condition) Fail(message);
    }

    trading::render::ValueGrid KrxEquityGrid()
    {
        trading::render::ValueGrid grid;
        grid.enabled = true;
        grid.bands = {
            { 2000.0, 1.0 },
            { 5000.0, 5.0 },
            { 20000.0, 10.0 },
            { 50000.0, 50.0 },
            { 200000.0, 100.0 },
            { 500000.0, 500.0 }
        };
        grid.fallbackStep = 1000.0;
        return grid;
    }
}

int main()
{
    const trading::render::ValueGrid grid = KrxEquityGrid();
    const char* error = nullptr;
    Check(trading::render::ValidateValueGrid(grid, &error),
          "KRX equity value-grid must validate");
    Check(trading::render::ValueGridStep(grid, 1999.0) == 1.0,
          "below 2,000 tick mismatch");
    Check(trading::render::ValueGridStep(grid, 2000.0) == 5.0,
          "2,000 boundary tick mismatch");
    Check(trading::render::ValueGridStep(grid, 5000.0) == 10.0,
          "5,000 boundary tick mismatch");
    Check(trading::render::ValueGridStep(grid, 20000.0) == 50.0,
          "20,000 boundary tick mismatch");
    Check(trading::render::ValueGridStep(grid, 50000.0) == 100.0,
          "50,000 boundary tick mismatch");
    Check(trading::render::ValueGridStep(grid, 200000.0) == 500.0,
          "200,000 boundary tick mismatch");
    Check(trading::render::ValueGridStep(grid, 500000.0) == 1000.0,
          "500,000 boundary tick mismatch");
    Check(trading::render::QuantizeValue(grid, 239543.0) == 239500.0,
          "price cursor must snap to nearest legal tick");
    Check(trading::render::QuantizeValue(grid, 239760.0) == 240000.0,
          "price cursor upper rounding mismatch");

    trading::render::ValueGrid volume;
    volume.enabled = true;
    volume.fallbackStep = 1.0;
    Check(trading::render::QuantizeValue(volume, 1234.49) == 1234.0,
          "volume cursor integer rounding mismatch");
    Check(trading::render::QuantizeValue(volume, 1234.51) == 1235.0,
          "volume cursor upper rounding mismatch");

    std::puts("[PASS] value_grid_tests");
    return 0;
}
