#include "indicator_configuration.h"

#include "indicator_properties.h"
#include "../core/adx_indicator.h"
#include "../core/jma_indicator.h"
#include "../core/obv_indicator.h"
#include "../core/standard_indicators.h"
#include "../core/vwap_indicator.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

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
            const std::string& legendRole,
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
            binding.legendRole = legendRole;
            binding.primaryColor = color;
            binding.width = width;
            binding.style = render::LineStyle::Solid;
            return binding;
        }

        IndicatorOutputBinding HistogramBinding(
            const indicators::IndicatorSpec& spec,
            std::size_t outputIndex,
            const std::string& paneId,
            const std::string& paneTitle,
            const std::string& suffix,
            const std::string& label,
            const std::string& legendRole,
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
            binding.legendRole = legendRole;
            binding.primaryColor = positive;
            binding.secondaryColor = negative;
            return binding;
        }

        IndicatorReferenceBinding ReferenceBinding(
            const indicators::IndicatorSpec& spec,
            const std::string& paneId,
            const std::string& paneTitle,
            const std::string& suffix,
            const std::string& label,
            double value)
        {
            IndicatorReferenceBinding binding;
            binding.paneId = paneId;
            binding.paneTitle = paneTitle;
            binding.referenceId =
                "indicator." + spec.id + "." + suffix;
            binding.label = label;
            binding.value = value;
            binding.color = { 170, 174, 188, 190 };
            binding.width = 1.0f;
            binding.style = render::LineStyle::Dashed;
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

        bool HasVisibleContribution(
            const IndicatorInstanceDefinition& definition) noexcept
        {
            if (!definition.visible) return false;
            for (const IndicatorOutputBinding& output : definition.outputs) {
                if (output.visible) return true;
            }
            for (const IndicatorReferenceBinding& reference :
                 definition.references)
            {
                if (reference.visible) return true;
            }
            return false;
        }

        std::string LowerAscii(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        void ReplaceAll(
            std::string& value,
            const std::string& from,
            const std::string& to)
        {
            if (from.empty() || from == to) return;
            std::size_t position = 0;
            while ((position = value.find(from, position)) !=
                   std::string::npos)
            {
                value.replace(position, from.size(), to);
                position += to.size();
            }
        }

        render::ColorRgba WithAlpha(
            render::ColorRgba color,
            std::uint8_t alpha) noexcept
        {
            color.alpha = alpha;
            return color;
        }
    }

    const std::vector<IndicatorCatalogEntry>&
    IndicatorCatalog() noexcept
    {
        static const std::vector<IndicatorCatalogEntry> catalog = {
            { "SMA", "SMA 단순이동평균" },
            { "EMA", "EMA 지수이동평균" },
            { "BOLLINGER", "Bollinger Bands" },
            { "RSI", "RSI 상대강도" },
            { "MACD", "MACD 추세모멘텀" },
            { "DMI", "DMI +DI/-DI/ADX" },
            { "SUPERTREND", "SuperTrend" },
            { "JMA", "JMA 적응형 이동평균" },
            { "VWAP", "VWAP 세션 밴드" },
            { "OBV", "OBV 거래량 흐름" },
            { "ADX", "ADX 추세 강도" }
        };
        return catalog;
    }

    bool CreateIndicatorDefinition(
        const indicators::IndicatorSpec& spec,
        IndicatorInstanceDefinition& definition,
        std::string& error)
    {
        IndicatorPropertySnapshot properties;
        if (!DescribeIndicatorProperties(spec, properties, error)) {
            return false;
        }

        IndicatorInstanceDefinition candidate;
        candidate.spec = spec;

        if (spec.type == "SMA") {
            candidate.outputs.push_back(LineBinding(
                spec,
                0U,
                "price",
                "Price",
                "value",
                "SMA",
                {},
                { 255, 210, 64, 255 },
                1.5f));
        }
        else if (spec.type == "EMA") {
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
        else if (spec.type == "JMA") {
            candidate.outputs.push_back(LineBinding(
                spec,
                indicators::JmaValueOutput,
                "price",
                "Price",
                "value",
                "JMA",
                {},
                { 232, 232, 238, 210 },
                1.0f));
            candidate.outputs.push_back(LineBinding(
                spec,
                indicators::JmaUpOutput,
                "price",
                "Price",
                "up",
                "JMA Up",
                {},
                { 58, 196, 125, 255 },
                2.0f));
            candidate.outputs.push_back(LineBinding(
                spec,
                indicators::JmaDownOutput,
                "price",
                "Price",
                "down",
                "JMA Down",
                {},
                { 235, 80, 92, 255 },
                2.0f));

            const std::string paneId =
                "indicator." + spec.id + ".slope.pane";
            IndicatorOutputBinding slope = HistogramBinding(
                spec,
                indicators::JmaSlopeOutput,
                paneId,
                "JMA Slope",
                "slope",
                "Slope %",
                "slope",
                { 58, 196, 125, 220 },
                { 235, 80, 92, 220 });
            ConfigureSymmetricPane(slope);
            candidate.outputs.push_back(slope);

            IndicatorReferenceBinding zero = ReferenceBinding(
                spec,
                paneId,
                "JMA Slope",
                "slope.zero",
                "Zero",
                0.0);
            zero.paneHeightWeight = 0.22f;
            zero.paneValueScale = render::PaneValueScale::Symmetric;
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
                candidate.outputs.push_back(LineBinding(
                    spec,
                    outputs[index],
                    "price",
                    "Price",
                    suffixes[index],
                    labels[index],
                    {},
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
                {},
                { 224, 224, 230, 255 },
                1.5f);
            ConfigureObvPane(value);
            candidate.outputs.push_back(value);

            IndicatorOutputBinding signal = LineBinding(
                spec,
                indicators::ObvSignalOutput,
                paneId,
                "OBV",
                "signal",
                "OBV Signal",
                {},
                { 255, 196, 64, 255 },
                1.2f);
            ConfigureObvPane(signal);
            candidate.outputs.push_back(signal);

            IndicatorOutputBinding direction = HistogramBinding(
                spec,
                indicators::ObvDirectionOutput,
                paneId,
                "OBV",
                "direction",
                "Direction",
                {},
                { 58, 196, 125, 180 },
                { 235, 80, 92, 180 });
            ConfigureObvPane(direction);
            candidate.outputs.push_back(direction);
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
                {},
                { 182, 120, 255, 255 },
                1.6f);
            ConfigureAdxPane(value);
            candidate.outputs.push_back(value);

            IndicatorReferenceBinding twenty = ReferenceBinding(
                spec,
                paneId,
                "ADX",
                "reference.20",
                "20",
                20.0);
            ConfigureAdxReference(twenty);
            candidate.references.push_back(twenty);

            IndicatorReferenceBinding twentyFive = ReferenceBinding(
                spec,
                paneId,
                "ADX",
                "reference.25",
                "25",
                25.0);
            ConfigureAdxReference(twentyFive);
            candidate.references.push_back(twentyFive);
        }
        else {
            error =
                "unsupported indicator configuration type: " +
                spec.type;
            return false;
        }

        definition = std::move(candidate);
        error.clear();
        return true;
    }

    bool CreateDefaultIndicatorDefinition(
        const std::string& type,
        const std::string& id,
        IndicatorInstanceDefinition& definition,
        std::string& error)
    {
        indicators::IndicatorSpec spec;
        spec.id = id;
        spec.type = type;

        if (type == "SMA") {
            spec.parameters.emplace("period", 20.0);
        }
        else if (type == "EMA") {
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
        else if (type == "JMA") {
            spec.parameters.emplace("period", 20.0);
            spec.parameters.emplace("phase", 0.0);
            spec.parameters.emplace("power", 2.0);
        }
        else if (type == "VWAP") {
            spec.parameters.emplace("std_dev_1", 1.0);
            spec.parameters.emplace("std_dev_2", 2.0);
        }
        else if (type == "OBV") {
            spec.parameters.emplace("signal_period", 20.0);
        }
        else if (type == "ADX") {
            spec.parameters.emplace("period", 14.0);
        }
        else {
            error = "unsupported indicator catalog type: " + type;
            return false;
        }

        return CreateIndicatorDefinition(spec, definition, error);
    }

    std::vector<IndicatorInstanceDefinition>
    InitialIndicatorDefinitions()
    {
        std::vector<IndicatorInstanceDefinition> definitions;
        const struct Initial final
        {
            const char* type;
            const char* id;
        } initial[] = {
            { "SMA", "sma.20" },
            { "JMA", "jma.20" },
            { "VWAP", "vwap.session" },
            { "OBV", "obv.20" },
            { "ADX", "adx.14" }
        };

        for (const Initial& item : initial) {
            IndicatorInstanceDefinition definition;
            std::string error;
            if (CreateDefaultIndicatorDefinition(
                    item.type,
                    item.id,
                    definition,
                    error))
            {
                definitions.push_back(std::move(definition));
            }
        }
        return definitions;
    }

    std::vector<indicators::IndicatorSpec>
    VisibleIndicatorSpecs(
        const std::vector<IndicatorInstanceDefinition>& definitions)
    {
        std::vector<indicators::IndicatorSpec> specs;
        for (const IndicatorInstanceDefinition& definition : definitions) {
            if (HasVisibleContribution(definition)) {
                specs.push_back(definition.spec);
            }
        }
        return specs;
    }

    bool BuildIndicatorRenderPlan(
        const std::vector<IndicatorInstanceDefinition>& definitions,
        IndicatorRenderPlan& plan,
        std::string& error)
    {
        IndicatorRenderPlan candidate;
        std::set<std::string> instanceIds;
        std::set<std::string> renderIds;

        for (const IndicatorInstanceDefinition& definition : definitions) {
            if (definition.spec.id.empty()) {
                error = "indicator definition id is empty";
                return false;
            }
            if (!instanceIds.insert(definition.spec.id).second) {
                error =
                    "duplicate indicator definition id: " +
                    definition.spec.id;
                return false;
            }
            if (!definition.visible) continue;

            for (const IndicatorOutputBinding& source : definition.outputs) {
                if (!source.visible) continue;
                IndicatorOutputBinding binding = source;
                binding.indicatorId = definition.spec.id;
                binding.legendLabel = IndicatorLegendLabel(
                    definition.spec,
                    binding.legendRole);
                if (binding.seriesId.empty() ||
                    !renderIds.insert(binding.seriesId).second)
                {
                    error =
                        "duplicate or empty indicator output id: " +
                        binding.seriesId;
                    return false;
                }
                candidate.bindings.push_back(std::move(binding));
            }

            for (const IndicatorReferenceBinding& source :
                 definition.references)
            {
                if (!source.visible) continue;
                IndicatorReferenceBinding reference = source;
                reference.indicatorId = definition.spec.id;
                if (reference.referenceId.empty() ||
                    !renderIds.insert(reference.referenceId).second)
                {
                    error =
                        "duplicate or empty indicator reference id: " +
                        reference.referenceId;
                    return false;
                }
                candidate.references.push_back(std::move(reference));
            }
        }

        plan = std::move(candidate);
        error.clear();
        return true;
    }

    const IndicatorInstanceDefinition* FindIndicatorDefinition(
        const std::vector<IndicatorInstanceDefinition>& definitions,
        const std::string& id) noexcept
    {
        for (const IndicatorInstanceDefinition& definition : definitions) {
            if (definition.spec.id == id) return &definition;
        }
        return nullptr;
    }

    IndicatorInstanceDefinition* FindIndicatorDefinition(
        std::vector<IndicatorInstanceDefinition>& definitions,
        const std::string& id) noexcept
    {
        for (IndicatorInstanceDefinition& definition : definitions) {
            if (definition.spec.id == id) return &definition;
        }
        return nullptr;
    }

    std::string NextIndicatorInstanceId(
        const std::string& type,
        const std::vector<IndicatorInstanceDefinition>& definitions)
    {
        const std::string base = LowerAscii(type.empty() ? "indicator" : type);
        for (std::size_t index = 1; index < 100000U; ++index) {
            const std::string candidate =
                base + "." + std::to_string(index);
            if (FindIndicatorDefinition(definitions, candidate) == nullptr) {
                return candidate;
            }
        }
        return base + ".overflow";
    }

    bool DuplicateIndicatorDefinition(
        const IndicatorInstanceDefinition& source,
        const std::string& newId,
        std::size_t colorVariant,
        IndicatorInstanceDefinition& duplicate,
        std::string& error)
    {
        if (source.spec.id.empty() || newId.empty()) {
            error = "indicator duplicate id is empty";
            return false;
        }

        duplicate = source;
        const std::string oldId = duplicate.spec.id;
        duplicate.spec.id = newId;
        for (IndicatorOutputBinding& output : duplicate.outputs) {
            output.indicatorId = newId;
            ReplaceAll(output.seriesId, oldId, newId);
        }
        for (IndicatorReferenceBinding& reference : duplicate.references) {
            reference.indicatorId = newId;
            ReplaceAll(reference.referenceId, oldId, newId);
        }
        ApplyIndicatorColorVariant(duplicate, colorVariant);
        error.clear();
        return true;
    }

    void ApplyIndicatorColorVariant(
        IndicatorInstanceDefinition& definition,
        std::size_t colorVariant) noexcept
    {
        static constexpr render::ColorRgba palette[] = {
            { 255, 196, 64, 255 },
            { 64, 210, 225, 255 },
            { 235, 92, 188, 255 },
            { 96, 214, 126, 255 },
            { 255, 132, 72, 255 },
            { 122, 152, 255, 255 },
            { 210, 110, 255, 255 },
            { 238, 238, 238, 255 }
        };
        constexpr std::size_t count = sizeof(palette) / sizeof(palette[0]);

        for (std::size_t index = 0; index < definition.outputs.size(); ++index) {
            IndicatorOutputBinding& output = definition.outputs[index];
            output.primaryColor = palette[(colorVariant + index) % count];
            if (output.kind == IndicatorRenderKind::Histogram) {
                output.primaryColor = WithAlpha(output.primaryColor, 220);
                output.secondaryColor = WithAlpha(
                    palette[(colorVariant + index + count / 2U) % count],
                    220);
            }
        }
        for (std::size_t index = 0; index < definition.references.size(); ++index) {
            definition.references[index].color = WithAlpha(
                palette[(colorVariant + index) % count],
                190);
        }
    }

    bool MoveIndicatorToPane(
        IndicatorInstanceDefinition& definition,
        const std::string& paneId,
        const std::string& paneTitle,
        std::string& error)
    {
        if (paneId.empty()) {
            error = "indicator target pane id is empty";
            return false;
        }
        const std::string title = paneTitle.empty()
            ? paneId
            : paneTitle;
        for (IndicatorOutputBinding& output : definition.outputs) {
            output.paneId = paneId;
            output.paneTitle = title;
        }
        for (IndicatorReferenceBinding& reference : definition.references) {
            reference.paneId = paneId;
            reference.paneTitle = title;
        }
        error.clear();
        return true;
    }
}
