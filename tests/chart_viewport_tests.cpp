#include "../render/chart_viewport.h"

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

    void TestLatestWindowResetZoomAndPan()
    {
        trading::render::ChartViewport viewport;
        trading::render::ResetViewport(viewport, 0.0, 899.0, 149.0);
        Check(viewport.initialized, "viewport reset must initialize");
        Check(Near(viewport.visibleStart, 750.0) &&
                  Near(viewport.visibleEnd, 899.0),
              "zero-padding reset must preserve the legacy latest window");
        Check(viewport.autoScroll,
              "viewport reset must enable auto-scroll");

        const double oldSpan = viewport.Span();
        trading::render::ZoomViewport(
            viewport,
            0.0,
            899.0,
            0.5,
            1.0,
            12.0);
        Check(viewport.Span() < oldSpan,
              "positive wheel must zoom in");
        Check(!viewport.autoScroll,
              "center zoom away from latest edge must disable auto-scroll");

        const double span = viewport.Span();
        const double start = viewport.visibleStart;
        trading::render::PanViewport(
            viewport,
            0.0,
            899.0,
            -0.25);
        Check(Near(viewport.Span(), span),
              "pan must preserve the visible span");
        Check(viewport.visibleStart < start,
              "negative pan fraction must move toward older bars");
    }

    void TestLatestRightMarginAndManualFutureSpace()
    {
        constexpr double padding = 0.12;
        constexpr double overscroll = 0.60;
        trading::render::ChartViewport viewport;
        trading::render::ResetViewport(
            viewport,
            0.0,
            899.0,
            149.0,
            padding,
            overscroll);
        Check(viewport.visibleEnd > 899.0,
              "latest reset must reserve visible space after the newest bar");
        Check(Near(viewport.visibleStart, 750.0),
              "right margin must not discard the requested historical window");
        const double resetGap = viewport.visibleEnd - 899.0;
        Check(resetGap > 10.0,
              "default latest margin must be visually meaningful");

        const double previousEnd = viewport.visibleEnd;
        trading::render::PanViewport(
            viewport,
            0.0,
            899.0,
            0.20,
            padding,
            overscroll);
        Check(viewport.visibleEnd > previousEnd,
              "dragging the chart left must create additional future space");
        Check(!viewport.autoScroll,
              "manual future-space pan must suspend auto-follow");

        const double manualStart = viewport.visibleStart;
        trading::render::FollowLatest(
            viewport,
            0.0,
            900.0,
            padding,
            overscroll);
        Check(Near(viewport.visibleStart, manualStart),
              "manual future-space viewport must survive live updates");

        trading::render::ResetViewport(
            viewport,
            0.0,
            900.0,
            149.0,
            padding,
            overscroll);
        Check(viewport.autoScroll,
              "double-click equivalent reset must restore auto-follow");
        Check(viewport.visibleEnd > 900.0,
              "reset must restore the latest-bar right margin");
    }

    void TestClampAndMinimumSpan()
    {
        trading::render::ChartViewport viewport;
        viewport.visibleStart = -100.0;
        viewport.visibleEnd = -99.0;
        viewport.initialized = true;
        viewport.autoScroll = false;

        trading::render::ClampViewport(
            viewport,
            0.0,
            100.0,
            12.0);
        Check(Near(viewport.visibleStart, 0.0),
              "clamp must move an early range to data start");
        Check(Near(viewport.Span(), 12.0),
              "clamp must enforce minimum span");

        trading::render::ZoomViewport(
            viewport,
            0.0,
            100.0,
            0.5,
            100.0,
            12.0);
        Check(Near(viewport.Span(), 12.0),
              "zoom must not pass minimum span");

        trading::render::PanViewport(
            viewport,
            0.0,
            100.0,
            100.0);
        Check(Near(viewport.visibleEnd, 100.0),
              "zero-overscroll pan must clamp to data end");
        Check(viewport.autoScroll,
              "panning to the latest edge must restore auto-scroll");
    }

    void TestFollowLatestAndManualHold()
    {
        trading::render::ChartViewport viewport;
        trading::render::ResetViewport(viewport, 0.0, 100.0, 30.0);
        const double span = viewport.Span();

        trading::render::FollowLatest(viewport, 0.0, 101.0);
        Check(Near(viewport.visibleEnd, 101.0),
              "zero-padding auto-scroll must follow the newest bar");
        Check(Near(viewport.Span(), span),
              "auto-scroll must retain zoom span");

        trading::render::PanViewport(
            viewport,
            0.0,
            101.0,
            -0.25);
        Check(!viewport.autoScroll,
              "manual pan away from latest must disable auto-scroll");
        const double manualStart = viewport.visibleStart;
        trading::render::FollowLatest(viewport, 0.0, 102.0);
        Check(Near(viewport.visibleStart, manualStart),
              "manual viewport must not jump on new live data");
    }

    void TestInvalidDataRange()
    {
        trading::render::ChartViewport viewport;
        trading::render::ResetViewport(viewport, 1.0, 1.0, 10.0);
        Check(!viewport.initialized,
              "invalid data range must clear viewport");
        Check(Near(viewport.Span(), 0.0),
              "invalid data range must have zero span");
    }
}

int main()
{
    TestLatestWindowResetZoomAndPan();
    TestLatestRightMarginAndManualFutureSpace();
    TestClampAndMinimumSpan();
    TestFollowLatestAndManualHold();
    TestInvalidDataRange();
    std::puts("[PASS] chart_viewport_tests");
    return 0;
}
