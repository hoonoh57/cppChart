#include "../render/pane_layout.h"

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
    float upper = 0.8f;
    float lower = 0.2f;
    const float original = upper + lower;
    Check(trading::render::AdjustAdjacentPaneWeights(
              800.0f,
              60.0f,
              1.0f,
              80.0f,
              upper,
              lower),
          "pane splitter drag must adjust weights");
    Check(upper > 0.8f && lower < 0.2f,
          "positive drag must grow upper pane");
    Check(std::fabs((upper + lower) - original) < 0.0001f,
          "pane splitter must preserve pair weight");

    upper = 0.9f;
    lower = 0.1f;
    Check(trading::render::AdjustAdjacentPaneWeights(
              500.0f,
              80.0f,
              1.0f,
              300.0f,
              upper,
              lower),
          "pane splitter must clamp an oversized drag");
    Check(lower >= 0.159f,
          "lower pane must not shrink below minimum height");

    upper = 0.5f;
    lower = 0.5f;
    Check(!trading::render::AdjustAdjacentPaneWeights(
              0.0f,
              60.0f,
              1.0f,
              10.0f,
              upper,
              lower),
          "invalid layout dimensions must fail closed");

    std::puts("[PASS] pane_layout_tests");
    return 0;
}
