#include "../render/chart_viewport.h"

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

    void TestResetZoomAndPan()
    {
        trading::render::ChartViewport viewport;
        trading::render::ResetViewport(viewport, 1000, 11000);
        Check(viewport.initialized, "viewport reset must initialize");
        Check(viewport.visibleStartMs == 1000 &&
                  viewport.visibleEndMs == 11000,
              "viewport reset range mismatch");
        Check(viewport.autoScroll,
              "viewport reset must enable auto-scroll");

        trading::render::ZoomViewport(
            viewport,
            1000,
            11000,
            0.5,
            1.0,
            1000);
        Check(viewport.SpanMs() < 10000,
              "positive wheel must zoom in");
        Check(viewport.visibleStartMs > 1000 &&
                  viewport.visibleEndMs < 11000,
              "center zoom must preserve the center anchor");
        Check(!viewport.autoScroll,
              "zoom away from the latest edge must disable auto-scroll");

        const auto span = viewport.SpanMs();
        const auto start = viewport.visibleStartMs;
        trading::render::PanViewport(
            viewport,
            1000,
            11000,
            0.25);
        Check(viewport.SpanMs() == span,
              "pan must preserve the visible span");
        Check(viewport.visibleStartMs > start,
              "positive pan fraction must move toward newer data");
    }

    void TestClampAndMinimumSpan()
    {
        trading::render::ChartViewport viewport;
        viewport.visibleStartMs = -10000;
        viewport.visibleEndMs = -9000;
        viewport.initialized = true;
        viewport.autoScroll = false;

        trading::render::ClampViewport(
            viewport,
            1000,
            11000,
            2000);
        Check(viewport.visibleStartMs == 1000,
              "clamp must move an early range to data start");
        Check(viewport.SpanMs() == 2000,
              "clamp must enforce minimum span");

        trading::render::ZoomViewport(
            viewport,
            1000,
            11000,
            0.5,
            100.0,
            3000);
        Check(viewport.SpanMs() == 3000,
              "zoom must not pass minimum span");

        trading::render::PanViewport(
            viewport,
            1000,
            11000,
            100.0);
        Check(viewport.visibleEndMs == 11000,
              "pan must clamp to data end");
        Check(viewport.autoScroll,
              "panning to the latest edge must restore auto-scroll");
    }

    void TestFollowLatest()
    {
        trading::render::ChartViewport viewport;
        trading::render::ResetViewport(viewport, 1000, 11000);
        trading::render::ZoomViewport(
            viewport,
            1000,
            11000,
            1.0,
            2.0,
            1000);
        Check(viewport.autoScroll,
              "zoom anchored at latest edge must retain auto-scroll");

        const auto span = viewport.SpanMs();
        trading::render::FollowLatest(viewport, 1000, 12000);
        Check(viewport.visibleEndMs == 12000,
              "auto-scroll must follow the new latest timestamp");
        Check(viewport.SpanMs() == span,
              "auto-scroll must retain zoom span");

        trading::render::PanViewport(
            viewport,
            1000,
            12000,
            -0.25);
        Check(!viewport.autoScroll,
              "manual pan away from latest must disable auto-scroll");
        const auto manualStart = viewport.visibleStartMs;
        trading::render::FollowLatest(viewport, 1000, 13000);
        Check(viewport.visibleStartMs == manualStart,
              "manual viewport must not jump on new data");
    }

    void TestInvalidDataRange()
    {
        trading::render::ChartViewport viewport;
        trading::render::ResetViewport(viewport, 1000, 1000);
        Check(!viewport.initialized,
              "invalid data range must clear viewport");
        Check(viewport.SpanMs() == 0,
              "invalid data range must have zero span");
    }
}

int main()
{
    TestResetZoomAndPan();
    TestClampAndMinimumSpan();
    TestFollowLatest();
    TestInvalidDataRange();
    std::puts("[PASS] chart_viewport_tests");
    return 0;
}
