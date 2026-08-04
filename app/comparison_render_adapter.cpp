#include "comparison_render_adapter.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace trading::app
{
    namespace
    {
        bool VisibleLevel(FeatureLevel level) noexcept
        {
            return level == FeatureLevel::Visible ||
                   level == FeatureLevel::Active;
        }

        void MixRevision(std::uint64_t& seed, std::uint64_t value) noexcept
        {
            seed ^=
                value + 0x9e3779b97f4a7c15ULL +
                (seed << 6U) + (seed >> 2U);
        }

        int ModeDecimals(ComparisonValueMode mode, int configured) noexcept
        {
            if (mode == ComparisonValueMode::RawClose) return configured;
            return 2;
        }
    }

    bool ComparisonRenderAdapter::Apply(
        const ComparisonModuleSnapshot& snapshot,
        render::RenderDocument& document,
        std::string& error)
    {
        if (!VisibleLevel(snapshot.level)) {
            error.clear();
            return true;
        }

        std::vector<Bar> primaryBars;
        const void* primaryIdentity = nullptr;
        if (!CopyPrimaryBars(document, primaryBars, primaryIdentity)) {
            error = "primary candle series is missing for comparison transform";
            return false;
        }

        std::set<std::string> activeIds;
        std::uint64_t nextRevision = snapshot.revision;
        for (const ComparisonSeriesSnapshot& source : snapshot.series) {
            if (!source.definition.visible) continue;
            if (source.state != ComparisonSeriesState::Ready ||
                !source.hasLiveBar || !source.completedBars)
            {
                continue;
            }
            if (!activeIds.insert(source.definition.id).second) {
                error = "duplicate comparison render id: " +
                    source.definition.id;
                return false;
            }
            if (!std::isfinite(source.definition.valueDivisor) ||
                source.definition.valueDivisor <= 0.0)
            {
                error = "comparison value divisor is invalid: " +
                    source.definition.id;
                return false;
            }

            Cache& cache = caches_[source.definition.id];
            const void* identity = source.completedBars.get();
            if (!cache.completedPoints ||
                cache.completedIdentity != identity ||
                cache.primaryIdentity != primaryIdentity ||
                cache.completedRevision != source.completedRevision ||
                cache.valueMode != source.definition.valueMode ||
                std::fabs(
                    cache.valueDivisor -
                    source.definition.valueDivisor) > 1e-12)
            {
                cache.transformed = TransformComparisonBars(
                    *source.completedBars,
                    primaryBars,
                    source.definition.valueMode,
                    source.definition.valueDivisor);
                cache.completedPoints =
                    std::make_shared<const std::vector<render::LinePoint>>(
                        cache.transformed.points);
                cache.completedIdentity = identity;
                cache.primaryIdentity = primaryIdentity;
                cache.completedRevision = source.completedRevision;
                cache.valueDivisor = source.definition.valueDivisor;
                cache.valueMode = source.definition.valueMode;
            }

            render::Pane* pane = nullptr;
            std::string axisId;
            const int decimals = ModeDecimals(
                source.definition.valueMode,
                source.definition.valueDecimals);
            if (source.definition.placement ==
                ComparisonPlacement::PriceSecondaryAxis)
            {
                pane = FindPane(document, "price");
                if (pane == nullptr) {
                    error = "price pane is missing for comparison overlay";
                    return false;
                }
                axisId =
                    "comparison." + source.definition.id + ".axis";
                render::ValueAxis axis;
                axis.id = axisId;
                axis.label = source.definition.displayName;
                axis.side = render::ValueAxisSide::Left;
                axis.valueScale = render::PaneValueScale::Auto;
                axis.valueDecimals = decimals;
                axis.color = source.definition.color;
                axis.cursorGrid.enabled = false;
                pane->valueAxes.push_back(std::move(axis));
            }
            else {
                const std::string paneId =
                    source.definition.paneId.empty()
                        ? "comparison." + source.definition.id + ".pane"
                        : source.definition.paneId;
                pane = FindPane(document, paneId);
                if (pane == nullptr) {
                    render::Pane created;
                    created.id = paneId;
                    created.title = source.definition.paneTitle.empty()
                        ? source.definition.displayName
                        : source.definition.paneTitle;
                    created.heightWeight =
                        source.definition.paneHeightWeight;
                    created.valueDecimals = decimals;
                    created.cursorGrid.enabled = false;
                    if (source.definition.valueMode ==
                        ComparisonValueMode::ReturnPercent)
                    {
                        created.valueScale = render::PaneValueScale::Symmetric;
                    }
                    document.panes.push_back(std::move(created));
                    pane = &document.panes.back();
                }
            }

            render::LegendEntry legend;
            legend.id =
                "comparison." + source.definition.id + ".legend";
            legend.ownerId = source.definition.id;
            legend.label = source.definition.displayName + " " +
                source.definition.code + " · " +
                ComparisonValueModeName(source.definition.valueMode);
            legend.color = source.definition.color;
            legend.width = source.definition.width;
            legend.style = source.definition.style;
            pane->legends.push_back(std::move(legend));

            render::LinePoint livePoint;
            const bool hasLivePoint = TransformComparisonLivePoint(
                source.liveBar,
                primaryBars,
                cache.transformed,
                source.definition.valueMode,
                source.definition.valueDivisor,
                livePoint);

            render::LineSeries line;
            line.id =
                "comparison." + source.definition.id + ".close";
            line.ownerId = source.definition.id;
            line.label = source.definition.displayName;
            line.axisId = axisId;
            line.color = source.definition.color;
            line.width = source.definition.width;
            line.style = source.definition.style;
            line.points.SetShared(
                cache.completedPoints,
                hasLivePoint ? &livePoint : nullptr);
            pane->lines.push_back(std::move(line));

            if (source.definition.valueMode != ComparisonValueMode::RawClose &&
                cache.transformed.hasAnchor)
            {
                render::ReferenceLine reference;
                reference.id =
                    "comparison." + source.definition.id + ".anchor";
                reference.ownerId = source.definition.id;
                reference.label = source.definition.valueMode ==
                    ComparisonValueMode::ReturnPercent ? "기준 0%" : "기준 100";
                reference.value = source.definition.valueMode ==
                    ComparisonValueMode::ReturnPercent ? 0.0 : 100.0;
                reference.color = { 150, 154, 166, 170 };
                reference.width = 1.0f;
                reference.style = render::LineStyle::Dashed;
                pane->referenceLines.push_back(std::move(reference));
            }

            MixRevision(nextRevision, source.revision);
            MixRevision(nextRevision, source.completedRevision);
            MixRevision(nextRevision, source.liveRevision);
            MixRevision(nextRevision,
                static_cast<std::uint64_t>(source.definition.valueMode));
        }

        for (auto iterator = caches_.begin(); iterator != caches_.end();) {
            if (activeIds.find(iterator->first) == activeIds.end()) {
                iterator = caches_.erase(iterator);
            }
            else {
                ++iterator;
            }
        }

        revision_ = nextRevision;
        MixRevision(document.revision, nextRevision);
        MixRevision(document.structureRevision, snapshot.revision);
        error.clear();
        return true;
    }

    std::uint64_t ComparisonRenderAdapter::Revision() const noexcept
    {
        return revision_;
    }

    std::size_t ComparisonRenderAdapter::RetainedBytes() const noexcept
    {
        std::size_t result = 0;
        for (const auto& entry : caches_) {
            result += entry.first.capacity();
            if (entry.second.completedPoints) {
                result +=
                    entry.second.completedPoints->capacity() *
                    sizeof(render::LinePoint);
            }
            result += entry.second.transformed.points.capacity() *
                sizeof(render::LinePoint);
        }
        return result;
    }

    void ComparisonRenderAdapter::ClearCache() noexcept
    {
        caches_.clear();
        ++revision_;
    }

    render::Pane* ComparisonRenderAdapter::FindPane(
        render::RenderDocument& document,
        const std::string& paneId) noexcept
    {
        for (render::Pane& pane : document.panes) {
            if (pane.id == paneId) return &pane;
        }
        return nullptr;
    }

    bool ComparisonRenderAdapter::CopyPrimaryBars(
        const render::RenderDocument& document,
        std::vector<Bar>& bars,
        const void*& identity)
    {
        for (const render::Pane& pane : document.panes) {
            for (const render::CandleSeries& candles : pane.candles) {
                if (!candles.visible || candles.bars.empty()) continue;
                bars.reserve(candles.bars.size());
                for (const Bar& bar : candles.bars) bars.push_back(bar);
                identity = candles.bars.SharedPrefix().get();
                if (identity == nullptr) identity = &candles;
                return true;
            }
        }
        return false;
    }
}
