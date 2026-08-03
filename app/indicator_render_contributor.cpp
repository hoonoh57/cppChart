#include "indicator_render_contributor.h"

#include "../core/adx_indicator.h"
#include "../core/jma_indicator.h"
#include "../core/obv_indicator.h"
#include "../core/vwap_indicator.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace trading::app
{
    namespace
    {
        using IndicatorValue = indicators::IndicatorValue;
        using IndicatorSeriesSnapshot = app::IndicatorSeriesSnapshot;

        bool IsVisibleLevel(FeatureLevel level) noexcept
        {
            return
                level == FeatureLevel::Visible ||
                level == FeatureLevel::Active;
        }

        render::Pane* FindPricePane(render::RenderDocument& document) noexcept
        {
            for (render::Pane& pane : document.panes) {
                if (pane.id == "price") return &pane;
            }
            for (render::Pane& pane : document.panes) {
                if (!pane.candles.empty()) return &pane;
            }
            return nullptr;
        }

        render::Pane& AddIndicatorPane(
            render::RenderDocument& document,
            const std::string& id,
            const std::string& title,
            float heightWeight,
            int valueDecimals,
            IndicatorRenderContributionStats& stats)
        {
            render::Pane pane;
            pane.id = id;
            pane.title = title;
            pane.heightWeight = heightWeight;
            pane.valueDecimals = valueDecimals;
            document.panes.push_back(std::move(pane));
            ++stats.paneCount;
            return document.panes.back();
        }

        template <typename Callback>
        void ForEachValue(
            const IndicatorSeriesSnapshot& series,
            Callback callback)
        {
            if (series.completedValues) {
                for (const IndicatorValue& value : *series.completedValues) {
                    callback(value);
                }
            }
            if (series.hasLiveValue) callback(series.liveValue);
        }

        bool TryOutput(
            const IndicatorValue& value,
            std::size_t outputIndex,
            double& output) noexcept
        {
            if (!value.IsReady(outputIndex)) return false;
            output = value.Value(outputIndex);
            return std::isfinite(output);
        }

        void AppendLinePoint(
            std::vector<render::LinePoint>& points,
            EpochMillis timestampMs,
            double value)
        {
            if (
                !points.empty() &&
                points.back().timestampMs == timestampMs)
            {
                points.back().value = value;
                return;
            }
            points.push_back({ timestampMs, value });
        }

        std::vector<render::LinePoint> BuildLinePoints(
            const IndicatorSeriesSnapshot& series,
            std::size_t outputIndex)
        {
            std::vector<render::LinePoint> points;
            if (series.completedValues) {
                points.reserve(
                    series.completedValues->size() +
                    (series.hasLiveValue ? 1U : 0U));
            }
            ForEachValue(series, [&](const IndicatorValue& value) {
                double output = 0.0;
                if (!TryOutput(value, outputIndex, output)) return;
                AppendLinePoint(points, value.timestampMs, output);
            });
            return points;
        }

        void AppendHistogramPoint(
            std::vector<render::HistogramPoint>& points,
            EpochMillis timestampMs,
            double value)
        {
            const render::HistogramPoint point{
                timestampMs,
                value,
                value >= 0.0
            };
            if (
                !points.empty() &&
                points.back().timestampMs == timestampMs)
            {
                points.back() = point;
                return;
            }
            points.push_back(point);
        }

        std::vector<render::HistogramPoint> BuildHistogramPoints(
            const IndicatorSeriesSnapshot& series,
            std::size_t outputIndex)
        {
            std::vector<render::HistogramPoint> points;
            if (series.completedValues) {
                points.reserve(
                    series.completedValues->size() +
                    (series.hasLiveValue ? 1U : 0U));
            }
            ForEachValue(series, [&](const IndicatorValue& value) {
                double output = 0.0;
                if (!TryOutput(value, outputIndex, output)) return;
                AppendHistogramPoint(points, value.timestampMs, output);
            });
            return points;
        }

        void AddLine(
            render::Pane& pane,
            const std::string& id,
            const std::string& label,
            std::vector<render::LinePoint> points,
            render::ColorRgba color,
            float width,
            IndicatorRenderContributionStats& stats)
        {
            if (points.empty()) return;
            render::LineSeries line;
            line.id = id;
            line.label = label;
            line.points = std::move(points);
            line.color = color;
            line.width = width;
            pane.lines.push_back(std::move(line));
            ++stats.lineSeriesCount;
        }

        void AddSegmentedLine(
            render::Pane& pane,
            const IndicatorSeriesSnapshot& series,
            std::size_t outputIndex,
            const std::string& idPrefix,
            const std::string& label,
            render::ColorRgba color,
            float width,
            IndicatorRenderContributionStats& stats)
        {
            std::vector<render::LinePoint> segment;
            std::size_t segmentIndex = 0;

            auto flush = [&]() {
                if (segment.empty()) return;
                AddLine(
                    pane,
                    idPrefix + ".segment." +
                        std::to_string(segmentIndex++),
                    label,
                    std::move(segment),
                    color,
                    width,
                    stats);
                segment.clear();
            };

            ForEachValue(series, [&](const IndicatorValue& value) {
                double output = 0.0;
                if (!TryOutput(value, outputIndex, output)) {
                    flush();
                    return;
                }
                AppendLinePoint(segment, value.timestampMs, output);
            });
            flush();
        }

        void AddHistogram(
            render::Pane& pane,
            const std::string& id,
            const std::string& label,
            std::vector<render::HistogramPoint> points,
            IndicatorRenderContributionStats& stats)
        {
            if (points.empty()) return;
            render::HistogramSeries histogram;
            histogram.id = id;
            histogram.label = label;
            histogram.points = std::move(points);
            pane.histograms.push_back(std::move(histogram));
            ++stats.histogramSeriesCount;
        }

        void AddReferenceLine(
            render::Pane& pane,
            const std::string& id,
            const std::string& label,
            double value,
            IndicatorRenderContributionStats& stats)
        {
            render::ReferenceLine line;
            line.id = id;
            line.label = label;
            line.value = value;
            line.color = { 170, 174, 188, 180 };
            line.width = 1.0f;
            pane.referenceLines.push_back(std::move(line));
            ++stats.referenceLineCount;
        }

        std::string PeriodLabel(
            const IndicatorSeriesSnapshot& series,
            const std::string& name)
        {
            const auto found = series.spec.parameters.find("period");
            if (found == series.spec.parameters.end()) return name;
            return
                name + " " +
                std::to_string(static_cast<int>(found->second));
        }

        void AddSmaContribution(
            render::Pane& pricePane,
            const IndicatorSeriesSnapshot& series,
            IndicatorRenderContributionStats& stats)
        {
            AddLine(
                pricePane,
                "indicator." + series.spec.id + ".value",
                PeriodLabel(series, "SMA"),
                BuildLinePoints(series, 0U),
                { 255, 210, 64, 255 },
                1.5f,
                stats);
        }

        void AddJmaContribution(
            render::RenderDocument& document,
            render::Pane& pricePane,
            const IndicatorSeriesSnapshot& series,
            IndicatorRenderContributionStats& stats)
        {
            const std::string prefix = "indicator." + series.spec.id;
            AddLine(
                pricePane,
                prefix + ".value",
                PeriodLabel(series, "JMA"),
                BuildLinePoints(series, indicators::JmaValueOutput),
                { 232, 232, 238, 210 },
                1.0f,
                stats);
            AddSegmentedLine(
                pricePane,
                series,
                indicators::JmaUpOutput,
                prefix + ".up",
                "JMA Up",
                { 58, 196, 125, 255 },
                2.0f,
                stats);
            AddSegmentedLine(
                pricePane,
                series,
                indicators::JmaDownOutput,
                prefix + ".down",
                "JMA Down",
                { 235, 80, 92, 255 },
                2.0f,
                stats);

            render::Pane& slopePane = AddIndicatorPane(
                document,
                prefix + ".slope.pane",
                PeriodLabel(series, "JMA Slope"),
                0.22f,
                2,
                stats);
            slopePane.valueScale = render::PaneValueScale::Symmetric;
            AddHistogram(
                slopePane,
                prefix + ".slope",
                "Slope %",
                BuildHistogramPoints(series, indicators::JmaSlopeOutput),
                stats);
            AddReferenceLine(
                slopePane,
                prefix + ".slope.zero",
                "Zero",
                0.0,
                stats);
        }

        void AddVwapContribution(
            render::Pane& pricePane,
            const IndicatorSeriesSnapshot& series,
            IndicatorRenderContributionStats& stats)
        {
            const std::string prefix = "indicator." + series.spec.id;
            const std::size_t outputs[] = {
                indicators::VwapValueOutput,
                indicators::VwapUpper1Output,
                indicators::VwapLower1Output,
                indicators::VwapUpper2Output,
                indicators::VwapLower2Output
            };
            const char* suffixes[] = {
                "value", "upper1", "lower1", "upper2", "lower2"
            };
            const char* labels[] = {
                "VWAP", "VWAP +1", "VWAP -1", "VWAP +2", "VWAP -2"
            };
            const render::ColorRgba colors[] = {
                { 64, 210, 225, 255 },
                { 95, 172, 235, 230 },
                { 95, 172, 235, 230 },
                { 125, 132, 220, 190 },
                { 125, 132, 220, 190 }
            };

            for (std::size_t index = 0; index < 5U; ++index) {
                AddLine(
                    pricePane,
                    prefix + "." + suffixes[index],
                    labels[index],
                    BuildLinePoints(series, outputs[index]),
                    colors[index],
                    index == 0U ? 1.8f : 1.0f,
                    stats);
            }
        }

        void AddObvContribution(
            render::RenderDocument& document,
            const IndicatorSeriesSnapshot& series,
            IndicatorRenderContributionStats& stats)
        {
            const std::string prefix = "indicator." + series.spec.id;
            render::Pane& pane = AddIndicatorPane(
                document,
                prefix + ".pane",
                "OBV",
                0.3f,
                0,
                stats);
            AddLine(
                pane,
                prefix + ".value",
                "OBV",
                BuildLinePoints(series, indicators::ObvValueOutput),
                { 224, 224, 230, 255 },
                1.5f,
                stats);
            AddLine(
                pane,
                prefix + ".signal",
                "OBV Signal",
                BuildLinePoints(series, indicators::ObvSignalOutput),
                { 255, 196, 64, 255 },
                1.2f,
                stats);
            AddHistogram(
                pane,
                prefix + ".direction",
                "Direction",
                BuildHistogramPoints(
                    series,
                    indicators::ObvDirectionOutput),
                stats);
        }

        void AddAdxContribution(
            render::RenderDocument& document,
            const IndicatorSeriesSnapshot& series,
            IndicatorRenderContributionStats& stats)
        {
            const std::string prefix = "indicator." + series.spec.id;
            render::Pane& pane = AddIndicatorPane(
                document,
                prefix + ".pane",
                PeriodLabel(series, "ADX"),
                0.25f,
                2,
                stats);
            pane.valueScale = render::PaneValueScale::Fixed;
            pane.fixedMinimum = 0.0;
            pane.fixedMaximum = 100.0;
            AddLine(
                pane,
                prefix + ".value",
                "ADX",
                BuildLinePoints(series, indicators::AdxValueOutput),
                { 182, 120, 255, 255 },
                1.6f,
                stats);
            AddReferenceLine(
                pane,
                prefix + ".reference.20",
                "20",
                20.0,
                stats);
            AddReferenceLine(
                pane,
                prefix + ".reference.25",
                "25",
                25.0,
                stats);
        }
    }

    bool AppendIndicatorRenderContributions(
        const IndicatorModuleSnapshot& snapshot,
        render::RenderDocument& document,
        IndicatorRenderContributionStats& stats,
        std::string& error)
    {
        stats = {};
        error.clear();

        if (snapshot.state == IndicatorModuleState::Error) {
            error = snapshot.error.empty()
                ? "indicator module is in Error state"
                : snapshot.error;
            return false;
        }
        if (!IsVisibleLevel(snapshot.level)) return true;
        if (snapshot.state != IndicatorModuleState::Ready) {
            error = "indicator module is not Ready";
            return false;
        }

        document.panes.reserve(document.panes.size() + snapshot.series.size());
        render::Pane* pricePane = FindPricePane(document);
        if (pricePane == nullptr) {
            error = "indicator contributions require a price pane";
            return false;
        }

        for (const IndicatorSeriesSnapshot& series : snapshot.series) {
            if (series.spec.type == "SMA") {
                AddSmaContribution(*pricePane, series, stats);
            }
            else if (series.spec.type == "JMA") {
                AddJmaContribution(document, *pricePane, series, stats);
            }
            else if (series.spec.type == "VWAP") {
                AddVwapContribution(*pricePane, series, stats);
            }
            else if (series.spec.type == "OBV") {
                AddObvContribution(document, series, stats);
            }
            else if (series.spec.type == "ADX") {
                AddAdxContribution(document, series, stats);
            }
            else {
                error = "unsupported indicator render type: " + series.spec.type;
                return false;
            }
        }

        document.revision =
            document.revision * 1315423911ULL +
            snapshot.calculationRevision + 1ULL;
        document.structureRevision =
            document.structureRevision * 2654435761ULL +
            static_cast<std::uint64_t>(stats.paneCount + stats.TotalSeries()) +
            1ULL;

        if (!render::ValidateRenderDocument(document, error)) return false;
        error.clear();
        return true;
    }
}
