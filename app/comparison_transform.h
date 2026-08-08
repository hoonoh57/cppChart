#pragma once

#include "../core/market_types.h"
#include "../render/render_document.h"

#include <vector>

namespace trading::app
{
    enum class ComparisonValueMode
    {
        RawClose,
        Indexed100,
        ReturnPercent,
        RelativeStrength100
    };

    struct ComparisonTransformResult final
    {
        std::vector<render::LinePoint> points;
        EpochMillis anchorTimestampMs = 0;
        double anchorComparisonValue = 0.0;
        double anchorPrimaryValue = 0.0;
        bool hasAnchor = false;
    };

    ComparisonTransformResult TransformComparisonBars(
        const std::vector<Bar>& comparisonBars,
        const std::vector<Bar>& primaryBars,
        ComparisonValueMode mode,
        double valueDivisor);

    bool TransformComparisonLivePoint(
        const Bar& comparisonLiveBar,
        const std::vector<Bar>& primaryBars,
        const ComparisonTransformResult& completed,
        ComparisonValueMode mode,
        double valueDivisor,
        render::LinePoint& point) noexcept;

    const char* ComparisonValueModeName(
        ComparisonValueMode mode) noexcept;
}
