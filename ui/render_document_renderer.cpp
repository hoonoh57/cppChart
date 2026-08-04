#include "render_document_renderer.h"

#include "../render/cursor_label_layout.h"
#include "../render/pane_layout.h"
#include "../render/series_geometry.h"
#include "../render/value_grid.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <string>
#include <vector>

namespace trading::ui
{
    namespace
    {
        constexpr float ValueAxisWidth = 74.0f;
        constexpr float TimeAxisHeight = 20.0f;
        constexpr float LegendItemHeight = 20.0f;
        constexpr float LegendSpacing = 4.0f;
        constexpr float PaneSplitterHeight = 7.0f;
        constexpr float MinimumPaneHeight = 48.0f;
        constexpr double MinimumVisibleSpan = 12.0;

        ImU32 ToImColor(const render::ColorRgba& color) noexcept
        {
            return IM_COL32(
                color.red,
                color.green,
                color.blue,
                color.alpha);
        }

        void DrawStyledLine(
            ImDrawList* draw,
            const ImVec2& start,
            const ImVec2& end,
            ImU32 color,
            float width,
            render::LineStyle style)
        {
            const float dx = end.x - start.x;
            const float dy = end.y - start.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (style == render::LineStyle::Solid || length < 1.0f) {
                draw->AddLine(start, end, color, width);
                return;
            }
            const float unitX = dx / length;
            const float unitY = dy / length;
            if (style == render::LineStyle::Dotted) {
                const float spacing = (std::max)(4.0f, width * 3.0f);
                const float radius = (std::max)(1.0f, width * 0.6f);
                for (float distance = 0.0f; distance <= length; distance += spacing) {
                    draw->AddCircleFilled(
                        ImVec2(
                            start.x + unitX * distance,
                            start.y + unitY * distance),
                        radius,
                        color);
                }
                return;
            }
            const float dash = (std::max)(6.0f, width * 4.0f);
            const float gap = (std::max)(4.0f, width * 2.5f);
            for (float distance = 0.0f; distance < length; distance += dash + gap) {
                const float finish = (std::min)(length, distance + dash);
                draw->AddLine(
                    ImVec2(
                        start.x + unitX * distance,
                        start.y + unitY * distance),
                    ImVec2(
                        start.x + unitX * finish,
                        start.y + unitY * finish),
                    color,
                    width);
            }
        }

        struct AxisRange final
        {
            render::AxisCoordinate minimum = 0.0;
            render::AxisCoordinate maximum = 0.0;
            bool valid = false;

            render::AxisCoordinate Span() const noexcept
            {
                return maximum > minimum ? maximum - minimum : 0.0;
            }
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

        struct PaneGeometry final
        {
            ImVec2 plotOrigin;
            ImVec2 plotEnd;
            bool valid = false;
        };

        std::vector<EpochMillis> PrimaryCandleTimestamps(
            const render::RenderDocument& document)
        {
            for (const render::Pane& pane : document.panes) {
                for (const render::CandleSeries& series : pane.candles) {
                    if (!series.visible || series.bars.empty()) continue;
                    std::vector<EpochMillis> result;
                    result.reserve(series.bars.size());
                    for (const Bar& bar : series.bars) {
                        result.push_back(bar.closeTimestampMs);
                    }
                    return result;
                }
            }
            return {};
        }

        std::vector<EpochMillis> CollectDocumentTimestamps(
            const render::RenderDocument& document)
        {
            std::vector<EpochMillis> result;
            for (const render::Pane& pane : document.panes) {
                for (const render::LineSeries& series : pane.lines) {
                    if (!series.visible) continue;
                    for (const render::LinePoint& point : series.points) {
                        if (point.timestampMs > 0) result.push_back(point.timestampMs);
                    }
                }
                for (const render::HistogramSeries& series : pane.histograms) {
                    if (!series.visible) continue;
                    for (const render::HistogramPoint& point : series.points) {
                        if (point.timestampMs > 0) result.push_back(point.timestampMs);
                    }
                }
                for (const render::MarkerSeries& series : pane.markers) {
                    if (!series.visible) continue;
                    for (const render::MarkerPoint& point : series.points) {
                        if (point.timestampMs > 0) result.push_back(point.timestampMs);
                    }
                }
                for (const render::TextAnnotation& annotation : pane.annotations) {
                    if (annotation.visible && annotation.timestampMs > 0) {
                        result.push_back(annotation.timestampMs);
                    }
                }
            }
            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
            return result;
        }

        bool InAxisRange(
            EpochMillis timestamp,
            const render::OrdinalTimeAxis& axis,
            const AxisRange& range) noexcept
        {
            if (!range.valid || timestamp <= 0 || axis.Empty()) return false;
            const render::AxisCoordinate coordinate =
                axis.CoordinateForTimestamp(timestamp);
            return
                coordinate >= range.minimum - 0.0001 &&
                coordinate <= range.maximum + 0.0001;
        }

        ValueRange PaneValueRange(
            const render::Pane& pane,
            const render::OrdinalTimeAxis& axis,
            const AxisRange& range)
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
                    if (!InAxisRange(bar.closeTimestampMs, axis, range)) continue;
                    result.Include(static_cast<double>(bar.low));
                    result.Include(static_cast<double>(bar.high));
                }
            }
            for (const render::LineSeries& series : pane.lines) {
                if (!series.visible) continue;
                for (const render::LinePoint& point : series.points) {
                    if (InAxisRange(point.timestampMs, axis, range)) {
                        result.Include(point.value);
                    }
                }
            }
            for (const render::HistogramSeries& series : pane.histograms) {
                if (!series.visible) continue;
                result.Include(0.0);
                for (const render::HistogramPoint& point : series.points) {
                    if (InAxisRange(point.timestampMs, axis, range)) {
                        result.Include(point.value);
                    }
                }
            }
            for (const render::MarkerSeries& series : pane.markers) {
                if (!series.visible) continue;
                for (const render::MarkerPoint& point : series.points) {
                    if (InAxisRange(point.timestampMs, axis, range)) {
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
            const render::OrdinalTimeAxis& axis,
            const AxisRange& range,
            float left,
            float width) noexcept
        {
            const double span = (std::max)(0.0001, range.Span());
            const double coordinate = axis.CoordinateForTimestamp(timestamp);
            const double ratio = (coordinate - range.minimum) / span;
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
            return range.maximum - ratio * (range.maximum - range.minimum);
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
            const render::OrdinalTimeAxis& axis,
            const AxisRange& range)
        {
            const Bar* nearest = nullptr;
            double nearestDistance = (std::numeric_limits<double>::max)();
            const double target = axis.CoordinateForTimestamp(timestamp);
            for (const render::CandleSeries& series : pane.candles) {
                if (!series.visible) continue;
                for (const Bar& bar : series.bars) {
                    if (!InAxisRange(bar.closeTimestampMs, axis, range)) continue;
                    const double distance = std::fabs(
                        axis.CoordinateForTimestamp(bar.closeTimestampMs) - target);
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

        double DefaultVisibleSpan(float width, std::size_t pointCount) noexcept
        {
            if (pointCount <= 1) return 1.0;
            const double desiredBars = (std::max)(
                40.0,
                (std::min)(
                    180.0,
                    static_cast<double>((std::max)(130.0f, width) - ValueAxisWidth) /
                        7.0));
            return (std::min)(
                static_cast<double>(pointCount - 1U),
                (std::max)(MinimumVisibleSpan, desiredBars - 1.0));
        }

        void FormatPaneValue(
            char* buffer,
            std::size_t bufferSize,
            double value,
            int decimals)
        {
            decimals = (std::max)(0, (std::min)(8, decimals));
            std::snprintf(buffer, bufferSize, "%.*f", decimals, value);
        }

        void DrawCursorValueLabel(
            ImDrawList* draw,
            const ImVec2& plotOrigin,
            const ImVec2& plotEnd,
            float y,
            double value,
            int decimals)
        {
            char label[64]{};
            FormatPaneValue(label, sizeof(label), value, decimals);
            draw->AddLine(
                ImVec2(plotOrigin.x, y),
                ImVec2(plotEnd.x, y),
                IM_COL32(205, 208, 220, 150),
                1.0f);
            draw->AddRectFilled(
                ImVec2(plotEnd.x + 2.0f, y - 9.0f),
                ImVec2(plotEnd.x + ValueAxisWidth - 2.0f, y + 9.0f),
                IM_COL32(65, 68, 80, 245));
            draw->AddText(
                ImVec2(plotEnd.x + 5.0f, y - 7.0f),
                IM_COL32(235, 237, 244, 255),
                label);
        }

        void DrawCursorTimeLabel(
            ImDrawList* draw,
            const ImVec2& plotOrigin,
            const ImVec2& plotEnd,
            float crossX,
            EpochMillis timestampMs,
            bool useTimeAxisBand)
        {
            const std::string label = FormatTimestamp(timestampMs);
            const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            constexpr float horizontalPadding = 6.0f;
            constexpr float labelHeight = 18.0f;
            const float requestedWidth = textSize.x + horizontalPadding * 2.0f;
            const render::HorizontalLabelPlacement placement =
                render::PlaceCenteredHorizontalLabel(
                    crossX,
                    requestedWidth,
                    plotOrigin.x,
                    plotEnd.x);
            const float top = useTimeAxisBand
                ? plotEnd.y + 1.0f
                : plotEnd.y - labelHeight - 1.0f;
            const float bottom = top + labelHeight;

            draw->AddRectFilled(
                ImVec2(placement.left, top),
                ImVec2(placement.right, bottom),
                IM_COL32(65, 68, 80, 245));
            draw->AddRect(
                ImVec2(placement.left, top),
                ImVec2(placement.right, bottom),
                IM_COL32(205, 208, 220, 180));
            draw->AddText(
                ImVec2(
                    placement.left +
                        (placement.right - placement.left - textSize.x) * 0.5f,
                    top + 2.0f),
                IM_COL32(235, 237, 244, 255),
                label.c_str());
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
                    values.maximum - ratio * (values.maximum - values.minimum);
                const float y =
                    plotOrigin.y + plotHeight * static_cast<float>(ratio);
                if (std::fabs(value) >= 1000.0) {
                    std::snprintf(label, sizeof(label), "%.0f", value);
                }
                else {
                    std::snprintf(label, sizeof(label), "%.2f", value);
                }
                draw->AddText(
                    ImVec2(plotOrigin.x + plotWidth + 5.0f, y - 7.0f),
                    IM_COL32(180, 184, 194, 255),
                    label);
            }
        }

        void DrawTimeAxis(
            ImDrawList* draw,
            const render::OrdinalTimeAxis& axis,
            const AxisRange& range,
            const ImVec2& plotOrigin,
            float plotWidth,
            float plotHeight)
        {
            for (int index = 0; index <= 4; ++index) {
                const double ratio = static_cast<double>(index) / 4.0;
                const render::AxisCoordinate coordinate =
                    range.minimum + range.Span() * ratio;
                const EpochMillis timestamp =
                    axis.TimestampForCoordinate(coordinate);
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


        bool PointInRect(
            const ImVec2& point,
            const ImVec2& minimum,
            const ImVec2& maximum) noexcept
        {
            return
                point.x >= minimum.x &&
                point.x <= maximum.x &&
                point.y >= minimum.y &&
                point.y <= maximum.y;
        }

        bool OwnerSelected(
            const std::string& ownerId,
            const RenderSurfaceState& state) noexcept
        {
            return
                !ownerId.empty() &&
                ownerId == state.selectedOwnerId;
        }

        bool DrawPaneLegends(
            ImDrawList* draw,
            const render::Pane& pane,
            const ImVec2& plotOrigin,
            const ImVec2& plotEnd,
            RenderSurfaceState& state)
        {
            if (pane.legends.empty()) return false;

            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float left = plotOrigin.x + 6.0f;
            const float right = plotEnd.x - 6.0f;
            float x = left;
            float y = plotOrigin.y + 5.0f;
            bool hoveredAny = false;

            for (const render::LegendEntry& legend : pane.legends) {
                if (!legend.visible || legend.label.empty()) continue;

                const ImVec2 textSize =
                    ImGui::CalcTextSize(legend.label.c_str());
                const float width = 28.0f + textSize.x;
                if (x > left && x + width > right) {
                    x = left;
                    y += LegendItemHeight + LegendSpacing;
                }
                if (y + LegendItemHeight > plotEnd.y - 3.0f) break;

                const ImVec2 minimum(x, y);
                const ImVec2 maximum(
                    (std::min)(right, x + width),
                    y + LegendItemHeight);
                const bool hovered =
                    PointInRect(mouse, minimum, maximum);
                hoveredAny = hoveredAny || hovered;
                const bool selected =
                    OwnerSelected(legend.ownerId, state);

                if (selected || hovered) {
                    draw->AddRectFilled(
                        minimum,
                        maximum,
                        selected
                            ? IM_COL32(52, 74, 112, 225)
                            : IM_COL32(52, 54, 64, 205),
                        2.0f);
                }
                if (selected) {
                    draw->AddRect(
                        minimum,
                        maximum,
                        IM_COL32(155, 190, 245, 235),
                        2.0f,
                        0,
                        1.0f);
                }

                const float centerY =
                    y + LegendItemHeight * 0.5f;
                DrawStyledLine(
                    draw,
                    ImVec2(x + 5.0f, centerY),
                    ImVec2(x + 17.0f, centerY),
                    ToImColor(legend.color),
                    legend.width + (selected ? 1.0f : 0.0f),
                    legend.style);
                draw->AddText(
                    ImVec2(x + 22.0f, y + 2.0f),
                    selected
                        ? IM_COL32(248, 250, 255, 255)
                        : IM_COL32(220, 223, 232, 245),
                    legend.label.c_str());

                if (
                    hovered &&
                    legend.selectable &&
                    !legend.ownerId.empty())
                {
                    const bool doubleClicked =
                        ImGui::IsMouseDoubleClicked(
                            ImGuiMouseButton_Left);
                    const bool clicked =
                        ImGui::IsMouseClicked(
                            ImGuiMouseButton_Left);
                    if (doubleClicked || clicked) {
                        state.selectedOwnerId = legend.ownerId;
                        state.selectedPaneId = pane.id;
                        state.selectedLegendId = legend.id;
                        state.selectedLegendLabel = legend.label;
                        state.selectionChanged = true;
                        state.selectionDoubleClicked =
                            doubleClicked;
                    }
                }

                x += width + LegendSpacing;
            }

            return hoveredAny;
        }

        void ProcessInteraction(
            const ImVec2& plotOrigin,
            float plotWidth,
            float plotHeight,
            const render::OrdinalTimeAxis& axis,
            const AxisRange& dataRange,
            double defaultVisibleSpan,
            const render::Pane& pane,
            const ValueRange& values,
            bool legendHovered,
            RenderSurfaceState& state)
        {
            if (legendHovered) return;
            if (!ImGui::IsItemHovered() && !ImGui::IsItemActive()) return;

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
                    MinimumVisibleSpan);
                state.dirty = true;
            }

            const bool draggingLeft =
                ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f);
            const bool draggingRight =
                ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f);
            if (draggingLeft || draggingRight) {
                render::PanViewport(
                    state.viewport,
                    dataRange.minimum,
                    dataRange.maximum,
                    -static_cast<double>(io.MouseDelta.x) /
                        static_cast<double>((std::max)(1.0f, plotWidth)));
                state.dirty = true;
            }

            if (!draggingLeft &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                render::ResetViewport(
                    state.viewport,
                    dataRange.minimum,
                    dataRange.maximum,
                    defaultVisibleSpan);
                state.dirty = true;
            }

            const render::AxisCoordinate crosshairCoordinate =
                state.viewport.visibleStart +
                state.viewport.Span() * mouseRatio;
            state.crosshairVisible = true;
            state.crosshairTimestampMs =
                axis.TimestampForCoordinate(crosshairCoordinate);
            const double rawCrosshairValue = UnmapY(
                io.MousePos.y,
                values,
                plotOrigin.y,
                plotHeight);
            state.crosshairValue = render::QuantizeValue(
                pane.cursorGrid,
                rawCrosshairValue);
            state.crosshairValue = (std::max)(
                values.minimum,
                (std::min)(values.maximum, state.crosshairValue));
        }

        void DrawPaneSplitter(
            const render::Pane& upperPane,
            const render::Pane& lowerPane,
            float width,
            float availableHeight,
            float totalWeight,
            RenderSurfaceState& state)
        {
            ImGui::PushID(("splitter." + upperPane.id + "." + lowerPane.id).c_str());
            ImGui::InvisibleButton(
                "##pane_splitter",
                ImVec2(width, PaneSplitterHeight),
                ImGuiButtonFlags_MouseButtonLeft);
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            if (hovered || active) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            }
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            const float centerY = (minimum.y + maximum.y) * 0.5f;
            draw->AddRectFilled(
                minimum,
                maximum,
                active
                    ? IM_COL32(76, 112, 166, 210)
                    : hovered
                        ? IM_COL32(64, 82, 112, 190)
                        : IM_COL32(28, 30, 36, 255));
            draw->AddLine(
                ImVec2(minimum.x, centerY),
                ImVec2(maximum.x, centerY),
                hovered || active
                    ? IM_COL32(150, 184, 235, 230)
                    : IM_COL32(75, 78, 90, 180),
                hovered || active ? 2.0f : 1.0f);
            if (active && ImGui::GetIO().MouseDelta.y != 0.0f) {
                float& upperWeight = state.paneHeightWeights[upperPane.id];
                float& lowerWeight = state.paneHeightWeights[lowerPane.id];
                if (render::AdjustAdjacentPaneWeights(
                        availableHeight,
                        MinimumPaneHeight,
                        totalWeight,
                        ImGui::GetIO().MouseDelta.y,
                        upperWeight,
                        lowerWeight))
                {
                    state.dirty = true;
                }
            }
            ImGui::PopID();
        }

        void DrawPane(
            const render::Pane& pane,
            const render::OrdinalTimeAxis& axis,
            const AxisRange& dataRange,
            const AxisRange& visibleRange,
            double defaultVisibleSpan,
            bool drawTimeAxis,
            ImVec2 size,
            RenderSurfaceState& state,
            std::vector<PaneGeometry>& paneGeometries)
        {
            if (size.x < 130.0f || size.y < 50.0f) return;

            const float timeAxisHeight = drawTimeAxis ? TimeAxisHeight : 0.0f;
            const float plotWidth = (std::max)(40.0f, size.x - ValueAxisWidth);
            const float plotHeight = (std::max)(30.0f, size.y - timeAxisHeight);
            const ValueRange values = PaneValueRange(pane, axis, visibleRange);

            ImGui::PushID(pane.id.c_str());
            ImGui::InvisibleButton(
                "##surface",
                size,
                ImGuiButtonFlags_MouseButtonLeft |
                    ImGuiButtonFlags_MouseButtonRight);
            const ImVec2 surfaceOrigin = ImGui::GetItemRectMin();
            const ImVec2 plotOrigin = surfaceOrigin;
            const ImVec2 plotEnd(
                plotOrigin.x + plotWidth,
                plotOrigin.y + plotHeight);
            PaneGeometry geometry;
            geometry.plotOrigin = plotOrigin;
            geometry.plotEnd = plotEnd;
            geometry.valid = true;
            paneGeometries.push_back(geometry);
            ImDrawList* draw = ImGui::GetWindowDrawList();

            draw->AddRectFilled(
                surfaceOrigin,
                ImGui::GetItemRectMax(),
                IM_COL32(15, 16, 20, 255));
            draw->AddRect(
                plotOrigin,
                plotEnd,
                IM_COL32(70, 72, 82, 255));

            const bool legendHovered = DrawPaneLegends(
                draw,
                pane,
                plotOrigin,
                plotEnd,
                state);

            if (!values.valid || !visibleRange.valid) {
                ImGui::PopID();
                return;
            }

            ProcessInteraction(
                plotOrigin,
                plotWidth,
                plotHeight,
                axis,
                dataRange,
                defaultVisibleSpan,
                pane,
                values,
                legendHovered,
                state);

            const bool paneHovered =
                !legendHovered &&
                (ImGui::IsItemHovered() || ImGui::IsItemActive());

            for (int grid = 1; grid < 5; ++grid) {
                const float y =
                    plotOrigin.y + plotHeight * static_cast<float>(grid) / 5.0f;
                draw->AddLine(
                    ImVec2(plotOrigin.x, y),
                    ImVec2(plotEnd.x, y),
                    IM_COL32(45, 47, 55, 255));
            }

            for (const render::TimeBoundary& boundary : state.timeBoundaries) {
                if (!InAxisRange(boundary.timestampMs, axis, visibleRange)) continue;
                const float x = MapX(
                    boundary.timestampMs,
                    axis,
                    visibleRange,
                    plotOrigin.x,
                    plotWidth);
                const bool calendarDate =
                    boundary.kind == render::TimeBoundaryKind::CalendarDate;
                draw->AddLine(
                    ImVec2(x, plotOrigin.y),
                    ImVec2(x, plotEnd.y),
                    calendarDate
                        ? IM_COL32(145, 150, 172, 190)
                        : IM_COL32(105, 110, 126, 135),
                    calendarDate ? 1.5f : 1.0f);
                if (drawTimeAxis) {
                    const std::string label = calendarDate
                        ? FormatTimestamp(boundary.timestampMs).substr(0, 5)
                        : std::string("gap");
                    draw->AddText(
                        ImVec2(x + 3.0f, plotOrigin.y + 3.0f),
                        calendarDate
                            ? IM_COL32(205, 208, 224, 230)
                            : IM_COL32(145, 148, 162, 200),
                        label.c_str());
                }
            }

            const float seriesBodyWidth = render::SeriesBodyWidth(
                plotWidth,
                visibleRange.Span());

            for (const render::HistogramSeries& series : pane.histograms) {
                if (!series.visible) continue;
                const bool selected =
                    OwnerSelected(series.ownerId, state);
                for (const render::HistogramPoint& point : series.points) {
                    if (!InAxisRange(point.timestampMs, axis, visibleRange)) continue;
                    const float x = MapX(
                        point.timestampMs,
                        axis,
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
                    const ImVec2 barMinimum(
                        x - seriesBodyWidth * 0.5f,
                        (std::min)(y, zeroY));
                    const ImVec2 barMaximum(
                        x + seriesBodyWidth * 0.5f,
                        (std::max)(y, zeroY));
                    draw->AddRectFilled(
                        barMinimum,
                        barMaximum,
                        ToImColor(
                            point.positive
                                ? series.positiveColor
                                : series.negativeColor));
                    if (selected) {
                        draw->AddRect(
                            barMinimum,
                            barMaximum,
                            IM_COL32(245, 247, 252, 210),
                            0.0f,
                            0,
                            1.0f);
                    }
                }
            }

            for (const render::CandleSeries& series : pane.candles) {
                if (!series.visible) continue;
                for (const Bar& bar : series.bars) {
                    if (!InAxisRange(bar.closeTimestampMs, axis, visibleRange)) continue;
                    const float x = MapX(
                        bar.closeTimestampMs,
                        axis,
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
                            x - seriesBodyWidth * 0.5f,
                            (std::min)(openY, closeY)),
                        ImVec2(
                            x + seriesBodyWidth * 0.5f,
                            (std::max)(openY, closeY)),
                        color);
                }
            }

            for (const render::LineSeries& series : pane.lines) {
                if (!series.visible) continue;
                const bool selected =
                    OwnerSelected(series.ownerId, state);
                bool hasPrevious = false;
                ImVec2 previous;
                for (const render::LinePoint& point : series.points) {
                    if (!InAxisRange(point.timestampMs, axis, visibleRange)) continue;
                    const ImVec2 current(
                        MapX(
                            point.timestampMs,
                            axis,
                            visibleRange,
                            plotOrigin.x,
                            plotWidth),
                        MapY(
                            point.value,
                            values,
                            plotOrigin.y,
                            plotHeight));
                    if (hasPrevious) {
                        DrawStyledLine(
                            draw,
                            previous,
                            current,
                            ToImColor(series.color),
                            series.width + (selected ? 1.5f : 0.0f),
                            series.style);
                    }
                    previous = current;
                    hasPrevious = true;
                }
            }

            for (const render::ReferenceLine& line : pane.referenceLines) {
                if (!line.visible) continue;
                const bool selected =
                    OwnerSelected(line.ownerId, state);
                const float y = MapY(
                    line.value,
                    values,
                    plotOrigin.y,
                    plotHeight);
                DrawStyledLine(
                    draw,
                    ImVec2(plotOrigin.x, y),
                    ImVec2(plotEnd.x, y),
                    ToImColor(line.color),
                    line.width + (selected ? 1.0f : 0.0f),
                    line.style);
                if (!line.label.empty()) {
                    draw->AddText(
                        ImVec2(plotOrigin.x + 4.0f, y - 15.0f),
                        ToImColor(line.color),
                        line.label.c_str());
                }
            }

            for (const render::MarkerSeries& series : pane.markers) {
                if (!series.visible) continue;
                const bool selected =
                    OwnerSelected(series.ownerId, state);
                for (const render::MarkerPoint& marker : series.points) {
                    if (!InAxisRange(marker.timestampMs, axis, visibleRange)) continue;
                    const ImVec2 point(
                        MapX(
                            marker.timestampMs,
                            axis,
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
                        selected ? 6.0f : 4.0f,
                        ToImColor(marker.color));
                }
            }

            for (const render::TextAnnotation& annotation : pane.annotations) {
                if (!annotation.visible ||
                    !InAxisRange(annotation.timestampMs, axis, visibleRange))
                {
                    continue;
                }
                draw->AddText(
                    ImVec2(
                        MapX(
                            annotation.timestampMs,
                            axis,
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
            if (
                LatestCandleClose(pane, latestPrice, latestTimestamp) &&
                InAxisRange(latestTimestamp, axis, visibleRange))
            {
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

            DrawValueAxis(draw, values, plotOrigin, plotWidth, plotHeight);
            if (drawTimeAxis) {
                DrawTimeAxis(
                    draw,
                    axis,
                    visibleRange,
                    plotOrigin,
                    plotWidth,
                    plotHeight);
            }

            if (paneHovered) {
                const float crossX = MapX(
                    state.crosshairTimestampMs,
                    axis,
                    visibleRange,
                    plotOrigin.x,
                    plotWidth);
                const float crossY = MapY(
                    state.crosshairValue,
                    values,
                    plotOrigin.y,
                    plotHeight);
                DrawCursorTimeLabel(
                    draw,
                    plotOrigin,
                    plotEnd,
                    crossX,
                    state.crosshairTimestampMs,
                    drawTimeAxis);
                DrawCursorValueLabel(
                    draw,
                    plotOrigin,
                    plotEnd,
                    crossY,
                    state.crosshairValue,
                    pane.valueDecimals);

                const Bar* nearest = NearestVisibleBar(
                    pane,
                    state.crosshairTimestampMs,
                    axis,
                    visibleRange);
                if (nearest != nullptr) {
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
        surfaceState.selectionChanged = false;
        surfaceState.selectionDoubleClicked = false;
        if (document.panes.empty()) return;

        const std::uint64_t structureRevision =
            document.structureRevision != 0
                ? document.structureRevision
                : document.revision;
        if (surfaceState.timeAxisRevision != structureRevision) {
            std::vector<EpochMillis> timestamps =
                PrimaryCandleTimestamps(document);
            if (timestamps.empty()) {
                timestamps = CollectDocumentTimestamps(document);
            }
            std::string axisError;
            if (!surfaceState.timeAxis.Reset(timestamps, axisError)) {
                surfaceState.timeAxis.Clear();
                surfaceState.viewport = {};
                return;
            }
            surfaceState.timeAxisRevision = structureRevision;
        }
        if (surfaceState.timeAxis.Empty()) return;

        const std::uint64_t boundaryStructureRevision = structureRevision;
        if (surfaceState.boundaryRevision != boundaryStructureRevision) {
            surfaceState.timeBoundaries = render::FindTimeBoundaries(
                surfaceState.timeAxis.Timestamps());
            surfaceState.boundaryRevision = boundaryStructureRevision;
        }

        AxisRange dataRange;
        dataRange.minimum = surfaceState.timeAxis.Minimum();
        dataRange.maximum = surfaceState.timeAxis.Maximum();
        dataRange.valid = dataRange.maximum > dataRange.minimum;
        if (!dataRange.valid) return;

        surfaceState.defaultVisibleSpan = DefaultVisibleSpan(
            size.x,
            surfaceState.timeAxis.Size());
        if (!surfaceState.viewport.initialized) {
            render::ResetViewport(
                surfaceState.viewport,
                dataRange.minimum,
                dataRange.maximum,
                surfaceState.defaultVisibleSpan);
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
            MinimumVisibleSpan);

        AxisRange visibleRange;
        visibleRange.minimum = surfaceState.viewport.visibleStart;
        visibleRange.maximum = surfaceState.viewport.visibleEnd;
        visibleRange.valid = visibleRange.maximum > visibleRange.minimum;
        if (!visibleRange.valid) return;

        surfaceState.crosshairVisible = false;

        for (const render::Pane& pane : document.panes) {
            const float documentWeight =
                (std::max)(0.01f, pane.heightWeight);
            const auto current =
                surfaceState.paneHeightWeights.find(pane.id);
            const auto previousDefault =
                surfaceState.paneDefaultHeightWeights.find(pane.id);
            const bool missing =
                current == surfaceState.paneHeightWeights.end() ||
                previousDefault ==
                    surfaceState.paneDefaultHeightWeights.end();
            const bool invalid =
                !missing &&
                (!std::isfinite(current->second) ||
                 current->second <= 0.0f);
            const bool configuredWeightChanged =
                !missing &&
                std::fabs(
                    previousDefault->second -
                    documentWeight) > 0.0001f;
            if (missing || invalid || configuredWeightChanged) {
                surfaceState.paneHeightWeights[pane.id] =
                    documentWeight;
            }
            surfaceState.paneDefaultHeightWeights[pane.id] =
                documentWeight;
        }

        float totalWeight = 0.0f;
        for (const render::Pane& pane : document.panes) {
            totalWeight += surfaceState.paneHeightWeights[pane.id];
        }

        const float splitterSpace = PaneSplitterHeight *
            static_cast<float>((std::max)(
                static_cast<std::size_t>(0),
                document.panes.size() - 1U));
        const float availableHeight =
            (std::max)(0.0f, size.y - splitterSpace);
        std::vector<PaneGeometry> paneGeometries;
        paneGeometries.reserve(document.panes.size());

        for (std::size_t index = 0; index < document.panes.size(); ++index) {
            const render::Pane& pane = document.panes[index];
            const float paneHeight =
                availableHeight *
                surfaceState.paneHeightWeights[pane.id] /
                totalWeight;
            DrawPane(
                pane,
                surfaceState.timeAxis,
                dataRange,
                visibleRange,
                surfaceState.defaultVisibleSpan,
                index + 1 == document.panes.size(),
                ImVec2(size.x, paneHeight),
                surfaceState,
                paneGeometries);
            if (index + 1 < document.panes.size()) {
                DrawPaneSplitter(
                    pane,
                    document.panes[index + 1U],
                    size.x,
                    availableHeight,
                    totalWeight,
                    surfaceState);
            }
        }

        if (
            surfaceState.crosshairVisible &&
            InAxisRange(
                surfaceState.crosshairTimestampMs,
                surfaceState.timeAxis,
                visibleRange))
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            for (const PaneGeometry& geometry : paneGeometries) {
                if (!geometry.valid) continue;
                const float width = geometry.plotEnd.x - geometry.plotOrigin.x;
                const float crossX = MapX(
                    surfaceState.crosshairTimestampMs,
                    surfaceState.timeAxis,
                    visibleRange,
                    geometry.plotOrigin.x,
                    width);
                draw->AddLine(
                    ImVec2(crossX, geometry.plotOrigin.y),
                    ImVec2(crossX, geometry.plotEnd.y),
                    IM_COL32(205, 208, 220, 180),
                    1.0f);
            }
        }

        surfaceState.renderedRevision = document.revision;
        surfaceState.dirty = false;
    }
}
