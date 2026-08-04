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
                cache.completedRevision != source.completedRevision ||
                std::fabs(
                    cache.valueDivisor -
                    source.definition.valueDivisor) > 1e-12)
            {
                cache.completedPoints = BuildCompletedPoints(
                    source.completedBars,
                    source.definition.valueDivisor);
                cache.completedIdentity = identity;
                cache.completedRevision = source.completedRevision;
                cache.valueDivisor = source.definition.valueDivisor;
            }

            render::Pane* pane = nullptr;
            std::string axisId;
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
                axis.valueDecimals = source.definition.valueDecimals;
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
                    created.valueDecimals =
                        source.definition.valueDecimals;
                    created.cursorGrid.enabled = false;
                    document.panes.push_back(std::move(created));
                    pane = &document.panes.back();
                }
            }

            render::LegendEntry legend;
            legend.id =
                "comparison." + source.definition.id + ".legend";
            legend.ownerId = source.definition.id;
            legend.label = source.definition.displayName + " " +
                source.definition.code;
            legend.color = source.definition.color;
            legend.width = source.definition.width;
            legend.style = source.definition.style;
            pane->legends.push_back(std::move(legend));

            render::LinePoint livePoint;
            livePoint.timestampMs = source.liveBar.closeTimestampMs;
            livePoint.value =
                static_cast<double>(source.liveBar.close) /
                source.definition.valueDivisor;

            render::LineSeries line;
            line.id =
                "comparison." + source.definition.id + ".close";
            line.ownerId = source.definition.id;
            line.label = source.definition.displayName;
            line.axisId = axisId;
            line.color = source.definition.color;
            line.width = source.definition.width;
            line.style = source.definition.style;
            line.points.SetShared(cache.completedPoints, &livePoint);
            pane->lines.push_back(std::move(line));

            MixRevision(nextRevision, source.revision);
            MixRevision(nextRevision, source.completedRevision);
            MixRevision(nextRevision, source.liveRevision);
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

    std::shared_ptr<const std::vector<render::LinePoint>>
    ComparisonRenderAdapter::BuildCompletedPoints(
        const std::shared_ptr<const std::vector<Bar>>& bars,
        double divisor)
    {
        auto points =
            std::make_shared<std::vector<render::LinePoint>>();
        if (bars) {
            points->reserve(bars->size());
            for (const Bar& bar : *bars) {
                render::LinePoint point;
                point.timestampMs = bar.closeTimestampMs;
                point.value =
                    static_cast<double>(bar.close) / divisor;
                points->push_back(point);
            }
        }
        return points;
    }
}
