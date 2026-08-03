from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")

def write(path: str, text: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8-sig", newline="")

def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)

path = "render/render_document.h"
text = read(path)
text = replace_once(
    text,
    '''    struct LinePoint final
''',
    '''    struct LegendEntry final
    {
        std::string id;
        std::string ownerId;
        std::string label;
        ColorRgba color;
        bool selectable = true;
        bool visible = true;
    };

    struct LinePoint final
''',
    "legend entry contract")
for struct_name in ("CandleSeries", "LineSeries", "HistogramSeries", "MarkerSeries", "ReferenceLine"):
    start = text.index(f"    struct {struct_name} final")
    end = text.index("    };", start) + len("    };")
    block = text[start:end]
    block_new = replace_once(
        block,
        '''        bool visible = true;
''',
        '''        bool visible = true;
        std::string ownerId;
''',
        f"{struct_name} owner id")
    text = text[:start] + block_new + text[end:]
text = replace_once(
    text,
    '''        int valueDecimals = 2;
        std::vector<CandleSeries> candles;
''',
    '''        int valueDecimals = 2;
        std::vector<LegendEntry> legends;
        std::vector<CandleSeries> candles;
''',
    "pane legends")
write(path, text)

path = "render/render_document.cpp"
text = read(path)
text = replace_once(
    text,
    '''            for (const CandleSeries& series : pane.candles) {
''',
    '''            for (const LegendEntry& legend : pane.legends) {
                if (!AddUnique(elementIds, legend.id, error)) return false;
                if (legend.label.empty()) {
                    error = "render legend label is empty: " + legend.id;
                    return false;
                }
                if (legend.selectable && legend.ownerId.empty()) {
                    error = "selectable render legend owner is empty: " + legend.id;
                    return false;
                }
                if (!ValidColor(legend.color)) {
                    error = "render legend color is invalid: " + legend.id;
                    return false;
                }
            }

            for (const CandleSeries& series : pane.candles) {
''',
    "render legend validation")
write(path, text)

path = "render/market_chart_builder.cpp"
text = read(path)
text = replace_once(
    text,
    '''        pricePane.cursorGrid.fallbackStep = 1000.0;

        CandleSeries candles;
''',
    '''        pricePane.cursorGrid.fallbackStep = 1000.0;

        LegendEntry priceLegend;
        priceLegend.id = seriesId + ".legend.price";
        priceLegend.label = title;
        priceLegend.color = { 232, 232, 238, 255 };
        priceLegend.selectable = false;
        pricePane.legends.push_back(std::move(priceLegend));

        CandleSeries candles;
''',
    "price legend")
text = replace_once(
    text,
    '''        candles.label = title;
        candles.bars.SetShared(
''',
    '''        candles.label = title;
        candles.ownerId = seriesId;
        candles.bars.SetShared(
''',
    "candle owner")
text = replace_once(
    text,
    '''        volumePane.cursorGrid.fallbackStep = 1.0;

        HistogramSeries volume;
''',
    '''        volumePane.cursorGrid.fallbackStep = 1.0;

        LegendEntry volumeLegend;
        volumeLegend.id = seriesId + ".legend.volume";
        volumeLegend.label = "Volume";
        volumeLegend.color = { 170, 174, 188, 255 };
        volumeLegend.selectable = false;
        volumePane.legends.push_back(std::move(volumeLegend));

        HistogramSeries volume;
''',
    "volume legend")
text = replace_once(
    text,
    '''        volume.label = "Volume";

        HistogramPoint liveVolume;
''',
    '''        volume.label = "Volume";
        volume.ownerId = seriesId + ".volume";

        HistogramPoint liveVolume;
''',
    "volume owner")
write(path, text)

path = "app/indicator_render_adapter.h"
text = read(path)
start = text.index("    struct IndicatorOutputBinding final")
end = text.index("    };", start) + len("    };")
block = text[start:end]
block = replace_once(
    block,
    '''        bool visible = true;
''',
    '''        bool visible = true;
        std::string legendLabel;
''',
    "output legend label")
text = text[:start] + block + text[end:]
start = text.index("    struct IndicatorReferenceBinding final")
end = text.index("    };", start) + len("    };")
block = text[start:end]
block = replace_once(
    block,
    '''        bool visible = true;
''',
    '''        bool visible = true;
        std::string indicatorId;
''',
    "reference owner")
text = text[:start] + block + text[end:]
write(path, text)

path = "app/indicator_render_adapter.cpp"
text = read(path)
text = replace_once(
    text,
    '''        void ApplyPaneContract(
            render::Pane& pane,
            const std::string& paneId,
            const std::string& paneTitle,
            float paneHeightWeight,
            render::PaneValueScale paneValueScale,
            double fixedMinimum,
            double fixedMaximum,
            const render::ValueGrid& cursorGrid,
            int valueDecimals)
        {
            pane.id = paneId;
            pane.title = paneTitle.empty() ? paneId : paneTitle;
            pane.heightWeight = paneHeightWeight;
            pane.valueScale = paneValueScale;
            pane.fixedMinimum = fixedMinimum;
            pane.fixedMaximum = fixedMaximum;
            pane.cursorGrid = cursorGrid;
            pane.valueDecimals = valueDecimals;
        }
''',
    '''        void ApplyPaneContract(
            render::Pane& pane,
            const std::string& paneId,
            const std::string& paneTitle,
            float paneHeightWeight,
            render::PaneValueScale paneValueScale,
            double fixedMinimum,
            double fixedMaximum,
            const render::ValueGrid& cursorGrid,
            int valueDecimals)
        {
            pane.id = paneId;
            pane.title = paneTitle.empty() ? paneId : paneTitle;
            pane.heightWeight = paneHeightWeight;
            pane.valueScale = paneValueScale;
            pane.fixedMinimum = fixedMinimum;
            pane.fixedMaximum = fixedMaximum;
            pane.cursorGrid = cursorGrid;
            pane.valueDecimals = valueDecimals;
        }

        bool EnsureLegend(
            render::Pane& pane,
            const std::string& ownerId,
            const std::string& label,
            render::ColorRgba color,
            bool visible,
            std::string& error)
        {
            if (ownerId.empty()) {
                error = "indicator legend owner id is empty";
                return false;
            }
            if (label.empty()) {
                error = "indicator legend label is empty: " + ownerId;
                return false;
            }

            const std::string legendId =
                "legend." + pane.id + "." + ownerId;
            for (const render::LegendEntry& existing : pane.legends) {
                if (existing.id != legendId) continue;
                if (
                    existing.ownerId != ownerId ||
                    existing.label != label)
                {
                    error =
                        "indicator legend contract conflicts: " +
                        legendId;
                    return false;
                }
                error.clear();
                return true;
            }

            render::LegendEntry legend;
            legend.id = legendId;
            legend.ownerId = ownerId;
            legend.label = label;
            legend.color = color;
            legend.selectable = true;
            legend.visible = visible;
            pane.legends.push_back(std::move(legend));
            error.clear();
            return true;
        }
''',
    "indicator legend helper")
text = replace_once(
    text,
    '''            render::Pane* pane =
                FindOrCreatePane(document, binding, error);
            if (pane == nullptr) return false;

            BindingCache& cache = caches_[binding.seriesId];
''',
    '''            render::Pane* pane =
                FindOrCreatePane(document, binding, error);
            if (pane == nullptr) return false;
            if (!EnsureLegend(
                    *pane,
                    binding.indicatorId,
                    binding.legendLabel.empty()
                        ? binding.label
                        : binding.legendLabel,
                    binding.primaryColor,
                    binding.visible,
                    error))
            {
                return false;
            }

            BindingCache& cache = caches_[binding.seriesId];
''',
    "indicator output legend")
text = text.replace(
    '''                    line.visible = binding.visible;
''',
    '''                    line.visible = binding.visible;
                    line.ownerId = binding.indicatorId;
''')
text = replace_once(
    text,
    '''                histogram.visible = binding.visible;
                histogram.points.SetShared(
''',
    '''                histogram.visible = binding.visible;
                histogram.ownerId = binding.indicatorId;
                histogram.points.SetShared(
''',
    "histogram owner")
text = replace_once(
    text,
    '''            line.visible = reference.visible;
            pane->referenceLines.push_back(std::move(line));
''',
    '''            line.visible = reference.visible;
            line.ownerId = reference.indicatorId;
            pane->referenceLines.push_back(std::move(line));
''',
    "reference owner")
text = replace_once(
    text,
    '''            bytes += binding.label.capacity();
''',
    '''            bytes += binding.label.capacity();
            bytes += binding.legendLabel.capacity();
''',
    "binding legend retained bytes")
text = replace_once(
    text,
    '''            bytes += reference.label.capacity();
''',
    '''            bytes += reference.label.capacity();
            bytes += reference.indicatorId.capacity();
''',
    "reference owner retained bytes")
write(path, text)

write("app/indicator_properties.h", r'''#pragma once

#include "../core/indicator_engine.h"

#include <string>
#include <vector>

namespace trading::app
{
    enum class IndicatorParameterKind
    {
        Integer,
        Decimal
    };

    struct IndicatorParameterDescriptor final
    {
        std::string key;
        std::string displayName;
        IndicatorParameterKind kind =
            IndicatorParameterKind::Decimal;
        double minimum = 0.0;
        double maximum = 0.0;
        double step = 1.0;
        double fastStep = 10.0;
    };

    struct IndicatorPropertySnapshot final
    {
        std::string indicatorId;
        std::string indicatorType;
        std::string displayName;
        std::vector<IndicatorParameterDescriptor> parameters;
    };

    bool DescribeIndicatorProperties(
        const indicators::IndicatorSpec& spec,
        IndicatorPropertySnapshot& snapshot,
        std::string& error);

    bool UpdateIndicatorParameter(
        std::vector<indicators::IndicatorSpec>& specs,
        const std::string& indicatorId,
        const std::string& parameterKey,
        double value,
        std::string& error);

    std::string IndicatorLegendLabel(
        const indicators::IndicatorSpec& spec,
        const std::string& role = {});
}
''')

write("app/indicator_properties.cpp", r'''#include "indicator_properties.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace trading::app
{
    namespace
    {
        IndicatorParameterDescriptor IntegerParameter(
            const char* key,
            const char* displayName,
            int minimum,
            int maximum,
            int step = 1,
            int fastStep = 10)
        {
            IndicatorParameterDescriptor result;
            result.key = key;
            result.displayName = displayName;
            result.kind = IndicatorParameterKind::Integer;
            result.minimum = static_cast<double>(minimum);
            result.maximum = static_cast<double>(maximum);
            result.step = static_cast<double>(step);
            result.fastStep = static_cast<double>(fastStep);
            return result;
        }

        IndicatorParameterDescriptor DecimalParameter(
            const char* key,
            const char* displayName,
            double minimum,
            double maximum,
            double step,
            double fastStep)
        {
            IndicatorParameterDescriptor result;
            result.key = key;
            result.displayName = displayName;
            result.kind = IndicatorParameterKind::Decimal;
            result.minimum = minimum;
            result.maximum = maximum;
            result.step = step;
            result.fastStep = fastStep;
            return result;
        }

        bool TryParameter(
            const indicators::IndicatorSpec& spec,
            const std::string& key,
            double& value)
        {
            const auto found = spec.parameters.find(key);
            if (found == spec.parameters.end() ||
                !std::isfinite(found->second))
            {
                return false;
            }
            value = found->second;
            return true;
        }

        std::string FormatNumber(double value)
        {
            if (std::fabs(value - std::round(value)) < 1e-9) {
                return std::to_string(
                    static_cast<long long>(std::llround(value)));
            }

            std::ostringstream output;
            output << std::fixed << std::setprecision(2) << value;
            std::string text = output.str();
            while (!text.empty() && text.back() == '0') text.pop_back();
            if (!text.empty() && text.back() == '.') text.pop_back();
            return text;
        }

        const IndicatorParameterDescriptor* FindDescriptor(
            const IndicatorPropertySnapshot& snapshot,
            const std::string& key)
        {
            for (const IndicatorParameterDescriptor& descriptor :
                 snapshot.parameters)
            {
                if (descriptor.key == key) return &descriptor;
            }
            return nullptr;
        }
    }

    bool DescribeIndicatorProperties(
        const indicators::IndicatorSpec& spec,
        IndicatorPropertySnapshot& snapshot,
        std::string& error)
    {
        IndicatorPropertySnapshot candidate;
        candidate.indicatorId = spec.id;
        candidate.indicatorType = spec.type;

        if (spec.id.empty() || spec.type.empty()) {
            error = "indicator property target is incomplete";
            return false;
        }

        if (spec.type == "SMA") {
            candidate.displayName = "SMA";
            candidate.parameters.push_back(
                IntegerParameter("period", "Period", 1, 100000));
        }
        else if (spec.type == "JMA") {
            candidate.displayName = "JMA";
            candidate.parameters.push_back(
                IntegerParameter("period", "Period", 1, 10000));
            candidate.parameters.push_back(
                IntegerParameter("phase", "Phase", -100, 100));
            candidate.parameters.push_back(
                IntegerParameter("power", "Power", 1, 10000));
        }
        else if (spec.type == "VWAP") {
            candidate.displayName = "VWAP";
            candidate.parameters.push_back(
                DecimalParameter(
                    "std_dev_1",
                    "Standard deviation 1",
                    0.0,
                    100.0,
                    0.1,
                    1.0));
            candidate.parameters.push_back(
                DecimalParameter(
                    "std_dev_2",
                    "Standard deviation 2",
                    0.0,
                    100.0,
                    0.1,
                    1.0));
        }
        else if (spec.type == "OBV") {
            candidate.displayName = "OBV";
            candidate.parameters.push_back(
                IntegerParameter(
                    "signal_period",
                    "Signal period",
                    1,
                    10000));
        }
        else if (spec.type == "ADX") {
            candidate.displayName = "ADX";
            candidate.parameters.push_back(
                IntegerParameter("period", "Period", 1, 10000));
        }
        else {
            error =
                "unsupported indicator property type: " +
                spec.type;
            return false;
        }

        for (const IndicatorParameterDescriptor& descriptor :
             candidate.parameters)
        {
            if (spec.parameters.find(descriptor.key) ==
                spec.parameters.end())
            {
                error =
                    "indicator property parameter is missing: " +
                    descriptor.key;
                return false;
            }
        }
        if (spec.parameters.size() != candidate.parameters.size()) {
            error =
                "indicator property parameter set does not match type: " +
                spec.type;
            return false;
        }

        snapshot = std::move(candidate);
        error.clear();
        return true;
    }

    bool UpdateIndicatorParameter(
        std::vector<indicators::IndicatorSpec>& specs,
        const std::string& indicatorId,
        const std::string& parameterKey,
        double value,
        std::string& error)
    {
        if (!std::isfinite(value)) {
            error = "indicator property value is not finite";
            return false;
        }

        for (indicators::IndicatorSpec& spec : specs) {
            if (spec.id != indicatorId) continue;

            IndicatorPropertySnapshot snapshot;
            if (!DescribeIndicatorProperties(spec, snapshot, error)) {
                return false;
            }
            const IndicatorParameterDescriptor* descriptor =
                FindDescriptor(snapshot, parameterKey);
            if (descriptor == nullptr) {
                error =
                    "indicator property key is not supported: " +
                    parameterKey;
                return false;
            }
            if (
                value < descriptor->minimum ||
                value > descriptor->maximum)
            {
                error =
                    "indicator property value is outside the allowed range: " +
                    parameterKey;
                return false;
            }
            if (
                descriptor->kind == IndicatorParameterKind::Integer &&
                std::fabs(value - std::round(value)) > 1e-9)
            {
                error =
                    "indicator property requires an integer value: " +
                    parameterKey;
                return false;
            }

            spec.parameters[parameterKey] =
                descriptor->kind == IndicatorParameterKind::Integer
                    ? std::round(value)
                    : value;
            error.clear();
            return true;
        }

        error =
            "indicator property target was not found: " +
            indicatorId;
        return false;
    }

    std::string IndicatorLegendLabel(
        const indicators::IndicatorSpec& spec,
        const std::string& role)
    {
        double period = 0.0;
        double phase = 0.0;
        double power = 0.0;
        double stdDev1 = 0.0;
        double stdDev2 = 0.0;
        double signalPeriod = 0.0;

        if (spec.type == "SMA" &&
            TryParameter(spec, "period", period))
        {
            return "SMA " + FormatNumber(period);
        }
        if (spec.type == "JMA" &&
            TryParameter(spec, "period", period) &&
            TryParameter(spec, "phase", phase) &&
            TryParameter(spec, "power", power))
        {
            if (role == "slope") {
                return "JMA Slope " + FormatNumber(period);
            }
            return
                "JMA " + FormatNumber(period) +
                " P" + FormatNumber(phase) +
                " Pow" + FormatNumber(power);
        }
        if (spec.type == "VWAP" &&
            TryParameter(spec, "std_dev_1", stdDev1) &&
            TryParameter(spec, "std_dev_2", stdDev2))
        {
            return
                "VWAP " + FormatNumber(stdDev1) +
                "/" + FormatNumber(stdDev2);
        }
        if (spec.type == "OBV" &&
            TryParameter(spec, "signal_period", signalPeriod))
        {
            return "OBV Signal " + FormatNumber(signalPeriod);
        }
        if (spec.type == "ADX" &&
            TryParameter(spec, "period", period))
        {
            return "ADX " + FormatNumber(period);
        }
        return spec.type.empty() ? spec.id : spec.type;
    }
}
''')

write("app/default_indicator_render_plan.cpp", r'''#include "default_indicator_render_plan.h"

#include "indicator_properties.h"
#include "../core/adx_indicator.h"
#include "../core/jma_indicator.h"
#include "../core/obv_indicator.h"
#include "../core/vwap_indicator.h"

#include <set>
#include <string>

namespace trading::app
{
    namespace
    {
        IndicatorOutputBinding LineBinding(
            const indicators::IndicatorSpec& spec,
            std::size_t outputIndex,
            const std::string& paneId,
            const std::string& paneTitle,
            const std::string& suffix,
            const std::string& label,
            const std::string& legendLabel,
            render::ColorRgba color,
            float width)
        {
            IndicatorOutputBinding binding;
            binding.indicatorId = spec.id;
            binding.outputIndex = outputIndex;
            binding.kind = IndicatorRenderKind::Line;
            binding.paneId = paneId;
            binding.paneTitle = paneTitle;
            binding.seriesId =
                "indicator." + spec.id + "." + suffix;
            binding.label = label;
            binding.legendLabel = legendLabel;
            binding.primaryColor = color;
            binding.width = width;
            return binding;
        }

        IndicatorOutputBinding HistogramBinding(
            const indicators::IndicatorSpec& spec,
            std::size_t outputIndex,
            const std::string& paneId,
            const std::string& paneTitle,
            const std::string& suffix,
            const std::string& label,
            const std::string& legendLabel,
            render::ColorRgba positive,
            render::ColorRgba negative)
        {
            IndicatorOutputBinding binding;
            binding.indicatorId = spec.id;
            binding.outputIndex = outputIndex;
            binding.kind = IndicatorRenderKind::Histogram;
            binding.paneId = paneId;
            binding.paneTitle = paneTitle;
            binding.seriesId =
                "indicator." + spec.id + "." + suffix;
            binding.label = label;
            binding.legendLabel = legendLabel;
            binding.primaryColor = positive;
            binding.secondaryColor = negative;
            return binding;
        }

        IndicatorReferenceBinding ReferenceBinding(
            const indicators::IndicatorSpec& spec,
            const std::string& paneId,
            const std::string& paneTitle,
            const std::string& id,
            const std::string& label,
            double value)
        {
            IndicatorReferenceBinding binding;
            binding.paneId = paneId;
            binding.paneTitle = paneTitle;
            binding.referenceId = id;
            binding.label = label;
            binding.value = value;
            binding.color = { 170, 174, 188, 190 };
            binding.width = 1.0f;
            binding.indicatorId = spec.id;
            return binding;
        }

        void ConfigureSymmetricPane(IndicatorOutputBinding& binding)
        {
            binding.paneHeightWeight = 0.22f;
            binding.paneValueScale = render::PaneValueScale::Symmetric;
            binding.valueDecimals = 2;
        }

        void ConfigureObvPane(IndicatorOutputBinding& binding)
        {
            binding.paneHeightWeight = 0.3f;
            binding.valueDecimals = 0;
        }

        void ConfigureAdxPane(IndicatorOutputBinding& binding)
        {
            binding.paneHeightWeight = 0.25f;
            binding.paneValueScale = render::PaneValueScale::Fixed;
            binding.fixedMinimum = 0.0;
            binding.fixedMaximum = 100.0;
            binding.valueDecimals = 2;
        }

        void ConfigureAdxReference(IndicatorReferenceBinding& binding)
        {
            binding.paneHeightWeight = 0.25f;
            binding.paneValueScale = render::PaneValueScale::Fixed;
            binding.fixedMinimum = 0.0;
            binding.fixedMaximum = 100.0;
            binding.valueDecimals = 2;
        }
    }

    bool BuildDefaultIndicatorRenderPlan(
        const std::vector<indicators::IndicatorSpec>& specs,
        IndicatorRenderPlan& plan,
        std::string& error)
    {
        IndicatorRenderPlan candidate;
        std::set<std::string> ids;

        for (const indicators::IndicatorSpec& spec : specs) {
            if (spec.id.empty()) {
                error = "default indicator render-plan id is empty";
                return false;
            }
            if (!ids.insert(spec.id).second) {
                error =
                    "duplicate default indicator render-plan id: " +
                    spec.id;
                return false;
            }

            const std::string legend =
                IndicatorLegendLabel(spec);

            if (spec.type == "SMA") {
                candidate.bindings.push_back(LineBinding(
                    spec,
                    0U,
                    "price",
                    "Price",
                    "value",
                    "SMA",
                    legend,
                    { 255, 210, 64, 255 },
                    1.5f));
            }
            else if (spec.type == "JMA") {
                candidate.bindings.push_back(LineBinding(
                    spec,
                    indicators::JmaValueOutput,
                    "price",
                    "Price",
                    "value",
                    "JMA",
                    legend,
                    { 232, 232, 238, 210 },
                    1.0f));
                candidate.bindings.push_back(LineBinding(
                    spec,
                    indicators::JmaUpOutput,
                    "price",
                    "Price",
                    "up",
                    "JMA Up",
                    legend,
                    { 58, 196, 125, 255 },
                    2.0f));
                candidate.bindings.push_back(LineBinding(
                    spec,
                    indicators::JmaDownOutput,
                    "price",
                    "Price",
                    "down",
                    "JMA Down",
                    legend,
                    { 235, 80, 92, 255 },
                    2.0f));

                const std::string paneId =
                    "indicator." + spec.id + ".slope.pane";
                const std::string slopeLegend =
                    IndicatorLegendLabel(spec, "slope");
                IndicatorOutputBinding slope = HistogramBinding(
                    spec,
                    indicators::JmaSlopeOutput,
                    paneId,
                    "JMA Slope",
                    "slope",
                    "Slope %",
                    slopeLegend,
                    { 58, 196, 125, 220 },
                    { 235, 80, 92, 220 });
                ConfigureSymmetricPane(slope);
                candidate.bindings.push_back(slope);

                IndicatorReferenceBinding zero = ReferenceBinding(
                    spec,
                    paneId,
                    "JMA Slope",
                    "indicator." + spec.id + ".slope.zero",
                    "Zero",
                    0.0);
                zero.paneHeightWeight = 0.22f;
                zero.paneValueScale =
                    render::PaneValueScale::Symmetric;
                zero.valueDecimals = 2;
                candidate.references.push_back(zero);
            }
            else if (spec.type == "VWAP") {
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
                    candidate.bindings.push_back(LineBinding(
                        spec,
                        outputs[index],
                        "price",
                        "Price",
                        suffixes[index],
                        labels[index],
                        legend,
                        colors[index],
                        index == 0U ? 1.8f : 1.0f));
                }
            }
            else if (spec.type == "OBV") {
                const std::string paneId =
                    "indicator." + spec.id + ".pane";
                IndicatorOutputBinding value = LineBinding(
                    spec,
                    indicators::ObvValueOutput,
                    paneId,
                    "OBV",
                    "value",
                    "OBV",
                    legend,
                    { 224, 224, 230, 255 },
                    1.5f);
                ConfigureObvPane(value);
                candidate.bindings.push_back(value);

                IndicatorOutputBinding signal = LineBinding(
                    spec,
                    indicators::ObvSignalOutput,
                    paneId,
                    "OBV",
                    "signal",
                    "OBV Signal",
                    legend,
                    { 255, 196, 64, 255 },
                    1.2f);
                ConfigureObvPane(signal);
                candidate.bindings.push_back(signal);

                IndicatorOutputBinding direction = HistogramBinding(
                    spec,
                    indicators::ObvDirectionOutput,
                    paneId,
                    "OBV",
                    "direction",
                    "Direction",
                    legend,
                    { 58, 196, 125, 180 },
                    { 235, 80, 92, 180 });
                ConfigureObvPane(direction);
                candidate.bindings.push_back(direction);
            }
            else if (spec.type == "ADX") {
                const std::string paneId =
                    "indicator." + spec.id + ".pane";
                IndicatorOutputBinding value = LineBinding(
                    spec,
                    indicators::AdxValueOutput,
                    paneId,
                    "ADX",
                    "value",
                    "ADX",
                    legend,
                    { 182, 120, 255, 255 },
                    1.6f);
                ConfigureAdxPane(value);
                candidate.bindings.push_back(value);

                IndicatorReferenceBinding twenty = ReferenceBinding(
                    spec,
                    paneId,
                    "ADX",
                    "indicator." + spec.id + ".reference.20",
                    "20",
                    20.0);
                ConfigureAdxReference(twenty);
                candidate.references.push_back(twenty);

                IndicatorReferenceBinding twentyFive = ReferenceBinding(
                    spec,
                    paneId,
                    "ADX",
                    "indicator." + spec.id + ".reference.25",
                    "25",
                    25.0);
                ConfigureAdxReference(twentyFive);
                candidate.references.push_back(twentyFive);
            }
            else {
                error =
                    "unsupported default indicator render type: " +
                    spec.type;
                return false;
            }
        }

        plan = std::move(candidate);
        error.clear();
        return true;
    }
}
''')

path = "ui/render_document_renderer.h"
text = read(path)
text = replace_once(
    text,
    '''#include <vector>
''',
    '''#include <string>
#include <vector>
''',
    "renderer string include")
text = replace_once(
    text,
    '''        std::vector<render::TimeBoundary> timeBoundaries;
''',
    '''        std::vector<render::TimeBoundary> timeBoundaries;
        std::string selectedOwnerId;
        std::string selectedPaneId;
        std::string selectedLegendId;
        std::string selectedLegendLabel;
        bool selectionChanged = false;
        bool selectionDoubleClicked = false;
''',
    "render selection state")
write(path, text)

path = "ui/render_document_renderer.cpp"
text = read(path)
text = replace_once(
    text,
    '''        constexpr float TimeAxisHeight = 20.0f;
        constexpr double MinimumVisibleSpan = 12.0;
''',
    '''        constexpr float TimeAxisHeight = 20.0f;
        constexpr float LegendItemHeight = 20.0f;
        constexpr float LegendSpacing = 4.0f;
        constexpr double MinimumVisibleSpan = 12.0;
''',
    "legend constants")
legend_helper = r'''
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
                draw->AddLine(
                    ImVec2(x + 5.0f, centerY),
                    ImVec2(x + 17.0f, centerY),
                    ToImColor(legend.color),
                    selected ? 3.0f : 2.0f);
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

'''
text = replace_once(
    text,
    '''        void ProcessInteraction(
''',
    legend_helper + '''        void ProcessInteraction(
''',
    "legend renderer helper")
text = replace_once(
    text,
    '''            const render::Pane& pane,
            const ValueRange& values,
            RenderSurfaceState& state)
        {
            if (!ImGui::IsItemHovered() && !ImGui::IsItemActive()) return;
''',
    '''            const render::Pane& pane,
            const ValueRange& values,
            bool legendHovered,
            RenderSurfaceState& state)
        {
            if (legendHovered) return;
            if (!ImGui::IsItemHovered() && !ImGui::IsItemActive()) return;
''',
    "legend interaction suppression")
text = replace_once(
    text,
    '''            draw->AddRect(
                plotOrigin,
                plotEnd,
                IM_COL32(70, 72, 82, 255));

            if (!values.valid || !visibleRange.valid) {
''',
    '''            draw->AddRect(
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
''',
    "draw pane legends")
text = replace_once(
    text,
    '''                pane,
                values,
                state);

            const bool paneHovered =
                ImGui::IsItemHovered() || ImGui::IsItemActive();
''',
    '''                pane,
                values,
                legendHovered,
                state);

            const bool paneHovered =
                !legendHovered &&
                (ImGui::IsItemHovered() || ImGui::IsItemActive());
''',
    "pass legend hover")
text = replace_once(
    text,
    '''            for (const render::HistogramSeries& series : pane.histograms) {
                if (!series.visible) continue;
                for (const render::HistogramPoint& point : series.points) {
''',
    '''            for (const render::HistogramSeries& series : pane.histograms) {
                if (!series.visible) continue;
                const bool selected =
                    OwnerSelected(series.ownerId, state);
                for (const render::HistogramPoint& point : series.points) {
''',
    "histogram selected state")
text = replace_once(
    text,
    '''                    draw->AddRectFilled(
                        ImVec2(
                            x - seriesBodyWidth * 0.5f,
                            (std::min)(y, zeroY)),
                        ImVec2(
                            x + seriesBodyWidth * 0.5f,
                            (std::max)(y, zeroY)),
                        ToImColor(
                            point.positive
                                ? series.positiveColor
                                : series.negativeColor));
''',
    '''                    const ImVec2 barMinimum(
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
''',
    "histogram selection outline")
text = replace_once(
    text,
    '''            for (const render::LineSeries& series : pane.lines) {
                if (!series.visible) continue;
                bool hasPrevious = false;
''',
    '''            for (const render::LineSeries& series : pane.lines) {
                if (!series.visible) continue;
                const bool selected =
                    OwnerSelected(series.ownerId, state);
                bool hasPrevious = false;
''',
    "line selected state")
text = replace_once(
    text,
    '''                            ToImColor(series.color),
                            series.width);
''',
    '''                            ToImColor(series.color),
                            series.width + (selected ? 1.5f : 0.0f));
''',
    "selected line width")
text = replace_once(
    text,
    '''            for (const render::ReferenceLine& line : pane.referenceLines) {
                if (!line.visible) continue;
                const float y = MapY(
''',
    '''            for (const render::ReferenceLine& line : pane.referenceLines) {
                if (!line.visible) continue;
                const bool selected =
                    OwnerSelected(line.ownerId, state);
                const float y = MapY(
''',
    "reference selected state")
text = replace_once(
    text,
    '''                    ToImColor(line.color),
                    line.width);
''',
    '''                    ToImColor(line.color),
                    line.width + (selected ? 1.0f : 0.0f));
''',
    "selected reference width")
text = replace_once(
    text,
    '''            for (const render::MarkerSeries& series : pane.markers) {
                if (!series.visible) continue;
                for (const render::MarkerPoint& marker : series.points) {
''',
    '''            for (const render::MarkerSeries& series : pane.markers) {
                if (!series.visible) continue;
                const bool selected =
                    OwnerSelected(series.ownerId, state);
                for (const render::MarkerPoint& marker : series.points) {
''',
    "marker selected state")
text = replace_once(
    text,
    '''                    draw->AddCircleFilled(point, 4.0f, ToImColor(marker.color));
''',
    '''                    draw->AddCircleFilled(
                        point,
                        selected ? 6.0f : 4.0f,
                        ToImColor(marker.color));
''',
    "selected marker radius")
text = replace_once(
    text,
    '''        if (document.panes.empty()) return;

        const std::uint64_t structureRevision =
''',
    '''        surfaceState.selectionChanged = false;
        surfaceState.selectionDoubleClicked = false;
        if (document.panes.empty()) return;

        const std::uint64_t structureRevision =
''',
    "selection event reset")
write(path, text)

path = "shell_main.cpp"
text = read(path)
text = replace_once(
    text,
    '''#include <memory>
#include <utility>
''',
    '''#include <memory>
#include <map>
#include <utility>
''',
    "shell map include")
text = replace_once(
    text,
    '''#include "app/default_indicator_render_plan.h"
#include "app/indicator_workspace_coordinator.h"
''',
    '''#include "app/default_indicator_render_plan.h"
#include "app/indicator_properties.h"
#include "app/indicator_workspace_coordinator.h"
''',
    "indicator properties include")
text = replace_once(
    text,
    '''static trading::app::IndicatorRenderAdapter g_indicatorRenderAdapter;
static trading::ui::RenderSurfaceState g_mainRenderSurface;
''',
    '''static trading::app::IndicatorRenderAdapter g_indicatorRenderAdapter;
static trading::ui::RenderSurfaceState g_mainRenderSurface;
static std::vector<trading::indicators::IndicatorSpec> g_indicatorSpecs;
static std::string g_selectedIndicatorId;
static std::string g_indicatorPropertyDraftId;
static std::map<std::string, double> g_indicatorPropertyDraft;
static bool g_indicatorPropertyDirty = false;
static bool g_focusIndicatorProperties = false;
static std::string g_indicatorPropertyError;
''',
    "indicator property globals")
text = replace_once(
    text,
    '''    error.clear();
    return true;
}

static const char* KiwoomSessionStateLabel(
''',
    '''    g_indicatorSpecs = specs;
    g_indicatorPropertyDraftId.clear();
    g_indicatorPropertyDraft.clear();
    g_indicatorPropertyDirty = false;
    g_indicatorPropertyError.clear();
    error.clear();
    return true;
}

static const trading::indicators::IndicatorSpec*
FindIndicatorSpecById(const std::string& indicatorId)
{
    for (const trading::indicators::IndicatorSpec& spec :
         g_indicatorSpecs)
    {
        if (spec.id == indicatorId) return &spec;
    }
    return nullptr;
}

static void ResetIndicatorPropertyDraft(
    const trading::indicators::IndicatorSpec& spec)
{
    g_indicatorPropertyDraftId = spec.id;
    g_indicatorPropertyDraft = spec.parameters;
    g_indicatorPropertyDirty = false;
    g_indicatorPropertyError.clear();
}

static bool ApplyIndicatorConfiguration(
    const std::vector<trading::indicators::IndicatorSpec>& candidate,
    std::string& error)
{
    trading::app::IndicatorRenderPlan plan;
    if (!trading::app::BuildDefaultIndicatorRenderPlan(
            candidate,
            plan,
            error))
    {
        return false;
    }

    trading::app::IndicatorRenderAdapter validationAdapter;
    if (!validationAdapter.Configure(plan, error)) {
        return false;
    }
    if (!g_indicatorModule.Configure(candidate, error)) {
        return false;
    }
    if (!g_indicatorRenderAdapter.Configure(plan, error)) {
        return false;
    }

    g_indicatorSpecs = candidate;
    g_mainRenderSurface.dirty = true;
    std::string healthError;
    g_featureRegistry.SetHealth(
        "indicators",
        true,
        {},
        healthError);
    WakeFrames(6);
    error.clear();
    return true;
}

static const char* KiwoomSessionStateLabel(
''',
    "indicator property helpers")
text = replace_once(
    text,
    '''    trading::ui::DrawRenderDocument(
        *workspace.document,
        available,
        g_mainRenderSurface);
    const std::uint64_t elapsedMicros = static_cast<std::uint64_t>(
''',
    '''    trading::ui::DrawRenderDocument(
        *workspace.document,
        available,
        g_mainRenderSurface);
    if (g_mainRenderSurface.selectionChanged) {
        const trading::indicators::IndicatorSpec* selected =
            FindIndicatorSpecById(
                g_mainRenderSurface.selectedOwnerId);
        if (selected != nullptr) {
            g_selectedIndicatorId = selected->id;
            ResetIndicatorPropertyDraft(*selected);
            if (g_mainRenderSurface.selectionDoubleClicked) {
                g_focusIndicatorProperties = true;
            }
            WakeFrames(4);
        }
    }
    const std::uint64_t elapsedMicros = static_cast<std::uint64_t>(
''',
    "legend selection handoff")
property_window = r'''
static void DrawIndicatorPropertiesWindow()
{
    if (g_focusIndicatorProperties) {
        ImGui::SetNextWindowFocus();
    }

    ImGui::Begin("프로퍼티");
    g_focusIndicatorProperties = false;

    if (g_selectedIndicatorId.empty()) {
        ImGui::TextDisabled(
            "차트 패널 좌측 상단의 지표 범례를 클릭하면 선택됩니다.");
        ImGui::TextDisabled(
            "더블클릭하면 이 프로퍼티 탭이 즉시 활성화됩니다.");
        ImGui::End();
        return;
    }

    const trading::indicators::IndicatorSpec* spec =
        FindIndicatorSpecById(g_selectedIndicatorId);
    if (spec == nullptr) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
            "선택한 지표 구성을 찾을 수 없습니다.");
        ImGui::End();
        return;
    }

    trading::app::IndicatorPropertySnapshot properties;
    std::string descriptionError;
    if (!trading::app::DescribeIndicatorProperties(
            *spec,
            properties,
            descriptionError))
    {
        ImGui::TextColored(
            ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
            "%s",
            descriptionError.c_str());
        ImGui::End();
        return;
    }

    if (g_indicatorPropertyDraftId != spec->id) {
        ResetIndicatorPropertyDraft(*spec);
    }

    ImGui::Text(
        "%s",
        trading::app::IndicatorLegendLabel(*spec).c_str());
    ImGui::TextDisabled(
        "ID: %s  Type: %s",
        spec->id.c_str(),
        spec->type.c_str());
    ImGui::Separator();

    if (ImGui::BeginTable(
            "indicator_property_grid",
            2,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("속성");
        ImGui::TableSetupColumn("값");
        ImGui::TableHeadersRow();

        for (const trading::app::IndicatorParameterDescriptor& descriptor :
             properties.parameters)
        {
            ImGui::TableNextRow();
            ImGui::PushID(descriptor.key.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(descriptor.displayName.c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);

            double& draft =
                g_indicatorPropertyDraft[descriptor.key];
            bool changed = false;
            if (
                descriptor.kind ==
                trading::app::IndicatorParameterKind::Integer)
            {
                int value = static_cast<int>(std::llround(draft));
                const int step =
                    static_cast<int>(std::llround(descriptor.step));
                const int fastStep =
                    static_cast<int>(std::llround(descriptor.fastStep));
                if (ImGui::InputInt(
                        "##value",
                        &value,
                        step,
                        fastStep))
                {
                    draft = static_cast<double>(value);
                    changed = true;
                }
            }
            else {
                double value = draft;
                if (ImGui::InputDouble(
                        "##value",
                        &value,
                        descriptor.step,
                        descriptor.fastStep,
                        "%.4f"))
                {
                    draft = value;
                    changed = true;
                }
            }
            if (changed) {
                g_indicatorPropertyDirty = true;
                g_indicatorPropertyError.clear();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (!g_indicatorPropertyDirty) ImGui::BeginDisabled();
    if (ImGui::Button("적용")) {
        const std::string selectedId = spec->id;
        std::vector<trading::indicators::IndicatorSpec> candidate =
            g_indicatorSpecs;
        std::string error;
        bool valid = true;
        for (const trading::app::IndicatorParameterDescriptor& descriptor :
             properties.parameters)
        {
            const auto found =
                g_indicatorPropertyDraft.find(descriptor.key);
            if (
                found == g_indicatorPropertyDraft.end() ||
                !trading::app::UpdateIndicatorParameter(
                    candidate,
                    selectedId,
                    descriptor.key,
                    found->second,
                    error))
            {
                valid = false;
                if (error.empty()) {
                    error =
                        "프로퍼티 초안 값이 없습니다: " +
                        descriptor.key;
                }
                break;
            }
        }

        if (valid && ApplyIndicatorConfiguration(candidate, error)) {
            const trading::indicators::IndicatorSpec* updated =
                FindIndicatorSpecById(selectedId);
            if (updated != nullptr) {
                ResetIndicatorPropertyDraft(*updated);
            }
            g_log.Add(
                "FEATURE",
                "지표 파라미터 적용: %s",
                selectedId.c_str());
        }
        else {
            g_indicatorPropertyError = error;
            g_log.Add(
                "REJECT",
                "지표 파라미터 적용 거부 %s: %s",
                selectedId.c_str(),
                error.c_str());
        }
    }
    if (!g_indicatorPropertyDirty) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("되돌리기")) {
        ResetIndicatorPropertyDraft(*spec);
    }

    if (!g_indicatorPropertyError.empty()) {
        ImGui::Separator();
        ImGui::TextColored(
            ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
            "%s",
            g_indicatorPropertyError.c_str());
    }

    ImGui::End();
}

'''
text = replace_once(
    text,
    '''static void DrawDashboard()
''',
    property_window + '''static void DrawDashboard()
''',
    "indicator property window")
text = replace_once(
    text,
    '''    ImGui::DockBuilderDockWindow("기능/성능", right);
''',
    '''    ImGui::DockBuilderDockWindow("기능/성능", right);
    ImGui::DockBuilderDockWindow("프로퍼티", right);
''',
    "dock property window")
text = replace_once(
    text,
    '''        DrawScanner();
        DrawDashboard();
''',
    '''        DrawScanner();
        DrawIndicatorPropertiesWindow();
        DrawDashboard();
''',
    "draw property window")
write(path, text)

write("tests/indicator_properties_tests.cpp", r'''#include "../app/indicator_properties.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::exit(1);
    }

    void Check(bool condition, const char* message)
    {
        if (!condition) Fail(message);
    }

    trading::indicators::IndicatorSpec Spec(
        const char* id,
        const char* type)
    {
        trading::indicators::IndicatorSpec spec;
        spec.id = id;
        spec.type = type;
        return spec;
    }
}

int main()
{
    using namespace trading;
    using namespace trading::app;

    indicators::IndicatorSpec sma = Spec("sma.20", "SMA");
    sma.parameters.emplace("period", 20.0);

    IndicatorPropertySnapshot properties;
    std::string error;
    Check(DescribeIndicatorProperties(sma, properties, error),
          "SMA properties must be described");
    Check(properties.parameters.size() == 1U,
          "SMA property count mismatch");
    Check(properties.parameters[0].kind ==
              IndicatorParameterKind::Integer,
          "SMA period must be integer");
    Check(IndicatorLegendLabel(sma) == "SMA 20",
          "SMA legend label mismatch");

    indicators::IndicatorSpec jma = Spec("jma.20", "JMA");
    jma.parameters.emplace("period", 20.0);
    jma.parameters.emplace("phase", 0.0);
    jma.parameters.emplace("power", 2.0);
    Check(IndicatorLegendLabel(jma) == "JMA 20 P0 Pow2",
          "JMA legend label mismatch");
    Check(IndicatorLegendLabel(jma, "slope") == "JMA Slope 20",
          "JMA slope legend label mismatch");

    indicators::IndicatorSpec vwap = Spec("vwap.session", "VWAP");
    vwap.parameters.emplace("std_dev_1", 1.0);
    vwap.parameters.emplace("std_dev_2", 2.0);
    Check(IndicatorLegendLabel(vwap) == "VWAP 1/2",
          "VWAP legend label mismatch");

    indicators::IndicatorSpec obv = Spec("obv.20", "OBV");
    obv.parameters.emplace("signal_period", 20.0);
    Check(IndicatorLegendLabel(obv) == "OBV Signal 20",
          "OBV legend label mismatch");

    indicators::IndicatorSpec adx = Spec("adx.14", "ADX");
    adx.parameters.emplace("period", 14.0);
    Check(IndicatorLegendLabel(adx) == "ADX 14",
          "ADX legend label mismatch");

    std::vector<indicators::IndicatorSpec> specs = {
        sma, jma, vwap, obv, adx
    };
    Check(UpdateIndicatorParameter(
              specs, "sma.20", "period", 25.0, error),
          "valid SMA period update must succeed");
    Check(specs[0].parameters["period"] == 25.0,
          "SMA period update was not stored");
    Check(!UpdateIndicatorParameter(
              specs, "sma.20", "period", 25.5, error),
          "fractional integer parameter must fail");
    Check(!UpdateIndicatorParameter(
              specs, "jma.20", "phase", 101.0, error),
          "out-of-range parameter must fail");
    Check(!UpdateIndicatorParameter(
              specs, "missing", "period", 10.0, error),
          "missing indicator target must fail");
    Check(!UpdateIndicatorParameter(
              specs, "adx.14", "unknown", 10.0, error),
          "unknown parameter key must fail");

    std::puts("[PASS] indicator_properties_tests");
    return 0;
}
''')

path = "tests/indicator_render_adapter_tests.cpp"
text = read(path)
text = replace_once(
    text,
    '''        up.label = "JMA Up";
        up.width = 1.5f;
''',
    '''        up.label = "JMA Up";
        up.width = 1.5f;
        up.legendLabel = "JMA 20";
''',
    "adapter test price legend")
text = replace_once(
    text,
    '''        slope.label = "Slope";
        plan.bindings.push_back(slope);
''',
    '''        slope.label = "Slope";
        slope.legendLabel = "JMA Slope 20";
        plan.bindings.push_back(slope);
''',
    "adapter test slope legend")
text = replace_once(
    text,
    '''        zero.width = 1.0f;
        plan.references.push_back(zero);
''',
    '''        zero.width = 1.0f;
        zero.indicatorId = "jma.main";
        plan.references.push_back(zero);
''',
    "adapter test reference owner")
text = replace_once(
    text,
    '''    Check(first.panes[0].lines.size() == 2U,
          "NaN line gap must split into two finite line segments");
''',
    '''    Check(first.panes[0].legends.size() == 1U,
          "price pane must contain one grouped JMA legend");
    Check(first.panes[0].legends[0].ownerId == "jma.main",
          "price legend owner mismatch");
    Check(first.panes[0].legends[0].label == "JMA 20",
          "price legend label mismatch");
    Check(first.panes[0].lines.size() == 2U,
          "NaN line gap must split into two finite line segments");
''',
    "adapter test price legend assertions")
text = replace_once(
    text,
    '''    Check(first.panes[1].histograms.size() == 1U,
          "indicator histogram contribution count mismatch");
''',
    '''    Check(first.panes[0].lines[0].ownerId == "jma.main",
          "line series owner mismatch");
    Check(first.panes[1].legends.size() == 1U,
          "slope pane must contain one grouped JMA legend");
    Check(first.panes[1].legends[0].label == "JMA Slope 20",
          "slope legend label mismatch");
    Check(first.panes[1].histograms.size() == 1U,
          "indicator histogram contribution count mismatch");
''',
    "adapter test slope legend assertions")
text = replace_once(
    text,
    '''    Check(first.panes[1].referenceLines[0].value == 0.0,
          "indicator reference-line value mismatch");
''',
    '''    Check(first.panes[1].referenceLines[0].value == 0.0,
          "indicator reference-line value mismatch");
    Check(first.panes[1].referenceLines[0].ownerId == "jma.main",
          "indicator reference-line owner mismatch");
''',
    "adapter test reference owner assertion")
write(path, text)

path = "tests/render_document_tests.cpp"
text = read(path)
text = replace_once(
    text,
    '''        trading::render::CandleSeries candles;
''',
    '''        trading::render::LegendEntry legend;
        legend.id = "legend.sma.5";
        legend.ownerId = "sma.5";
        legend.label = "SMA 5";
        pricePane.legends.push_back(legend);

        trading::render::CandleSeries candles;
''',
    "valid render legend")
text = replace_once(
    text,
    '''        Check(!trading::render::ValidateRenderDocument(unordered, error),
              "unordered candle timestamps must fail");
''',
    '''        Check(!trading::render::ValidateRenderDocument(unordered, error),
              "unordered candle timestamps must fail");

        trading::render::RenderDocument invalidLegend;
        invalidLegend.workspaceId = "main";
        trading::render::Pane legendPane;
        legendPane.id = "price";
        trading::render::LegendEntry selectable;
        selectable.id = "legend.invalid";
        selectable.label = "Invalid";
        selectable.selectable = true;
        legendPane.legends.push_back(selectable);
        invalidLegend.panes.push_back(legendPane);
        Check(!trading::render::ValidateRenderDocument(
                  invalidLegend, error),
              "selectable legend without owner must fail");
''',
    "invalid render legend")
write(path, text)

path = "tests/run_all.bat"
text = read(path)
text = replace_once(
    text,
    '''call :build_and_run default_indicator_render_plan_tests.exe "tests\\default_indicator_render_plan_tests.cpp app\\default_indicator_render_plan.cpp app\\indicator_render_adapter.cpp render\\render_document.cpp"
if errorlevel 1 exit /b 1
''',
    '''call :build_and_run default_indicator_render_plan_tests.exe "tests\\default_indicator_render_plan_tests.cpp app\\default_indicator_render_plan.cpp app\\indicator_properties.cpp app\\indicator_render_adapter.cpp render\\render_document.cpp"
if errorlevel 1 exit /b 1

call :build_and_run indicator_properties_tests.exe "tests\\indicator_properties_tests.cpp app\\indicator_properties.cpp"
if errorlevel 1 exit /b 1
''',
    "property tests in full suite")
write(path, text)

path = "build.bat"
text = read(path)
text = replace_once(
    text,
    '''   app\\default_indicator_render_plan.cpp ^
   app\\indicator_workspace_coordinator.cpp ^
''',
    '''   app\\default_indicator_render_plan.cpp ^
   app\\indicator_properties.cpp ^
   app\\indicator_workspace_coordinator.cpp ^
''',
    "property source in shell build")
write(path, text)

path = ".github/workflows/windows-ci.yml"
text = read(path)
text = replace_once(
    text,
    '''permissions:
  contents: write
''',
    '''permissions:
  contents: read
''',
    "restore workflow permissions")
begin = text.index("      # BEGIN TEMP INDICATOR LEGEND APPLY\n")
end = text.index("      # END TEMP INDICATOR LEGEND APPLY\n", begin)
end += len("      # END TEMP INDICATOR LEGEND APPLY\n")
text = text[:begin] + text[end:]
text = replace_once(
    text,
    '''            'IndicatorRenderAdapter g_indicatorRenderAdapter',
''',
    '''            'IndicatorRenderAdapter g_indicatorRenderAdapter',
            'DrawIndicatorPropertiesWindow',
            'g_indicatorSpecs',
            'selectionDoubleClicked',
''',
    "shell property CI markers")
text = replace_once(
    text,
    '''            'HasLiveTail()',
            'ValueGrid cursorGrid')) {
''',
    '''            'HasLiveTail()',
            'ValueGrid cursorGrid',
            'struct LegendEntry final',
            'std::vector<LegendEntry> legends')) {
''',
    "render legend contract CI markers")
text = replace_once(
    text,
    '''            'QuantizeValue(',
            'SeriesBodyWidth(')) {
''',
    '''            'QuantizeValue(',
            'SeriesBodyWidth(',
            'DrawPaneLegends',
            'selectedOwnerId',
            'selectionDoubleClicked')) {
''',
    "renderer legend CI markers")
text = replace_once(
    text,
    '''            'app\\default_indicator_render_plan.cpp',
            'app\\indicator_workspace_coordinator.cpp')) {
''',
    '''            'app\\default_indicator_render_plan.cpp',
            'app\\indicator_properties.cpp',
            'app\\indicator_workspace_coordinator.cpp')) {
''',
    "property build CI marker")
text = replace_once(
    text,
    '''          if (-not $runAll.Contains('chart_viewport_tests.exe') -or
''',
    '''          if (-not $runAll.Contains('indicator_properties_tests.exe')) {
            throw 'indicator property tests must be in the full suite'
          }
          if (-not $runAll.Contains('chart_viewport_tests.exe') -or
''',
    "property test CI marker")
write(path, text)

Path(__file__).unlink()
print("indicator legends and properties applied")
