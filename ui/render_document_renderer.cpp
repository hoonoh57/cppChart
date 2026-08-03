#include "render_document_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <string>

namespace trading::ui
{
    namespace
    {
        constexpr float ValueAxisWidth = 74.0f;
        constexpr float TimeAxisHeight = 20.0f;

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

        TimeRange DocumentDataRange(
            const render::RenderDocument& document)
        {
            TimeRange result;
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

        EpochMillis MinimumViewportSpan(
            const render::RenderDocument& document) noexcept
        {
            EpochMillis minimumStep = 0;
            for (const render::Pane& pane : document.panes) {
                for (const render::CandleSeries& series : pane.candles) {
                    EpochMillis previous = 0;
                    for (const Bar& bar : series.bars) {
                        if (previous > 0 && bar.closeTimestampMs > previous) {
                            const EpochMillis step =
                                bar.closeTimestampMs - previous;
                            minimumStep = minimumStep == 0
                                ? step
                                : (std::min)(minimumStep, step);
                        }
                        previous = bar.closeTimestampMs;
                    }
                }
            }
            return (std::max)(
                static_cast<EpochMillis>(1000),
                minimumStep > 0
                    ? minimumStep * 12
                    : static_cast<EpochMillis>(1000));
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
                    if (InTimeRange(point.timestampMs, timeRange)) {
                        result.Include(point.value);
                    }
                }
            }
            for (const render::HistogramSeries& series : pane.histograms) {
                if (!series.visible) continue;
                result.Include(0.0);
                for (const render::HistogramPoint& point : series.points) {
                    if (InTimeRange(point.timestampMs, timeRange)) {
                        result.Include(point.value);
                    }
                }
            }
            for (const render::MarkerSeries& series : pane.markers) {
                if (!series.visible) continue;
                for (const render::MarkerPoint& point : series.points) {
                    if (InTimeRange(point.timestampMs, timeRange)) {
                        result.Include(point.value);
                    }
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
            const EpochMillis span = (std::max)(
                static_cast<EpochMillis>(1),
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
            const double ratio = (range.maximum - value) / span;
            return top +
                static_cast<float>((std::max)(0.0, (std::min)(1.0, ratio))) *
                height;
        }

        double UnmapY(
            float y,
            const ValueRange& range,
            float top,
            float height) noexcept
        {
            const double ratio = (std::max)(
                0.0,
                (std::min)(1.0,
                    static_cast<double>(y - top) /
                    static_cast<double>((std::max)(1.0f, height))));
            return range.maximum -
                ratio * (range.maximum - range.minimum);
        }

        std::string FormatTimestamp(EpochMillis timestampMs)
        {
            const std::time_t seconds = static_cast<std::time_t>(
                timestampMs / 1000);
            std::tm local{};
#if defined(_WIN32)
            localtime_s(&local, &seconds);
#else
            localtime_r(&seconds, &local);
#endif
            char buffer[32]{};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "%02d/%02d %02d:%02d",
                local.tm_mon + 1,
                local.tm_mday,
                local.tm_hour,
                local.tm_min);
            return buffer;
        }

        const Bar* NearestVisibleBar(
            const render::Pane& pane,
            EpochMillis timestamp,
            const TimeRange& timeRange)
        {
            const Bar* nearest = nullptr;
            EpochMillis nearestDistance =
                (std::numeric_limits<EpochMillis>::max)();
            for (const render::CandleSeries& series : pane.candles) {
                if (!series.visible) continue;
                for (const Bar& bar : series.bars) {
                    if (!InTimeRange(bar.closeTimestampMs, timeRange)) continue;
                    const EpochMillis distance =
                        bar.closeTimestampMs > timestamp
                            ? bar.closeTimestampMs - timestamp
                            : timestamp - bar.closeTimestampMs;
                    if (distance < nearestDistance) {
                        nearest = &bar;
                        nearestDistance = distance;
                    }
                }
            }
            return nearest;
        }

        bool LatestCandleClose(
            const render::Pane& pane,
            PriceWon& price,
            EpochMillis& timestamp)
        {
            bool found = false;
            for (const render::CandleSeries& series : pane.candles) {
                if (!series.visible || series.bars.empty()) continue;
                const Bar& bar = series.bars.back();
                if (!found || bar.closeTimestampMs > timestamp) {
                    price = bar.close;
                    timestamp = bar.closeTimestampMs;
                    found = true;
                }
            }
            return found;
        }

        void DrawValueAxis(
            ImDrawList* draw,
            const ValueRange& values,
            const ImVec2& plotOrigin,
            float plotWidth,
            float plotHeight)
        {
            char label[48]{};
            for (int index = 0; index <= 5; ++index) {
                const double ratio = static_cast<double>(index) / 5.0;
                const double value =
                    values.maximum -
                    ratio * (values.maximum - values.minimum);
                const float y =
                    plotOrigin.y + plotHeight * static_cast<float>(ratio);
                std::snprintf(label, sizeof(label), "%.2f", value);
                draw->AddText(
                    ImVec2(plotOrigin.x + plotWidth + 5.0f, y - 7.0f),
                    IM_COL32(180, 184, 194, 255),
                    label);
            }
        }

        void DrawTimeAxis(
            ImDrawList* draw,
            const TimeRange& timeRange,
            const ImVec2& plotOrigin,
            float plotWidth,
            float plotHeight)
        {
            for (int index = 0; index <= 4; ++index) {
                const double ratio = static_cast<double>(index) / 4.0;
                const EpochMillis timestamp =
                    timeRange.minimum +
                    static_cast<EpochMillis>(std::llround(
                        static_cast<double>(
                            timeRange.maximum - timeRange.minimum) * ratio));
                const float x =
                    plotOrigin.x + plotWidth * static_cast<float>(ratio);
                const std::string label = FormatTimestamp(timestamp);
                const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
                draw->AddText(
                    ImVec2(
                        x - textSize.x * static_cast<float>(ratio),
                        plotOrigin.y + plotHeight + 3.0f),
                    IM_COL32(180, 184, 194, 255),
                    label.c_str());
            }
        }

        void ProcessInteraction(
            const ImVec2& plotOrigin,
            float plotWidth,
            float plotHeight,
            const TimeRange& dataRange,
            EpochMillis minimumSpanMs,
            ValueRange values,
            RenderSurfaceState& state)
        {
            if (!ImGui::IsItemHovered()) return;

            ImGuiIO& io = ImGui::GetIO();
            const double mouseRatio = (std::max)(
                0.0,
                (std::min)(1.0,
                    static_cast<double>(io.MousePos.x - plotOrigin.x) /
                    static_cast<double>((std::max)(1.0f, plotWidth))));

            if (io.MouseWheel != 0.0f) {
                render::ZoomViewport(
                    state.viewport,
                    dataRange.minimum,
                    dataRange.maximum,
                    mouseRatio,
                    static_cast<double>(io.MouseWheel),
                    minimumSpanMs);
                state.dirty = true;
            }

            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f)) {
                render::PanViewport(
                    state.viewport,
                    dataRange.minimum,
                    dataRange.maximum,
                    -static_cast<double>(io.MouseDelta.x) /
                        static_cast<double>((std::max)(1.0f, plotWidth)));
                state.dirty = true;
            }

            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                render::ResetViewport(
                    state.viewport,
                    dataRange.minimum,
                    dataRange.maximum);
                state.dirty = true;
            }

            state.crosshairVisible = true;
            state.crosshairTimestampMs =
                state.viewport.visibleStartMs +
                static_cast<EpochMillis>(std::llround(
                    static_cast<double>(state.viewport.SpanMs()) * mouseRatio));
            state.crosshairValue = UnmapY(
                io.MousePos.y,
                values,
                plotOrigin.y,
                plotHeight);
        }

        void DrawPane(
            const render::Pane& pane,
            const TimeRange& dataRange,
            const TimeRange& visibleRange,
            EpochMillis minimumSpanMs,
            bool drawTimeAxis,
            ImVec2 size,
            RenderSurfaceState& state)
        {
            if (size.x < 130.0f || size.y < 50.0f) return;

            const float timeAxisHeight =
                drawTimeAxis ? TimeAxisHeight : 0.0f;
            const float plotWidth = (std::max)(
                40.0f,
                size.x - ValueAxisWidth);
            const float plotHeight = (std::max)(
                30.0f,
                size.y - timeAxisHeight);
            const ValueRange values =
                PaneValueRange(pane, visibleRange);

            ImGui::PushID(pane.id.c_str());
            ImGui::InvisibleButton("##surface", size);
            const ImVec2 surfaceOrigin = ImGui::GetItemRectMin();
            const ImVec2 plotOrigin = surfaceOrigin;
            const ImVec2 plotEnd(
                plotOrigin.x + plotWidth,
                plotOrigin.y + plotHeight);
            ImDrawList* draw = ImGui::GetWindowDrawList();

            draw->AddRectFilled(
                surfaceOrigin,
                ImGui::GetItemRectMax(),
                IM_COL32(15, 16, 20, 255));
            draw->AddRect(
                plotOrigin,
                plotEnd,
                IM_COL32(70, 72, 82, 255));

            if (!values.valid || !visibleRange.valid) {
                ImGui::PopID();
                return;
            }

            ProcessInteraction(
                plotOrigin,
                plotWidth,
                plotHeight,
                dataRange,
                minimumSpanMs,
                values,
                state);

            for (int grid = 1; grid < 5; ++grid) {
                const float y =
                    plotOrigin.y +
                    plotHeight * static_cast<float>(grid) / 5.0f;
                draw->AddLine(
                    ImVec2(plotOrigin.x, y),
                    ImVec2(plotEnd.x, y),
                    IM_COL32(45, 47, 55, 255));
            }

            std::size_t visibleCandleCount = 0;
            for (const render::CandleSeries& series : pane.candles) {
                if (!series.visible) continue;
                for (const Bar& bar : series.bars) {
                    if (InTimeRange(bar.closeTimestampMs, visibleRange)) {
                        ++visibleCandleCount;
                    }
                }
            }
            const float candleBodyWidth = (std::min)(
                18.0f,
                (std::max)(
                    1.0f,
                    plotWidth /
                        static_cast<float>((std::max)(
                            static_cast<std::size_t>(1),
                            visibleCandleCount)) *
                        0.58f));

            for (const render::HistogramSeries& series : pane.histograms) {
                if (!series.visible) continue;
                for (const render::HistogramPoint& point : series.points) {
                    if (!InTimeRange(point.timestampMs, visibleRange)) continue;
                    const float x = MapX(
                        point.timestampMs,
                        visibleRange,
                        plotOrigin.x,
                        plotWidth);
                    const float y = MapY(
                        point.value,
                        values,
                        plotOrigin.y,
                        plotHeight);
                    const float zeroY = MapY(
                        0.0,
                        values,
                        plotOrigin.y,
                        plotHeight);
                    draw->AddRectFilled(
                        ImVec2(
                            x - candleBodyWidth * 0.5f,
                            (std::min)(y, zeroY)),
                        ImVec2(
                            x + candleBodyWidth * 0.5f,
                            (std::max)(y, zeroY)),
                        ToImColor(
                            point.positive
                                ? series.positiveColor
                                : series.negativeColor));
                }
            }

            for (const render::CandleSeries& series : pane.candles) {
                if (!series.visible) continue;
                for (const Bar& bar : series.bars) {
                    if (!InTimeRange(bar.closeTimestampMs, visibleRange)) continue;
                    const float x = MapX(
                        bar.closeTimestampMs,
                        visibleRange,
                        plotOrigin.x,
                        plotWidth);
                    const bool up = bar.close >= bar.open;
                    const ImU32 color = ToImColor(
                        up ? series.upColor : series.downColor);
                    draw->AddLine(
                        ImVec2(
                            x,
                            MapY(bar.high, values, plotOrigin.y, plotHeight)),
                        ImVec2(
                            x,
                            MapY(bar.low, values, plotOrigin.y, plotHeight)),
                        color,
                        1.0f);

                    float openY = MapY(
                        bar.open,
                        values,
                        plotOrigin.y,
                        plotHeight);
                    float closeY = MapY(
                        bar.close,
                        values,
                        plotOrigin.y,
                        plotHeight);
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
                    if (!InTimeRange(point.timestampMs, visibleRange)) continue;
                    const ImVec2 current(
                        MapX(
                            point.timestampMs,
                            visibleRange,
                            plotOrigin.x,
                            plotWidth),
                        MapY(
                            point.value,
                            values,
                            plotOrigin.y,
                            plotHeight));
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
                    plotOrigin.y,
                    plotHeight);
                draw->AddLine(
                    ImVec2(plotOrigin.x, y),
                    ImVec2(plotEnd.x, y),
                    ToImColor(line.color),
                    line.width);
            }

            for (const render::MarkerSeries& series : pane.markers) {
                if (!series.visible) continue;
                for (const render::MarkerPoint& marker : series.points) {
                    if (!InTimeRange(marker.timestampMs, visibleRange)) continue;
                    const ImVec2 point(
                        MapX(
                            marker.timestampMs,
                            visibleRange,
                            plotOrigin.x,
                            plotWidth),
                        MapY(
                            marker.value,
                            values,
                            plotOrigin.y,
                            plotHeight));
                    draw->AddCircleFilled(
                        point,
                        4.0f,
                        ToImColor(marker.color));
                }
            }

            for (const render::TextAnnotation& annotation : pane.annotations) {
                if (!annotation.visible ||
                    !InTimeRange(annotation.timestampMs, visibleRange))
                {
                    continue;
                }
                draw->AddText(
                    ImVec2(
                        MapX(
                            annotation.timestampMs,
                            visibleRange,
                            plotOrigin.x,
                            plotWidth),
                        MapY(
                            annotation.value,
                            values,
                            plotOrigin.y,
                            plotHeight)),
                    ToImColor(annotation.color),
                    annotation.text.c_str());
            }

            PriceWon latestPrice = 0;
            EpochMillis latestTimestamp = 0;
            if (LatestCandleClose(pane, latestPrice, latestTimestamp)) {
                const float currentY = MapY(
                    static_cast<double>(latestPrice),
                    values,
                    plotOrigin.y,
                    plotHeight);
                draw->AddLine(
                    ImVec2(plotOrigin.x, currentY),
                    ImVec2(plotEnd.x, currentY),
                    IM_COL32(245, 196, 70, 210),
                    1.0f);
                char label[48]{};
                std::snprintf(label, sizeof(label), "%d", latestPrice);
                draw->AddRectFilled(
                    ImVec2(plotEnd.x + 2.0f, currentY - 9.0f),
                    ImVec2(plotEnd.x + ValueAxisWidth - 2.0f, currentY + 9.0f),
                    IM_COL32(110, 82, 18, 255));
                draw->AddText(
                    ImVec2(plotEnd.x + 5.0f, currentY - 7.0f),
                    IM_COL32(255, 244, 190, 255),
                    label);
            }

            DrawValueAxis(
                draw,
                values,
                plotOrigin,
                plotWidth,
                plotHeight);
            if (drawTimeAxis) {
                DrawTimeAxis(
                    draw,
                    visibleRange,
                    plotOrigin,
                    plotWidth,
                    plotHeight);
            }

            if (state.crosshairVisible &&
                InTimeRange(state.crosshairTimestampMs, visibleRange))
            {
                const float crossX = MapX(
                    state.crosshairTimestampMs,
                    visibleRange,
                    plotOrigin.x,
                    plotWidth);
                draw->AddLine(
                    ImVec2(crossX, plotOrigin.y),
                    ImVec2(crossX, plotEnd.y),
                    IM_COL32(205, 208, 220, 180),
                    1.0f);
            }

            if (ImGui::IsItemHovered()) {
                const Bar* nearest = NearestVisibleBar(
                    pane,
                    state.crosshairTimestampMs,
                    visibleRange);
                if (nearest != nullptr) {
                    const float crossY = MapY(
                        static_cast<double>(nearest->close),
                        values,
                        plotOrigin.y,
                        plotHeight);
                    draw->AddLine(
                        ImVec2(plotOrigin.x, crossY),
                        ImVec2(plotEnd.x, crossY),
                        IM_COL32(205, 208, 220, 130),
                        1.0f);
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(
                        FormatTimestamp(nearest->closeTimestampMs).c_str());
                    ImGui::Separator();
                    ImGui::Text(
                        "O %d  H %d  L %d  C %d",
                        nearest->open,
                        nearest->high,
                        nearest->low,
                        nearest->close);
                    ImGui::Text(
                        "V %lld  T %d",
                        static_cast<long long>(nearest->volume),
                        nearest->tickCount);
                    ImGui::EndTooltip();
                }
            }

            ImGui::PopID();
        }
    }

    void DrawRenderDocument(
        const render::RenderDocument& document,
        ImVec2 size,
        RenderSurfaceState& surfaceState)
    {
        const TimeRange dataRange = DocumentDataRange(document);
        if (!dataRange.valid || document.panes.empty()) return;

        const EpochMillis minimumSpanMs =
            MinimumViewportSpan(document);
        if (!surfaceState.viewport.initialized) {
            render::ResetViewport(
                surfaceState.viewport,
                dataRange.minimum,
                dataRange.maximum);
        }
        else if (surfaceState.renderedRevision != document.revision) {
            render::FollowLatest(
                surfaceState.viewport,
                dataRange.minimum,
                dataRange.maximum);
        }
        render::ClampViewport(
            surfaceState.viewport,
            dataRange.minimum,
            dataRange.maximum,
            minimumSpanMs);

        TimeRange visibleRange;
        visibleRange.minimum = surfaceState.viewport.visibleStartMs;
        visibleRange.maximum = surfaceState.viewport.visibleEndMs;
        visibleRange.valid =
            visibleRange.maximum > visibleRange.minimum;
        if (!visibleRange.valid) return;

        surfaceState.crosshairVisible = false;

        float totalWeight = 0.0f;
        for (const render::Pane& pane : document.panes) {
            totalWeight += (std::max)(0.01f, pane.heightWeight);
        }

        const float spacing =
            ImGui::GetStyle().ItemSpacing.y *
            static_cast<float>((std::max)(
                static_cast<std::size_t>(0),
                document.panes.size() - 1));
        const float availableHeight =
            (std::max)(0.0f, size.y - spacing);

        for (std::size_t index = 0; index < document.panes.size(); ++index) {
            const render::Pane& pane = document.panes[index];
            const float paneHeight =
                availableHeight *
                (std::max)(0.01f, pane.heightWeight) /
                totalWeight;
            DrawPane(
                pane,
                dataRange,
                visibleRange,
                minimumSpanMs,
                index + 1 == document.panes.size(),
                ImVec2(size.x, paneHeight),
                surfaceState);
            if (index + 1 < document.panes.size()) {
                ImGui::Dummy(ImVec2(0.0f, 0.0f));
            }
        }

        surfaceState.renderedRevision = document.revision;
        surfaceState.dirty = false;
    }
}
