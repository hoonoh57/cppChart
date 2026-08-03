#include "render_document.h"

#include <cmath>
#include <set>

namespace trading::render
{
    namespace
    {
        bool AddUnique(
            std::set<std::string>& ids,
            const std::string& id,
            std::string& error)
        {
            if (id.empty()) {
                error = "render element id is empty";
                return false;
            }
            if (!ids.insert(id).second) {
                error = "duplicate render element id: " + id;
                return false;
            }
            return true;
        }

        bool ValidColor(const ColorRgba&) noexcept
        {
            return true;
        }
    }

    bool ValidateRenderDocument(
        const RenderDocument& document,
        std::string& error)
    {
        if (document.workspaceId.empty()) {
            error = "render workspace id is empty";
            return false;
        }
        if (document.panes.empty()) {
            error = "render document has no panes";
            return false;
        }

        std::set<std::string> paneIds;
        std::set<std::string> elementIds;

        for (const Pane& pane : document.panes) {
            if (!paneIds.insert(pane.id).second || pane.id.empty()) {
                error = "duplicate or empty render pane id";
                return false;
            }
            if (!std::isfinite(pane.heightWeight) || pane.heightWeight <= 0.0f) {
                error = "render pane height weight is invalid: " + pane.id;
                return false;
            }
            if (
                pane.valueScale == PaneValueScale::Fixed &&
                (!std::isfinite(pane.fixedMinimum) ||
                 !std::isfinite(pane.fixedMaximum) ||
                 pane.fixedMaximum <= pane.fixedMinimum))
            {
                error = "fixed render pane range is invalid: " + pane.id;
                return false;
            }

            for (const CandleSeries& series : pane.candles) {
                if (!AddUnique(elementIds, series.id, error)) return false;
                if (!ValidColor(series.upColor) || !ValidColor(series.downColor)) {
                    error = "candle series color is invalid";
                    return false;
                }
                EpochMillis previous = 0;
                for (const Bar& bar : series.bars) {
                    if (
                        !IsValidPrice(bar.open) ||
                        !IsValidPrice(bar.high) ||
                        !IsValidPrice(bar.low) ||
                        !IsValidPrice(bar.close) ||
                        bar.high < bar.low ||
                        bar.high < bar.open ||
                        bar.high < bar.close ||
                        bar.low > bar.open ||
                        bar.low > bar.close ||
                        bar.closeTimestampMs <= 0 ||
                        bar.volume < 0 ||
                        bar.tickCount < 0)
                    {
                        error = "candle series contains invalid bar: " + series.id;
                        return false;
                    }
                    if (previous >= bar.closeTimestampMs) {
                        error = "candle series timestamps are not strictly increasing: " + series.id;
                        return false;
                    }
                    previous = bar.closeTimestampMs;
                }
            }

            for (const LineSeries& series : pane.lines) {
                if (!AddUnique(elementIds, series.id, error)) return false;
                if (!std::isfinite(series.width) || series.width <= 0.0f) {
                    error = "line series width is invalid: " + series.id;
                    return false;
                }
                EpochMillis previous = 0;
                for (const LinePoint& point : series.points) {
                    if (
                        point.timestampMs <= 0 ||
                        std::isinf(point.value) ||
                        previous >= point.timestampMs)
                    {
                        error = "line series point is invalid: " + series.id;
                        return false;
                    }
                    previous = point.timestampMs;
                }
            }

            for (const HistogramSeries& series : pane.histograms) {
                if (!AddUnique(elementIds, series.id, error)) return false;
                EpochMillis previous = 0;
                for (const HistogramPoint& point : series.points) {
                    if (
                        point.timestampMs <= 0 ||
                        !std::isfinite(point.value) ||
                        previous >= point.timestampMs)
                    {
                        error = "histogram series point is invalid: " + series.id;
                        return false;
                    }
                    previous = point.timestampMs;
                }
            }

            for (const MarkerSeries& series : pane.markers) {
                if (!AddUnique(elementIds, series.id, error)) return false;
                for (const MarkerPoint& point : series.points) {
                    if (point.timestampMs <= 0 || !std::isfinite(point.value)) {
                        error = "marker series point is invalid: " + series.id;
                        return false;
                    }
                }
            }

            for (const ReferenceLine& line : pane.referenceLines) {
                if (!AddUnique(elementIds, line.id, error)) return false;
                if (!std::isfinite(line.value) ||
                    !std::isfinite(line.width) ||
                    line.width <= 0.0f)
                {
                    error = "reference line is invalid: " + line.id;
                    return false;
                }
            }

            for (const TextAnnotation& annotation : pane.annotations) {
                if (!AddUnique(elementIds, annotation.id, error)) return false;
                if (
                    annotation.timestampMs <= 0 ||
                    !std::isfinite(annotation.value) ||
                    annotation.text.empty())
                {
                    error = "text annotation is invalid: " + annotation.id;
                    return false;
                }
            }
        }

        error.clear();
        return true;
    }
}
