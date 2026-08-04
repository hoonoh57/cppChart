from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def write(path: str, content: str) -> None:
    (ROOT / path).write_text(content, encoding="utf-8-sig", newline="")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


# Render contract: generic secondary value axes for line series.
path = "render/render_document.h"
text = read(path)
text = replace_once(
    text,
    "        LineStyle style = LineStyle::Solid;\n"
    "        bool visible = true;\n"
    "        std::string ownerId;\n"
    "    };\n\n"
    "    struct HistogramSeries final",
    "        LineStyle style = LineStyle::Solid;\n"
    "        bool visible = true;\n"
    "        std::string ownerId;\n"
    "        std::string axisId;\n"
    "    };\n\n"
    "    struct HistogramSeries final",
    "line axis id")
text = replace_once(
    text,
    "    struct Pane final\n",
    "    enum class ValueAxisSide\n"
    "    {\n"
    "        Left,\n"
    "        Right\n"
    "    };\n\n"
    "    struct ValueAxis final\n"
    "    {\n"
    "        std::string id;\n"
    "        std::string label;\n"
    "        ValueAxisSide side = ValueAxisSide::Left;\n"
    "        PaneValueScale valueScale = PaneValueScale::Auto;\n"
    "        double fixedMinimum = 0.0;\n"
    "        double fixedMaximum = 0.0;\n"
    "        ValueGrid cursorGrid;\n"
    "        int valueDecimals = 2;\n"
    "        ColorRgba color{ 180, 184, 194, 255 };\n"
    "        bool visible = true;\n"
    "    };\n\n"
    "    struct Pane final\n",
    "value axis contract")
text = replace_once(
    text,
    "        int valueDecimals = 2;\n"
    "        std::vector<LegendEntry> legends;\n",
    "        int valueDecimals = 2;\n"
    "        std::vector<ValueAxis> valueAxes;\n"
    "        std::vector<LegendEntry> legends;\n",
    "pane value axes")
write(path, text)

path = "render/render_document.cpp"
text = read(path)
text = replace_once(
    text,
    "            for (const LegendEntry& legend : pane.legends) {\n",
    "            std::set<std::string> axisIds;\n"
    "            for (const ValueAxis& axis : pane.valueAxes) {\n"
    "                if (axis.id.empty() || !axisIds.insert(axis.id).second) {\n"
    "                    error = \"duplicate or empty value axis id: \" + pane.id;\n"
    "                    return false;\n"
    "                }\n"
    "                if (axis.valueDecimals < 0 || axis.valueDecimals > 8 ||\n"
    "                    !ValidColor(axis.color))\n"
    "                {\n"
    "                    error = \"render value axis metadata is invalid: \" + axis.id;\n"
    "                    return false;\n"
    "                }\n"
    "                if (axis.valueScale == PaneValueScale::Fixed &&\n"
    "                    (!std::isfinite(axis.fixedMinimum) ||\n"
    "                     !std::isfinite(axis.fixedMaximum) ||\n"
    "                     axis.fixedMaximum <= axis.fixedMinimum))\n"
    "                {\n"
    "                    error = \"fixed value axis range is invalid: \" + axis.id;\n"
    "                    return false;\n"
    "                }\n"
    "            }\n\n"
    "            for (const LegendEntry& legend : pane.legends) {\n",
    "axis validation")
text = replace_once(
    text,
    "            for (const LineSeries& series : pane.lines) {\n"
    "                if (!AddUnique(elementIds, series.id, error)) return false;\n",
    "            for (const LineSeries& series : pane.lines) {\n"
    "                if (!AddUnique(elementIds, series.id, error)) return false;\n"
    "                if (!series.axisId.empty() &&\n"
    "                    axisIds.find(series.axisId) == axisIds.end())\n"
    "                {\n"
    "                    error = \"line series references missing value axis: \" + series.id;\n"
    "                    return false;\n"
    "                }\n",
    "line axis validation")
write(path, text)

# Renderer: primary axis excludes secondary lines; secondary axes get independent ranges.
path = "ui/render_document_renderer.cpp"
text = read(path)
text = replace_once(
    text,
    "#include <limits>\n#include <string>\n#include <vector>\n",
    "#include <limits>\n#include <map>\n#include <string>\n#include <vector>\n",
    "renderer map include")
text = replace_once(
    text,
    "            for (const render::LineSeries& series : pane.lines) {\n"
    "                if (!series.visible) continue;\n",
    "            for (const render::LineSeries& series : pane.lines) {\n"
    "                if (!series.visible || !series.axisId.empty()) continue;\n",
    "exclude secondary lines from primary range")
needle = "        float MapX(\n"
secondary_helpers = r'''        const render::ValueAxis* FindValueAxis(
            const render::Pane& pane,
            const std::string& axisId) noexcept
        {
            for (const render::ValueAxis& axis : pane.valueAxes) {
                if (axis.id == axisId) return &axis;
            }
            return nullptr;
        }

        ValueRange SecondaryAxisRange(
            const render::Pane& pane,
            const render::ValueAxis& metadata,
            const render::OrdinalTimeAxis& axis,
            const AxisRange& range)
        {
            ValueRange result;
            if (metadata.valueScale == render::PaneValueScale::Fixed) {
                result.minimum = metadata.fixedMinimum;
                result.maximum = metadata.fixedMaximum;
                result.valid = true;
                return result;
            }
            for (const render::LineSeries& series : pane.lines) {
                if (!series.visible || series.axisId != metadata.id) continue;
                for (const render::LinePoint& point : series.points) {
                    if (InAxisRange(point.timestampMs, axis, range)) {
                        result.Include(point.value);
                    }
                }
            }
            if (!result.valid) return result;
            if (metadata.valueScale == render::PaneValueScale::Symmetric) {
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

        const ValueRange* FindSecondaryRange(
            const std::map<std::string, ValueRange>& ranges,
            const std::string& axisId) noexcept
        {
            const auto found = ranges.find(axisId);
            return found == ranges.end() || !found->second.valid
                ? nullptr
                : &found->second;
        }

'''
text = replace_once(text, needle, secondary_helpers + needle, "secondary range helpers")
axis_needle = "        void DrawTimeAxis(\n"
axis_helper = r'''        void DrawSecondaryValueAxis(
            ImDrawList* draw,
            const render::ValueAxis& metadata,
            const ValueRange& values,
            const ImVec2& plotOrigin,
            float plotHeight,
            std::size_t column)
        {
            const float right =
                plotOrigin.x - static_cast<float>(column) * ValueAxisWidth;
            const float left = right - ValueAxisWidth;
            draw->AddLine(
                ImVec2(right - 1.0f, plotOrigin.y),
                ImVec2(right - 1.0f, plotOrigin.y + plotHeight),
                ToImColor(metadata.color),
                1.0f);
            if (!metadata.label.empty()) {
                draw->AddText(
                    ImVec2(left + 4.0f, plotOrigin.y + 2.0f),
                    ToImColor(metadata.color),
                    metadata.label.c_str());
            }
            char label[48]{};
            for (int index = 0; index <= 5; ++index) {
                const double ratio = static_cast<double>(index) / 5.0;
                const double value =
                    values.maximum - ratio * (values.maximum - values.minimum);
                const float y =
                    plotOrigin.y + plotHeight * static_cast<float>(ratio);
                FormatPaneValue(
                    label,
                    sizeof(label),
                    value,
                    metadata.valueDecimals);
                draw->AddText(
                    ImVec2(left + 4.0f, y - 7.0f),
                    ToImColor(metadata.color),
                    label);
            }
        }

'''
text = replace_once(text, axis_needle, axis_helper + axis_needle, "secondary axis drawing")
old_layout = (
    "            const float timeAxisHeight = drawTimeAxis ? TimeAxisHeight : 0.0f;\n"
    "            const float plotWidth = (std::max)(40.0f, size.x - ValueAxisWidth);\n"
    "            const float plotHeight = (std::max)(30.0f, size.y - timeAxisHeight);\n"
    "            const ValueRange values = PaneValueRange(pane, axis, visibleRange);\n\n"
    "            ImGui::PushID(pane.id.c_str());\n")
new_layout = (
    "            const float timeAxisHeight = drawTimeAxis ? TimeAxisHeight : 0.0f;\n"
    "            std::vector<const render::ValueAxis*> leftAxes;\n"
    "            std::map<std::string, ValueRange> secondaryRanges;\n"
    "            for (const render::ValueAxis& metadata : pane.valueAxes) {\n"
    "                if (!metadata.visible ||\n"
    "                    metadata.side != render::ValueAxisSide::Left)\n"
    "                {\n"
    "                    continue;\n"
    "                }\n"
    "                ValueRange range = SecondaryAxisRange(\n"
    "                    pane, metadata, axis, visibleRange);\n"
    "                if (!range.valid) continue;\n"
    "                leftAxes.push_back(&metadata);\n"
    "                secondaryRanges.emplace(metadata.id, range);\n"
    "            }\n"
    "            const float leftAxisWidth =\n"
    "                static_cast<float>(leftAxes.size()) * ValueAxisWidth;\n"
    "            const float plotWidth = (std::max)(\n"
    "                40.0f,\n"
    "                size.x - ValueAxisWidth - leftAxisWidth);\n"
    "            const float plotHeight = (std::max)(30.0f, size.y - timeAxisHeight);\n"
    "            const ValueRange values = PaneValueRange(pane, axis, visibleRange);\n\n"
    "            ImGui::PushID(pane.id.c_str());\n")
text = replace_once(text, old_layout, new_layout, "secondary axis layout")
text = replace_once(
    text,
    "            const ImVec2 plotOrigin = surfaceOrigin;\n",
    "            const ImVec2 plotOrigin(\n"
    "                surfaceOrigin.x + leftAxisWidth,\n"
    "                surfaceOrigin.y);\n",
    "plot origin secondary axes")
old_line_map = (
    "            for (const render::LineSeries& series : pane.lines) {\n"
    "                if (!series.visible) continue;\n"
    "                const bool selected =\n"
    "                    OwnerSelected(series.ownerId, state);\n"
    "                bool hasPrevious = false;\n")
new_line_map = (
    "            for (const render::LineSeries& series : pane.lines) {\n"
    "                if (!series.visible) continue;\n"
    "                const ValueRange* seriesValues = &values;\n"
    "                if (!series.axisId.empty()) {\n"
    "                    seriesValues = FindSecondaryRange(\n"
    "                        secondaryRanges,\n"
    "                        series.axisId);\n"
    "                    if (seriesValues == nullptr) continue;\n"
    "                }\n"
    "                const bool selected =\n"
    "                    OwnerSelected(series.ownerId, state);\n"
    "                bool hasPrevious = false;\n")
text = replace_once(text, old_line_map, new_line_map, "line axis range selection")
text = replace_once(
    text,
    "                            point.value,\n"
    "                            values,\n"
    "                            plotOrigin.y,\n"
    "                            plotHeight));\n"
    "                    if (hasPrevious) {\n",
    "                            point.value,\n"
    "                            *seriesValues,\n"
    "                            plotOrigin.y,\n"
    "                            plotHeight));\n"
    "                    if (hasPrevious) {\n",
    "line map secondary range")
text = replace_once(
    text,
    "            DrawValueAxis(draw, values, plotOrigin, plotWidth, plotHeight);\n"
    "            if (drawTimeAxis) {\n",
    "            DrawValueAxis(draw, values, plotOrigin, plotWidth, plotHeight);\n"
    "            for (std::size_t index = 0; index < leftAxes.size(); ++index) {\n"
    "                const auto found = secondaryRanges.find(leftAxes[index]->id);\n"
    "                if (found != secondaryRanges.end()) {\n"
    "                    DrawSecondaryValueAxis(\n"
    "                        draw,\n"
    "                        *leftAxes[index],\n"
    "                        found->second,\n"
    "                        plotOrigin,\n"
    "                        plotHeight,\n"
    "                        index);\n"
    "                }\n"
    "            }\n"
    "            if (drawTimeAxis) {\n",
    "draw secondary axes")
write(path, text)

# Register and describe the standard indicator pack.
path = "app/indicator_module.cpp"
text = read(path)
text = replace_once(
    text,
    "#include \"../core/sma_indicator.h\"\n",
    "#include \"../core/sma_indicator.h\"\n"
    "#include \"../core/standard_indicators.h\"\n",
    "standard indicator include")
text = replace_once(
    text,
    "            indicators::RegisterVwapIndicator(registry_);\n",
    "            indicators::RegisterVwapIndicator(registry_) &&\n"
    "            indicators::RegisterEmaIndicator(registry_) &&\n"
    "            indicators::RegisterBollingerIndicator(registry_) &&\n"
    "            indicators::RegisterRsiIndicator(registry_) &&\n"
    "            indicators::RegisterMacdIndicator(registry_) &&\n"
    "            indicators::RegisterDmiIndicator(registry_) &&\n"
    "            indicators::RegisterSuperTrendIndicator(registry_);\n",
    "standard indicator registration")
write(path, text)

path = "app/indicator_properties.cpp"
text = read(path)
text = replace_once(
    text,
    "        else if (spec.type == \"JMA\") {\n",
    "        else if (spec.type == \"EMA\") {\n"
    "            candidate.displayName = \"EMA\";\n"
    "            candidate.parameters.push_back(\n"
    "                IntegerParameter(\"period\", \"Period\", 1, 100000));\n"
    "        }\n"
    "        else if (spec.type == \"BOLLINGER\") {\n"
    "            candidate.displayName = \"Bollinger Bands\";\n"
    "            candidate.parameters.push_back(\n"
    "                IntegerParameter(\"period\", \"Period\", 2, 100000));\n"
    "            candidate.parameters.push_back(\n"
    "                DecimalParameter(\"deviation\", \"Deviation\", 0.0, 100.0, 0.1, 1.0));\n"
    "        }\n"
    "        else if (spec.type == \"RSI\") {\n"
    "            candidate.displayName = \"RSI\";\n"
    "            candidate.parameters.push_back(\n"
    "                IntegerParameter(\"period\", \"Period\", 1, 10000));\n"
    "        }\n"
    "        else if (spec.type == \"MACD\") {\n"
    "            candidate.displayName = \"MACD\";\n"
    "            candidate.parameters.push_back(IntegerParameter(\"fast_period\", \"Fast period\", 1, 10000));\n"
    "            candidate.parameters.push_back(IntegerParameter(\"slow_period\", \"Slow period\", 2, 10000));\n"
    "            candidate.parameters.push_back(IntegerParameter(\"signal_period\", \"Signal period\", 1, 10000));\n"
    "        }\n"
    "        else if (spec.type == \"DMI\") {\n"
    "            candidate.displayName = \"DMI\";\n"
    "            candidate.parameters.push_back(\n"
    "                IntegerParameter(\"period\", \"Period\", 1, 10000));\n"
    "        }\n"
    "        else if (spec.type == \"SUPERTREND\") {\n"
    "            candidate.displayName = \"SuperTrend\";\n"
    "            candidate.parameters.push_back(\n"
    "                IntegerParameter(\"period\", \"ATR period\", 1, 10000));\n"
    "            candidate.parameters.push_back(\n"
    "                DecimalParameter(\"multiplier\", \"Multiplier\", 0.01, 100.0, 0.1, 1.0));\n"
    "        }\n"
    "        else if (spec.type == \"JMA\") {\n",
    "standard property descriptions")
label_insert = r'''        if (spec.type == "EMA" &&
            TryParameter(spec, "period", period))
        {
            return "EMA " + FormatNumber(period);
        }
        if (spec.type == "BOLLINGER" &&
            TryParameter(spec, "period", period) &&
            TryParameter(spec, "deviation", power))
        {
            return "BB " + FormatNumber(period) + " x" + FormatNumber(power);
        }
        if (spec.type == "RSI" &&
            TryParameter(spec, "period", period))
        {
            return "RSI " + FormatNumber(period);
        }
        if (spec.type == "MACD") {
            double fast = 0.0;
            double slow = 0.0;
            if (TryParameter(spec, "fast_period", fast) &&
                TryParameter(spec, "slow_period", slow) &&
                TryParameter(spec, "signal_period", signalPeriod))
            {
                return "MACD " + FormatNumber(fast) + "/" +
                    FormatNumber(slow) + "/" + FormatNumber(signalPeriod);
            }
        }
        if (spec.type == "DMI" &&
            TryParameter(spec, "period", period))
        {
            return "DMI " + FormatNumber(period);
        }
        if (spec.type == "SUPERTREND" &&
            TryParameter(spec, "period", period) &&
            TryParameter(spec, "multiplier", power))
        {
            return "SuperTrend " + FormatNumber(period) + " x" +
                FormatNumber(power);
        }
'''
text = replace_once(
    text,
    "        if (spec.type == \"JMA\" &&\n",
    label_insert + "        if (spec.type == \"JMA\" &&\n",
    "standard indicator labels")
write(path, text)

# Indicator configuration catalog, bindings, and defaults.
path = "app/indicator_configuration.cpp"
text = read(path)
text = replace_once(
    text,
    "#include \"../core/obv_indicator.h\"\n",
    "#include \"../core/obv_indicator.h\"\n"
    "#include \"../core/standard_indicators.h\"\n",
    "configuration standard include")
text = replace_once(
    text,
    "            { \"SMA\", \"SMA 단순이동평균\" },\n",
    "            { \"SMA\", \"SMA 단순이동평균\" },\n"
    "            { \"EMA\", \"EMA 지수이동평균\" },\n"
    "            { \"BOLLINGER\", \"Bollinger Bands\" },\n"
    "            { \"RSI\", \"RSI 상대강도\" },\n"
    "            { \"MACD\", \"MACD 추세모멘텀\" },\n"
    "            { \"DMI\", \"DMI +DI/-DI/ADX\" },\n"
    "            { \"SUPERTREND\", \"SuperTrend\" },\n",
    "indicator catalog expansion")
insert_before_jma = r'''        else if (spec.type == "EMA") {
            candidate.outputs.push_back(LineBinding(
                spec, indicators::EmaValueOutput, "price", "Price",
                "value", "EMA", {}, { 255, 154, 64, 255 }, 1.6f));
        }
        else if (spec.type == "BOLLINGER") {
            candidate.outputs.push_back(LineBinding(
                spec, indicators::BollingerMiddleOutput, "price", "Price",
                "middle", "Middle", {}, { 222, 222, 230, 230 }, 1.2f));
            candidate.outputs.push_back(LineBinding(
                spec, indicators::BollingerUpperOutput, "price", "Price",
                "upper", "Upper", {}, { 190, 92, 235, 220 }, 1.1f));
            candidate.outputs.push_back(LineBinding(
                spec, indicators::BollingerLowerOutput, "price", "Price",
                "lower", "Lower", {}, { 190, 92, 235, 220 }, 1.1f));
        }
        else if (spec.type == "RSI") {
            const std::string paneId = "indicator." + spec.id + ".pane";
            IndicatorOutputBinding value = LineBinding(
                spec, indicators::RsiValueOutput, paneId, "RSI",
                "value", "RSI", {}, { 182, 120, 255, 255 }, 1.6f);
            ConfigureAdxPane(value);
            candidate.outputs.push_back(value);
            IndicatorReferenceBinding overbought = ReferenceBinding(
                spec, paneId, "RSI", "reference.70", "Overbought", 70.0);
            ConfigureAdxReference(overbought);
            overbought.color = { 235, 92, 92, 210 };
            candidate.references.push_back(overbought);
            IndicatorReferenceBinding oversold = ReferenceBinding(
                spec, paneId, "RSI", "reference.30", "Oversold", 30.0);
            ConfigureAdxReference(oversold);
            oversold.color = { 72, 154, 235, 210 };
            candidate.references.push_back(oversold);
        }
        else if (spec.type == "MACD") {
            const std::string paneId = "indicator." + spec.id + ".pane";
            IndicatorOutputBinding macd = LineBinding(
                spec, indicators::MacdValueOutput, paneId, "MACD",
                "value", "MACD", {}, { 64, 210, 225, 255 }, 1.5f);
            macd.paneHeightWeight = 0.30f;
            macd.paneValueScale = render::PaneValueScale::Symmetric;
            macd.valueDecimals = 2;
            candidate.outputs.push_back(macd);
            IndicatorOutputBinding signal = LineBinding(
                spec, indicators::MacdSignalOutput, paneId, "MACD",
                "signal", "Signal", {}, { 255, 196, 64, 255 }, 1.2f);
            signal.paneHeightWeight = 0.30f;
            signal.paneValueScale = render::PaneValueScale::Symmetric;
            signal.valueDecimals = 2;
            candidate.outputs.push_back(signal);
            IndicatorOutputBinding histogram = HistogramBinding(
                spec, indicators::MacdHistogramOutput, paneId, "MACD",
                "histogram", "Histogram", {},
                { 58, 196, 125, 210 }, { 235, 80, 92, 210 });
            histogram.paneHeightWeight = 0.30f;
            histogram.paneValueScale = render::PaneValueScale::Symmetric;
            histogram.valueDecimals = 2;
            candidate.outputs.push_back(histogram);
            IndicatorReferenceBinding zero = ReferenceBinding(
                spec, paneId, "MACD", "reference.zero", "Zero", 0.0);
            zero.paneHeightWeight = 0.30f;
            zero.paneValueScale = render::PaneValueScale::Symmetric;
            zero.valueDecimals = 2;
            candidate.references.push_back(zero);
        }
        else if (spec.type == "DMI") {
            const std::string paneId = "indicator." + spec.id + ".pane";
            IndicatorOutputBinding plus = LineBinding(
                spec, indicators::DmiPlusOutput, paneId, "DMI",
                "plus", "+DI", {}, { 58, 196, 125, 255 }, 1.5f);
            ConfigureAdxPane(plus);
            candidate.outputs.push_back(plus);
            IndicatorOutputBinding minus = LineBinding(
                spec, indicators::DmiMinusOutput, paneId, "DMI",
                "minus", "-DI", {}, { 235, 80, 92, 255 }, 1.5f);
            ConfigureAdxPane(minus);
            candidate.outputs.push_back(minus);
            IndicatorOutputBinding adx = LineBinding(
                spec, indicators::DmiAdxOutput, paneId, "DMI",
                "adx", "ADX", {}, { 255, 196, 64, 255 }, 1.6f);
            ConfigureAdxPane(adx);
            candidate.outputs.push_back(adx);
            IndicatorReferenceBinding twenty = ReferenceBinding(
                spec, paneId, "DMI", "reference.20", "20", 20.0);
            ConfigureAdxReference(twenty);
            candidate.references.push_back(twenty);
            IndicatorReferenceBinding twentyFive = ReferenceBinding(
                spec, paneId, "DMI", "reference.25", "25", 25.0);
            ConfigureAdxReference(twentyFive);
            candidate.references.push_back(twentyFive);
        }
        else if (spec.type == "SUPERTREND") {
            candidate.outputs.push_back(LineBinding(
                spec, indicators::SuperTrendUpOutput, "price", "Price",
                "up", "Up", {}, { 58, 196, 125, 255 }, 2.0f));
            candidate.outputs.push_back(LineBinding(
                spec, indicators::SuperTrendDownOutput, "price", "Price",
                "down", "Down", {}, { 235, 80, 92, 255 }, 2.0f));
        }
'''
text = replace_once(
    text,
    "        else if (spec.type == \"JMA\") {\n",
    insert_before_jma + "        else if (spec.type == \"JMA\") {\n",
    "standard indicator bindings")
defaults = r'''        else if (type == "EMA") {
            spec.parameters.emplace("period", 20.0);
        }
        else if (type == "BOLLINGER") {
            spec.parameters.emplace("period", 20.0);
            spec.parameters.emplace("deviation", 2.0);
        }
        else if (type == "RSI") {
            spec.parameters.emplace("period", 14.0);
        }
        else if (type == "MACD") {
            spec.parameters.emplace("fast_period", 12.0);
            spec.parameters.emplace("slow_period", 26.0);
            spec.parameters.emplace("signal_period", 9.0);
        }
        else if (type == "DMI") {
            spec.parameters.emplace("period", 14.0);
        }
        else if (type == "SUPERTREND") {
            spec.parameters.emplace("period", 14.0);
            spec.parameters.emplace("multiplier", 2.0);
        }
'''
text = replace_once(
    text,
    "        else if (type == \"JMA\") {\n",
    defaults + "        else if (type == \"JMA\") {\n",
    "standard indicator defaults")
write(path, text)

# Tests and production build.
path = "build.bat"
text = read(path)
text = replace_once(
    text,
    "   core\\sma_indicator.cpp ^\n",
    "   core\\sma_indicator.cpp ^\n"
    "   core\\standard_indicators.cpp ^\n",
    "build standard indicators")
write(path, text)

path = "tests/run_all.bat"
text = read(path)
text = replace_once(
    text,
    "call :build_and_run jma_indicator_tests.exe",
    "call :build_and_run standard_indicators_tests.exe \"tests\\standard_indicators_tests.cpp core\\json_lite.cpp core\\indicator_engine.cpp core\\standard_indicators.cpp\"\n"
    "if errorlevel 1 exit /b 1\n\n"
    "call :build_and_run jma_indicator_tests.exe",
    "standard indicator test")
text = text.replace(
    "core\\vwap_indicator.cpp app\\indicator_module.cpp",
    "core\\vwap_indicator.cpp core\\standard_indicators.cpp app\\indicator_module.cpp")
write(path, text)

path = "tests/indicator_configuration_tests.cpp"
text = read(path)
text = replace_once(
    text,
    "    Check(IndicatorCatalog().size() == 5U,\n",
    "    Check(IndicatorCatalog().size() == 11U,\n",
    "expanded catalog count")
write(path, text)

print("indicator pack and dual-axis contract applied")
