#include "../render/value_viewport.h"

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

    bool Near(double left, double right, double tolerance = 0.0001)
    {
        return std::fabs(left - right) <= tolerance;
    }
}

int main()
{
    using namespace trading::render;

    ValueViewport viewport;
    ResetValueViewport(viewport, 100.0, 200.0, 0.10, 0.05);
    Check(viewport.initialized,
          "value viewport reset must initialize");
    Check(viewport.autoScale,
          "value viewport reset must enable auto scale");
    Check(Near(viewport.minimum, 95.0) &&
              Near(viewport.maximum, 210.0),
          "value reset must reserve bottom and top margins");

    const double initialSpan = viewport.Span();
    PanValueViewport(viewport, 50.0, 500.0);
    Check(!viewport.autoScale,
          "vertical plot drag must enter manual value mode");
    Check(viewport.minimum > 95.0 && viewport.maximum > 210.0,
          "dragging downward must move plotted values downward");
    Check(Near(viewport.Span(), initialSpan),
          "vertical pan must preserve value span");

    const double beforeZoom = viewport.Span();
    ZoomValueViewport(viewport, 0.5, -30.0);
    Check(viewport.Span() < beforeZoom,
          "dragging the value axis upward must magnify candles");

    const double manualMinimum = viewport.minimum;
    FollowValueRange(viewport, 50.0, 500.0, 0.10, 0.05);
    Check(Near(viewport.minimum, manualMinimum),
          "manual value range must survive live data updates");

    ResetValueViewport(viewport, 50.0, 500.0, 0.10, 0.05);
    Check(viewport.autoScale,
          "double-click equivalent reset must restore auto scale");
    Check(viewport.maximum > 500.0,
          "reset must restore top observation margin");

    FollowValueRange(viewport, 50.0, 600.0, 0.10, 0.05);
    Check(viewport.maximum > 600.0,
          "auto value range must follow a new live high with top margin");

    ValueViewport invalid;
    ResetValueViewport(invalid, 10.0, 10.0);
    Check(!invalid.initialized,
          "invalid value range must fail closed");

    std::puts("[PASS] value_viewport_tests");
    return 0;
}
