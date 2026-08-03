#include "../render/series_geometry.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

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
}

int main()
{
    const float candle = trading::render::SeriesBodyWidth(1000.0f, 199.0);
    const float volume = trading::render::SeriesBodyWidth(1000.0f, 199.0);
    Check(std::fabs(candle - volume) < 0.0001f,
          "price and volume body widths must share the same axis-slot geometry");
    Check(candle > 1.0f && candle < 18.0f,
          "normal zoom body width must remain readable");
    Check(trading::render::SeriesBodyWidth(1000.0f, 10.0) == 18.0f,
          "high zoom body width must respect maximum");
    Check(trading::render::SeriesBodyWidth(1000.0f, 2000.0) == 1.0f,
          "wide view body width must respect minimum");

    std::puts("[PASS] series_geometry_tests");
    return 0;
}
