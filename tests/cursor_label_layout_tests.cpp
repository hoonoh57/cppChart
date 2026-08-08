#include "../render/cursor_label_layout.h"

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

    bool Near(float left, float right)
    {
        return std::fabs(left - right) < 0.0001f;
    }
}

int main()
{
    const trading::render::HorizontalLabelPlacement centered =
        trading::render::PlaceCenteredHorizontalLabel(50.0f, 20.0f, 0.0f, 100.0f);
    Check(Near(centered.left, 40.0f) && Near(centered.right, 60.0f),
          "centered label placement mismatch");

    const trading::render::HorizontalLabelPlacement left =
        trading::render::PlaceCenteredHorizontalLabel(3.0f, 20.0f, 0.0f, 100.0f);
    Check(Near(left.left, 0.0f) && Near(left.right, 20.0f),
          "left-edge label must remain inside the pane");

    const trading::render::HorizontalLabelPlacement right =
        trading::render::PlaceCenteredHorizontalLabel(97.0f, 20.0f, 0.0f, 100.0f);
    Check(Near(right.left, 80.0f) && Near(right.right, 100.0f),
          "right-edge label must remain inside the pane");

    const trading::render::HorizontalLabelPlacement oversized =
        trading::render::PlaceCenteredHorizontalLabel(50.0f, 200.0f, 10.0f, 90.0f);
    Check(Near(oversized.left, 10.0f) && Near(oversized.right, 90.0f),
          "oversized label must clamp to the available pane width");

    std::puts("[PASS] cursor_label_layout_tests");
    return 0;
}
