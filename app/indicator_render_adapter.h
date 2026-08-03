#pragma once

#include "indicator_module.h"
#include "../render/render_document.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace trading::app
{
    enum class IndicatorRenderKind
    {
        Line,
        Histogram
    };

    struct IndicatorOutputBinding final
    {
        std::string indicatorId;
        std::size_t outputIndex = 0;
        IndicatorRenderKind kind = IndicatorRenderKind::Line;
        std::string paneId;
        std::string paneTitle;
        float paneHeightWeight = 0.3f;
        render::PaneValueScale paneValueScale =
            render::PaneValueScale::Auto;
        double fixedMinimum = 0.0;
        double fixedMaximum = 0.0;
        render::ValueGrid cursorGrid;
        int valueDecimals = 2;
        std::string seriesId;
        std::string label;
        render::ColorRgba primaryColor;
        render::ColorRgba secondaryColor;
        float width = 1.0f;
        bool visible = true;
    };

    struct IndicatorRenderPlan final
    {
        std::vector<IndicatorOutputBinding> bindings;
    };

    class IndicatorRenderAdapter final
    {
    public:
        bool Configure(
            const IndicatorRenderPlan& plan,
            std::string& error);

        bool Apply(
            const IndicatorModuleSnapshot& indicators,
            render::RenderDocument& document,
            std::string& error);

        void Reset() noexcept;

        std::uint64_t PlanRevision() const noexcept;
        std::size_t RetainedBytes() const noexcept;

    private:
        struct BindingCache final
        {
            std::uint64_t completedRevision = 0;
            const void* completedIdentity = nullptr;
            std::vector<
                std::shared_ptr<const std::vector<render::LinePoint>>>
                lineSegments;
            std::shared_ptr<const std::vector<render::HistogramPoint>>
                histogramPoints;
            bool completedEndsFinite = false;
        };

        const IndicatorSeriesSnapshot* FindSeries(
            const IndicatorModuleSnapshot& indicators,
            const std::string& id) const noexcept;

        render::Pane* FindOrCreatePane(
            render::RenderDocument& document,
            const IndicatorOutputBinding& binding,
            std::string& error) const;

        bool RebuildLineCache(
            const IndicatorOutputBinding& binding,
            const IndicatorSeriesSnapshot& series,
            std::uint64_t completedRevision,
            BindingCache& cache,
            std::string& error) const;

        bool RebuildHistogramCache(
            const IndicatorOutputBinding& binding,
            const IndicatorSeriesSnapshot& series,
            std::uint64_t completedRevision,
            BindingCache& cache,
            std::string& error) const;

        static std::uint64_t MixRevision(
            std::uint64_t seed,
            std::uint64_t value) noexcept;

        IndicatorRenderPlan plan_;
        std::map<std::string, BindingCache> caches_;
        std::uint64_t planRevision_ = 0;
    };
}
