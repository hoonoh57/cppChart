#include "render_document_renderer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace trading::ui
{
    namespace
    {
        ImU32 ToImColor(const render::ColorRgba& color) noexcept
        {
            return IM_COL32(
                color.red,
                color.green,
                color.blue,
                color.alpha);
        }

        struct TimeRange final
        {
            EpochMillis minimum = 0;
            EpochMillis maximum = 0;
            bool valid = false;
        };

        struct ValueRange final
        {
            double minimum = 0.0;
            double maximum = 0.0;
            bool valid = false;

            void Include(double value) noexcept
            {
                if (!std::isfinite(value)) return;
                if (!valid) {
                    minimum = value;
                    maximum = value;
                    valid = true;
                    return;
                }
                minimum = (std::min)(minimum, value);
                maximum = (std::max)(maximum, value);
            }
        };

        void IncludeTimestamp(
            TimeRange& range,
            EpochMillis timestamp) noexcept
        {
            if (timestamp <= 0) return;
            if (!range.valid) {
                range.minimum = timestamp;
                range.maximum = timestamp;
                range.valid = true;
                return;
            }
            range.minimum = (std::min)(range.minimum, timestamp);
            range.maximum = (std::max)(range.maximum, timestamp);
        }

        TimeRange DocumentTimeRange(
            const render::RenderDocument& document)
        {
            TimeRange result;
            if (
                document.interaction.visibleStartMs > 0 &&
                document.interaction.visibleEndMs >
                    document.interaction.visibleStartMs)
            {
                result.minimum = document.interaction.visibleStartMs;
                result.maximum = document.interaction.visibleEndMs;
                result.valid = true;
                return result;
            }

            for (const render::Pane& pane : document.panes) {
                for (const render::CandleSeries& series : pane.candles) {
                    if (!series.visible) continue;
                    for (const Bar& bar : series.bars) {
                        IncludeTimestamp(result, bar.closeTimestampMs);
                    }
                }
                for (const render::LineSeries& series : pane.lines) {
                    if (!series.visible) continue;
                    for (const render::LinePoint& point : series.points) {
                        IncludeTimestamp(result, point.timestampMs);
                    }
                }
                for (const render::HistogramSeries& series : pane.histograms) {
                    if (!series.visible) continue;
                    for (const render::HistogramPoint& point : series.points) {
                        IncludeTimestamp(result, point.timestampMs);
                    }
                }
                for (const render::MarkerSeries& series : pane.markers) {
                    if (!series.visible) continue;
                    for (const render::MarkerPoint& point : series.points) {
                        IncludeTimestamp(result, point.timestampMs);
                    }
                }
            }
            return result;
        }

        bool InTimeRange(
            EpochMillis timestamp,
            const TimeRange& range) noexcept
        {
            return
                range.valid &&
                timestamp >= range.minimum &&
                timestamp <= range.maximum;
        }

        ValueRange PaneValueRange(
            const render::Pane& pane,
            const TimeRange& timeRange)
        {
            ValueRange result;
            if (pane.valueScale == render::PaneValueScale::Fixed) {
                result.minimum = pane.fixedMinimum;
                result.maximum = pane.fixedMaximum;
                result.valid = true;
                return result;
            }

            for (const render::CandleSeries& series : pane.candles) {
                if (!series.visible) continue;
                for (const Bar& bar : series.bars) {
                    if (!InTimeRange(bar.closeTimestampMs, timeRange)) continue;
                    result.Include(static_cast<double>(bar.low));
                    result.Include(static_cast<double>(bar.high));
                }
            }
            for (const render::LineSeries& series : pane.lines) {
                if (!series.visible) continue;
                for (const render::LinePoint& point : series.points) {
                    if (!InTimeRange(point.timestampMs, timeRange)) continue;
                    result.Include(point.value);
                }
            }
            for (const render::HistogramSeries& series : pane.histograms) {
                if (!series.visible) continue;
                result.Include(0.0);
                for (const render::HistogramPoint& point : series.points) {
                    if (!InTimeRange(point.timestampMs, timeRange)) continue;
                    result.Include(point.value);
                }
            }
            for (const render::MarkerSeries& series : pane.markers) {
                if (!series.visible) continue;
                for (const render::MarkerPoint& point : series.points) {
                    if (!InTimeRange(point.timestampMs, timeRange)) continue;
                    result.Include(point.value);
                }
            }
            for (const render::ReferenceLine& line : pane.referenceLines) {
                if (line.visible) result.Include(line.value);
            }

            if (!result.valid) return result;
            if (pane.valueScale == render::PaneValueScale::Symmetric) {
                const double absolute = (std::max)(
                    std::fabs(result.minimum),
                    std::fabs(result.maximum));
                result.minimum = -absolute;
                result.maximum = absolute;
            }
            if (result.maximum <= result.minimum) {
                const double padding =
                    (std::max)(1.0, std::fabs(result.maximum) * 0.01);
                result.minimum -= padding;
                result.maximum += padding;
            }
            else {
                const double padding =
                    (result.maximum - result.minimum) * 0.04;
                result.minimum -= padding;
                result.maximum += padding;
            }
            return result;
        }

        float MapX(
            EpochMillis timestamp,
            const TimeRange& range,
            float left,
            float width) noexcept
        {
            const EpochMillis span =
                (std::max)(static_cast<EpochMillis>(1),
                           range.maximum - range.minimum);
            const double ratio =
                static_cast<double>(timestamp - range.minimum) /
                static_cast<double>(span);
            return left +
                static_cast<float>((std::max)(0.0, (std::min)(1.0, ratio))) *
                width;
        }

        float MapY(
            double value,
            const ValueRange& range,
            float top,
            float height) noexcept
        {
            const double span =
                (std::max)(1e-12, range.maximum - range.minimum);
            const double ratio =
                (range.maximum - value) / span;
            return top +
                static_cast<float>((std::max)(0.0, (std::min)(1.0, ratio))) *
                height;
        }

        void DrawPane(
            const render::Pane& pane,
            const TimeRange& timeRange,
            ImVec2 size)
        {
            if (size.x < 40.0f || size.y < 30.0f) return;

            const ValueRange values = PaneValueRange(pane, timeRange);
            ImGui::PushID(pane.id.c_str());
            ImGui::InvisibleButton("##surface", size);
            const ImVec2 origin = ImGui::GetItemRectMin();
            const ImVec2 end = ImGui::GetItemRectMax();
            ImDrawList* draw = ImGui::GetWindowDrawList();

            draw->AddRectFilled(
                origin,
                end,
                IM_COL32(15, 16, 20, 255));
            draw->AddRect(
                origin,
                end,
                IM_COL32(70, 72, 82, 255));

            if (!values.valid || !timeRange.valid) {
                ImGui::PopID();
                return;
            }

            for (int grid = 1; grid < 5; ++grid) {
                const float y =
                    origin.y + size.y * static_cast<float>(grid) / 5.0f;
                draw->AddLine(
                    ImVec2(origin.x, y),
                    ImVec2(end.x, y),
                    IM_COL32(45, 47, 55, 255));
            }

            std::size_t visibleCandleCount = 0;
            for (const render::CandleSeries& series : pane.candles) {
                if (!series.visible) continue;
                for (const Bar& bar : series.bars) {
                    if (InTimeRange(bar.closeTimestampMs, timeRange)) {
                        ++visibleCandleCount;
                    }
                }
            }
            const float candleBodyWidth = (std::max)(
                1.0f,
                size.x /
                    static_cast<float>((std::max)(
                        static_cast<std::size_t>(1),
                        visibleCandleCount)) *
                    0.58f);

            for (const render::HistogramSeries& series : pane.histograms) {
                if (!series.visible) continue;
                for (const render::HistogramPoint& point : series.points) {
                    if (!InTimeRange(point.timestampMs, timeRange)) continue;
                    const float x = MapX(
                        point.timestampMs,
                        timeRange,
                        origin.x,
                        size.x);
                    const float y = MapY(
                        point.value,
                        values,
                        origin.y,
                        size.y);
                    const float zeroY = MapY(
                        0.0,
                        values,
                        origin.y,
                        size.y);
                    draw->AddRectFilled(
                        ImVec2(x - candleBodyWidth * 0.5f, (std::min)(y, zeroY)),
                        ImVec2(x + candleBodyWidth * 0.5f, (std::max)(y, zeroY)),
                        ToImColor(
                            point.positive
                                ? series.positiveColor
                                : series.negativeColor));
                }
            }

            for (const render::CandleSeries& series : pane.candles) {
                if (!series.visible) continue;
                for (const Bar& bar : series.bars) {
                    if (!InTimeRange(bar.closeTimestampMs, timeRange)) continue;
                    const float x = MapX(
                        bar.closeTimestampMs,
                        timeRange,
                        origin.x,
                        size.x);
                    const bool up = bar.close >= bar.open;
                    const ImU32 color = ToImColor(
                        up ? series.upColor : series.downColor);
                    draw->AddLine(
                        ImVec2(
                            x,
                            MapY(bar.high, values, origin.y, size.y)),
                        ImVec2(
                            x,
                            MapY(bar.low, values, origin.y, size.y)),
                        color,
                        1.0f);

                    float openY = MapY(
                        bar.open,
                        values,
                        origin.y,
                        size.y);
                    float closeY = MapY(
                        bar.close,
                        values,
                        origin.y,
                        size.y);
                    if (std::fabs(openY - closeY) < 1.0f) {
                        closeY = openY + 1.0f;
                    }
                    draw->AddRectFilled(
                        ImVec2(
                            x - candleBodyWidth * 0.5f,
                            (std::min)(openY, closeY)),
                        ImVec2(
                            x + candleBodyWidth * 0.5f,
                            (std::max)(openY, closeY)),
                        color);
                }
            }

            for (const render::LineSeries& series : pane.lines) {
                if (!series.visible) continue;
                bool hasPrevious = false;
                ImVec2 previous;
                for (const render::LinePoint& point : series.points) {
                    if (!InTimeRange(point.timestampMs, timeRange)) continue;
                    const ImVec2 current(
                        MapX(point.timestampMs, timeRange, origin.x, size.x),
                        MapY(point.value, values, origin.y, size.y));
                    if (hasPrevious) {
                        draw->AddLine(
                            previous,
                            current,
                            ToImColor(series.color),
                            series.width);
                    }
                    previous = current;
                    hasPrevious = true;
                }
            }

            for (const render::ReferenceLine& line : pane.referenceLines) {
                if (!line.visible) continue;
                const float y = MapY(
                    line.value,
                    values,
                    origin.y,
                    size.y);
                draw->AddLine(
                    ImVec2(origin.x, y),
                    ImVec2(end.x, y),
                    ToImColor(line.color),
                    line.width);
            }

            for (const render::MarkerSeries& series : pane.markers) {
                if (!series.visible) continue;
                for (const render::MarkerPoint& marker : series.points) {
                    if (!InTimeRange(marker.timestampMs, timeRange)) continue;
                    const ImVec2 point(
                        MapX(marker.timestampMs, timeRange, origin.x, size.x),
                        MapY(marker.value, values, origin.y, size.y));
                    draw->AddCircleFilled(
                        point,
                        4.0f,
                        ToImColor(marker.color));
                }
            }

            for (const render::TextAnnotation& annotation : pane.annotations) {
                if (!annotation.visible ||
                    !InTimeRange(annotation.timestampMs, timeRange))
                {
                    continue;
                }
                const ImVec2 point(
                    MapX(annotation.timestampMs, timeRange, origin.x, size.x),
                    MapY(annotation.value, values, origin.y, size.y));
                draw->AddText(
                    point,
                    ToImColor(annotation.color),
                    annotation.text.c_str());
            }

            ImGui::PopID();
        }
    }

    void DrawRenderDocument(
        const render::RenderDocument& document,
        ImVec2 size,
        RenderSurfaceState& surfaceState)
    {
        const TimeRange timeRange = DocumentTimeRange(document);
        if (!timeRange.valid || document.panes.empty()) return;

        float totalWeight = 0.0f;
        for (const render::Pane& pane : document.panes) {
            totalWeight += (std::max)(0.01f, pane.heightWeight);
        }

        const float spacing =
            ImGui::GetStyle().ItemSpacing.y *
            static_cast<float>((std::max)(
                static_cast<std::size_t>(0),
                document.panes.size() - 1));
        const float availableHeight = (std::max)(0.0f, size.y - spacing);

        for (std::size_t index = 0; index < document.panes.size(); ++index) {
            const render::Pane& pane = document.panes[index];
            const float paneHeight =
                availableHeight *
                (std::max)(0.01f, pane.heightWeight) /
                totalWeight;
            DrawPane(
                pane,
                timeRange,
                ImVec2(size.x, paneHeight));
            if (index + 1 < document.panes.size()) {
                ImGui::Dummy(ImVec2(0.0f, 0.0f));
            }
        }

        surfaceState.renderedRevision = document.revision;
        surfaceState.dirty = false;
    }
}
