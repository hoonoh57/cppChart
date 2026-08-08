#include "value_viewport.h"

#include <algorithm>
#include <cmath>

namespace trading::render
{
    namespace
    {
        bool ValidRange(double minimum, double maximum) noexcept
        {
            return
                std::isfinite(minimum) &&
                std::isfinite(maximum) &&
                maximum > minimum;
        }

        double SafeFraction(double value) noexcept
        {
            if (!std::isfinite(value)) return 0.0;
            return (std::max)(0.0, (std::min)(1.0, value));
        }
    }

    void ResetValueViewport(
        ValueViewport& viewport,
        double dataMinimum,
        double dataMaximum,
        double topPaddingFraction,
        double bottomPaddingFraction) noexcept
    {
        if (!ValidRange(dataMinimum, dataMaximum)) {
            viewport = {};
            return;
        }

        const double span = dataMaximum - dataMinimum;
        viewport.minimum =
            dataMinimum - span * SafeFraction(bottomPaddingFraction);
        viewport.maximum =
            dataMaximum + span * SafeFraction(topPaddingFraction);
        viewport.initialized = true;
        viewport.autoScale = true;
    }

    void FollowValueRange(
        ValueViewport& viewport,
        double dataMinimum,
        double dataMaximum,
        double topPaddingFraction,
        double bottomPaddingFraction) noexcept
    {
        if (!viewport.initialized || viewport.autoScale) {
            ResetValueViewport(
                viewport,
                dataMinimum,
                dataMaximum,
                topPaddingFraction,
                bottomPaddingFraction);
        }
    }

    void PanValueViewport(
        ValueViewport& viewport,
        double verticalPixels,
        double plotHeight) noexcept
    {
        if (
            !viewport.initialized ||
            !std::isfinite(verticalPixels) ||
            verticalPixels == 0.0 ||
            !std::isfinite(plotHeight) ||
            plotHeight <= 0.0)
        {
            return;
        }

        const double span = viewport.Span();
        if (span <= 0.0) return;
        const double shift = span * verticalPixels / plotHeight;
        viewport.minimum += shift;
        viewport.maximum += shift;
        viewport.autoScale = false;
    }

    void ZoomValueViewport(
        ValueViewport& viewport,
        double anchorRatioFromTop,
        double verticalPixels,
        double sensitivity) noexcept
    {
        if (
            !viewport.initialized ||
            !std::isfinite(verticalPixels) ||
            verticalPixels == 0.0 ||
            !std::isfinite(sensitivity) ||
            sensitivity <= 0.0)
        {
            return;
        }

        const double oldSpan = viewport.Span();
        if (oldSpan <= 0.0) return;
        anchorRatioFromTop = SafeFraction(anchorRatioFromTop);
        const double anchorValue =
            viewport.maximum - oldSpan * anchorRatioFromTop;
        const double factor = std::exp(verticalPixels * sensitivity);
        const double minimumSpan = (std::max)(
            1e-9,
            oldSpan * 1e-6);
        const double newSpan = (std::max)(
            minimumSpan,
            oldSpan * factor);

        viewport.maximum =
            anchorValue + newSpan * anchorRatioFromTop;
        viewport.minimum = viewport.maximum - newSpan;
        viewport.autoScale = false;
    }
}
