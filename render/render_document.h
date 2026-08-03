#pragma once

#include "../core/market_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace trading::render
{
    struct ColorRgba final
    {
        std::uint8_t red = 255;
        std::uint8_t green = 255;
        std::uint8_t blue = 255;
        std::uint8_t alpha = 255;
    };

    struct LinePoint final
    {
        EpochMillis timestampMs = 0;
        double value = 0.0;
    };

    struct HistogramPoint final
    {
        EpochMillis timestampMs = 0;
        double value = 0.0;
        bool positive = true;
    };

    enum class MarkerShape
    {
        Circle,
        TriangleUp,
        TriangleDown,
        Diamond,
        Square
    };

    struct MarkerPoint final
    {
        EpochMillis timestampMs = 0;
        double value = 0.0;
        MarkerShape shape = MarkerShape::Circle;
        std::string label;
        ColorRgba color;
    };

    struct CandleSeries final
    {
        std::string id;
        std::string label;
        std::vector<Bar> bars;
        ColorRgba upColor{ 235, 72, 72, 255 };
        ColorRgba downColor{ 70, 130, 240, 255 };
        bool visible = true;
    };

    struct LineSeries final
    {
        std::string id;
        std::string label;
        std::vector<LinePoint> points;
        ColorRgba color;
        float width = 1.0f;
        bool visible = true;
    };

    struct HistogramSeries final
    {
        std::string id;
        std::string label;
        std::vector<HistogramPoint> points;
        ColorRgba positiveColor{ 235, 72, 72, 255 };
        ColorRgba negativeColor{ 70, 130, 240, 255 };
        bool visible = true;
    };

    struct MarkerSeries final
    {
        std::string id;
        std::string label;
        std::vector<MarkerPoint> points;
        bool visible = true;
    };

    struct ReferenceLine final
    {
        std::string id;
        std::string label;
        double value = 0.0;
        ColorRgba color;
        float width = 1.0f;
        bool visible = true;
    };

    struct TextAnnotation final
    {
        std::string id;
        EpochMillis timestampMs = 0;
        double value = 0.0;
        std::string text;
        ColorRgba color;
        bool visible = true;
    };

    enum class PaneValueScale
    {
        Auto,
        Fixed,
        Symmetric
    };

    struct Pane final
    {
        std::string id;
        std::string title;
        float heightWeight = 1.0f;
        PaneValueScale valueScale = PaneValueScale::Auto;
        double fixedMinimum = 0.0;
        double fixedMaximum = 0.0;
        std::vector<CandleSeries> candles;
        std::vector<LineSeries> lines;
        std::vector<HistogramSeries> histograms;
        std::vector<MarkerSeries> markers;
        std::vector<ReferenceLine> referenceLines;
        std::vector<TextAnnotation> annotations;
    };

    struct InteractionState final
    {
        EpochMillis visibleStartMs = 0;
        EpochMillis visibleEndMs = 0;
        EpochMillis crosshairTimestampMs = 0;
        double crosshairValue = 0.0;
        bool autoScroll = true;
        bool crosshairVisible = false;
    };

    struct RenderDocument final
    {
        std::uint64_t revision = 0;
        std::string workspaceId;
        std::string title;
        std::vector<Pane> panes;
        InteractionState interaction;
    };

    bool ValidateRenderDocument(
        const RenderDocument& document,
        std::string& error);
}
