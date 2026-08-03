#include "indicator_render_adapter.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace trading::app
{
    namespace
    {
        bool IsVisibleLevel(FeatureLevel level) noexcept
        {
            return
                level == FeatureLevel::Visible ||
                level == FeatureLevel::Active;
        }

        bool IsFiniteReady(
            const indicators::IndicatorValue& value,
            std::size_t outputIndex) noexcept
        {
            return
                value.IsReady(outputIndex) &&
                std::isfinite(value.Value(outputIndex));
        }

        std::string SegmentId(
            const std::string& base,
            std::size_t index)
        {
            return index == 0
                ? base
                : base + ".segment." + std::to_string(index + 1U);
        }
    }

    bool IndicatorRenderAdapter::Configure(
        const IndicatorRenderPlan& plan,
        std::string& error)
    {
        std::set<std::string> seriesIds;
        for (const IndicatorOutputBinding& binding : plan.bindings) {
            if (binding.indicatorId.empty()) {
                error = "indicator render binding id is empty";
                return false;
            }
            if (binding.outputIndex >= indicators::MaxIndicatorOutputs) {
                error = "indicator render output index is out of range";
                return false;
            }
            if (binding.paneId.empty()) {
                error = "indicator render pane id is empty";
                return false;
            }
            if (binding.seriesId.empty()) {
                error = "indicator render series id is empty";
                return false;
            }
            if (!seriesIds.insert(binding.seriesId).second) {
                error =
                    "duplicate indicator render series id: " +
                    binding.seriesId;
                return false;
            }
            if (
                !std::isfinite(binding.paneHeightWeight) ||
                binding.paneHeightWeight <= 0.0f)
            {
                error = "indicator render pane height is invalid";
                return false;
            }
            if (
                binding.paneValueScale == render::PaneValueScale::Fixed &&
                (
                    !std::isfinite(binding.fixedMinimum) ||
                    !std::isfinite(binding.fixedMaximum) ||
                    binding.fixedMaximum <= binding.fixedMinimum))
            {
                error = "indicator render fixed pane range is invalid";
                return false;
            }
            if (
                binding.kind == IndicatorRenderKind::Line &&
                (!std::isfinite(binding.width) || binding.width <= 0.0f))
            {
                error = "indicator render line width is invalid";
                return false;
            }
        }

        plan_ = plan;
        caches_.clear();
        ++planRevision_;
        error.clear();
        return true;
    }

    bool IndicatorRenderAdapter::Apply(
        const IndicatorModuleSnapshot& indicators,
        render::RenderDocument& document,
        std::string& error)
    {
        if (!IsVisibleLevel(indicators.level)) {
            error = "indicator render source is not Visible or Active";
            return false;
        }
        if (indicators.state != IndicatorModuleState::Ready) {
            error = indicators.error.empty()
                ? "indicator render source is not Ready"
                : indicators.error;
            return false;
        }
        if (document.workspaceId.empty() || document.panes.empty()) {
            error = "indicator render target document is incomplete";
            return false;
        }

        std::uint64_t structureToken = planRevision_;
        structureToken = MixRevision(
            structureToken,
            indicators.completedRevision);
        std::size_t contributedSeries = 0;

        for (const IndicatorOutputBinding& binding : plan_.bindings) {
            const IndicatorSeriesSnapshot* series =
                FindSeries(indicators, binding.indicatorId);
            if (series == nullptr) {
                error =
                    "indicator render binding source was not found: " +
                    binding.indicatorId;
                return false;
            }

            render::Pane* pane =
                FindOrCreatePane(document, binding, error);
            if (pane == nullptr) return false;

            BindingCache& cache = caches_[binding.seriesId];
            const void* completedIdentity = series->completedValues.get();
            const bool cacheChanged =
                cache.completedRevision != indicators.completedRevision ||
                cache.completedIdentity != completedIdentity;

            if (binding.kind == IndicatorRenderKind::Line) {
                if (
                    cacheChanged &&
                    !RebuildLineCache(
                        binding,
                        *series,
                        indicators.completedRevision,
                        cache,
                        error))
                {
                    return false;
                }

                const bool hasLive =
                    series->hasLiveValue &&
                    IsFiniteReady(
                        series->liveValue,
                        binding.outputIndex);
                render::LinePoint livePoint;
                if (hasLive) {
                    livePoint.timestampMs =
                        series->liveValue.timestampMs;
                    livePoint.value =
                        series->liveValue.Value(binding.outputIndex);
                }

                std::size_t segmentIndex = 0;
                for (const auto& segment : cache.lineSegments) {
                    render::LineSeries line;
                    line.id = SegmentId(
                        binding.seriesId,
                        segmentIndex);
                    line.label = binding.label;
                    line.color = binding.primaryColor;
                    line.width = binding.width;
                    line.visible = binding.visible;

                    const bool appendLive =
                        hasLive &&
                        cache.completedEndsFinite &&
                        segmentIndex + 1U == cache.lineSegments.size();
                    line.points.SetShared(
                        segment,
                        appendLive ? &livePoint : nullptr);
                    pane->lines.push_back(std::move(line));
                    ++segmentIndex;
                    ++contributedSeries;
                }

                if (hasLive && !cache.completedEndsFinite) {
                    render::LineSeries line;
                    line.id = SegmentId(
                        binding.seriesId,
                        segmentIndex);
                    line.label = binding.label;
                    line.color = binding.primaryColor;
                    line.width = binding.width;
                    line.visible = binding.visible;
                    line.points.push_back(livePoint);
                    pane->lines.push_back(std::move(line));
                    ++segmentIndex;
                    ++contributedSeries;
                }

                structureToken = MixRevision(
                    structureToken,
                    static_cast<std::uint64_t>(segmentIndex));
            }
            else {
                if (
                    cacheChanged &&
                    !RebuildHistogramCache(
                        binding,
                        *series,
                        indicators.completedRevision,
                        cache,
                        error))
                {
                    return false;
                }

                render::HistogramPoint livePoint;
                const bool hasLive =
                    series->hasLiveValue &&
                    IsFiniteReady(
                        series->liveValue,
                        binding.outputIndex);
                if (hasLive) {
                    livePoint.timestampMs =
                        series->liveValue.timestampMs;
                    livePoint.value =
                        series->liveValue.Value(binding.outputIndex);
                    livePoint.positive = livePoint.value >= 0.0;
                }

                render::HistogramSeries histogram;
                histogram.id = binding.seriesId;
                histogram.label = binding.label;
                histogram.positiveColor = binding.primaryColor;
                histogram.negativeColor = binding.secondaryColor;
                histogram.visible = binding.visible;
                histogram.points.SetShared(
                    cache.histogramPoints,
                    hasLive ? &livePoint : nullptr);
                pane->histograms.push_back(std::move(histogram));
                ++contributedSeries;
                structureToken = MixRevision(structureToken, 1U);
            }
        }

        document.revision = MixRevision(
            document.revision,
            MixRevision(
                planRevision_,
                indicators.calculationRevision));
        document.structureRevision = MixRevision(
            document.structureRevision,
            structureToken);

        std::string validationError;
        if (!render::ValidateRenderDocument(document, validationError)) {
            error =
                "indicator render contribution is invalid: " +
                validationError;
            return false;
        }

        (void)contributedSeries;
        error.clear();
        return true;
    }

    void IndicatorRenderAdapter::Reset() noexcept
    {
        plan_ = {};
        caches_.clear();
        ++planRevision_;
    }

    std::uint64_t IndicatorRenderAdapter::PlanRevision() const noexcept
    {
        return planRevision_;
    }

    std::size_t IndicatorRenderAdapter::RetainedBytes() const noexcept
    {
        std::size_t bytes =
            plan_.bindings.capacity() *
                sizeof(IndicatorOutputBinding) +
            caches_.size() *
                sizeof(std::pair<const std::string, BindingCache>);

        for (const IndicatorOutputBinding& binding : plan_.bindings) {
            bytes += binding.indicatorId.capacity();
            bytes += binding.paneId.capacity();
            bytes += binding.paneTitle.capacity();
            bytes += binding.seriesId.capacity();
            bytes += binding.label.capacity();
        }
        for (const auto& entry : caches_) {
            bytes += entry.first.capacity();
            const BindingCache& cache = entry.second;
            bytes += cache.lineSegments.capacity() *
                sizeof(std::shared_ptr<const std::vector<render::LinePoint>>);
            for (const auto& segment : cache.lineSegments) {
                if (segment) {
                    bytes += segment->capacity() *
                        sizeof(render::LinePoint);
                }
            }
            if (cache.histogramPoints) {
                bytes += cache.histogramPoints->capacity() *
                    sizeof(render::HistogramPoint);
            }
        }
        return bytes;
    }

    const IndicatorSeriesSnapshot* IndicatorRenderAdapter::FindSeries(
        const IndicatorModuleSnapshot& indicators,
        const std::string& id) const noexcept
    {
        for (const IndicatorSeriesSnapshot& series : indicators.series) {
            if (series.spec.id == id) return &series;
        }
        return nullptr;
    }

    render::Pane* IndicatorRenderAdapter::FindOrCreatePane(
        render::RenderDocument& document,
        const IndicatorOutputBinding& binding,
        std::string& error) const
    {
        for (render::Pane& pane : document.panes) {
            if (pane.id == binding.paneId) return &pane;
        }

        render::Pane pane;
        pane.id = binding.paneId;
        pane.title = binding.paneTitle.empty()
            ? binding.paneId
            : binding.paneTitle;
        pane.heightWeight = binding.paneHeightWeight;
        pane.valueScale = binding.paneValueScale;
        pane.fixedMinimum = binding.fixedMinimum;
        pane.fixedMaximum = binding.fixedMaximum;
        pane.cursorGrid = binding.cursorGrid;
        pane.valueDecimals = binding.valueDecimals;
        document.panes.push_back(std::move(pane));
        error.clear();
        return &document.panes.back();
    }

    bool IndicatorRenderAdapter::RebuildLineCache(
        const IndicatorOutputBinding& binding,
        const IndicatorSeriesSnapshot& series,
        std::uint64_t completedRevision,
        BindingCache& cache,
        std::string& error) const
    {
        cache.lineSegments.clear();
        cache.histogramPoints.reset();
        cache.completedEndsFinite = false;

        auto current = std::make_shared<std::vector<render::LinePoint>>();
        if (series.completedValues) {
            for (const indicators::IndicatorValue& value :
                 *series.completedValues)
            {
                if (IsFiniteReady(value, binding.outputIndex)) {
                    render::LinePoint point;
                    point.timestampMs = value.timestampMs;
                    point.value = value.Value(binding.outputIndex);
                    current->push_back(point);
                    cache.completedEndsFinite = true;
                }
                else {
                    if (!current->empty()) {
                        cache.lineSegments.push_back(current);
                        current = std::make_shared<
                            std::vector<render::LinePoint>>();
                    }
                    cache.completedEndsFinite = false;
                }
            }
        }
        if (!current->empty()) {
            cache.lineSegments.push_back(current);
        }

        cache.completedRevision = completedRevision;
        cache.completedIdentity = series.completedValues.get();
        error.clear();
        return true;
    }

    bool IndicatorRenderAdapter::RebuildHistogramCache(
        const IndicatorOutputBinding& binding,
        const IndicatorSeriesSnapshot& series,
        std::uint64_t completedRevision,
        BindingCache& cache,
        std::string& error) const
    {
        auto points =
            std::make_shared<std::vector<render::HistogramPoint>>();
        if (series.completedValues) {
            points->reserve(series.completedValues->size());
            for (const indicators::IndicatorValue& value :
                 *series.completedValues)
            {
                if (!IsFiniteReady(value, binding.outputIndex)) continue;
                render::HistogramPoint point;
                point.timestampMs = value.timestampMs;
                point.value = value.Value(binding.outputIndex);
                point.positive = point.value >= 0.0;
                points->push_back(point);
            }
        }

        cache.lineSegments.clear();
        cache.histogramPoints = points;
        cache.completedEndsFinite = false;
        cache.completedRevision = completedRevision;
        cache.completedIdentity = series.completedValues.get();
        error.clear();
        return true;
    }

    std::uint64_t IndicatorRenderAdapter::MixRevision(
        std::uint64_t seed,
        std::uint64_t value) noexcept
    {
        seed ^= value + 0x9e3779b97f4a7c15ULL +
            (seed << 6U) + (seed >> 2U);
        return seed;
    }
}
