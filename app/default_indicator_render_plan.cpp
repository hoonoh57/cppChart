#include "default_indicator_render_plan.h"

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
            binding.primaryColor = positive;
            binding.secondaryColor = negative;
            return binding;
        }

        IndicatorReferenceBinding ReferenceBinding(
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
                error = "duplicate default indicator render-plan id: " + spec.id;
                return false;
            }

            if (spec.type == "SMA") {
                candidate.bindings.push_back(LineBinding(
                    spec,
                    0U,
                    "price",
                    "Price",
                    "value",
                    "SMA",
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
                    { 232, 232, 238, 210 },
                    1.0f));
                candidate.bindings.push_back(LineBinding(
                    spec,
                    indicators::JmaUpOutput,
                    "price",
                    "Price",
                    "up",
                    "JMA Up",
                    { 58, 196, 125, 255 },
                    2.0f));
                candidate.bindings.push_back(LineBinding(
                    spec,
                    indicators::JmaDownOutput,
                    "price",
                    "Price",
                    "down",
                    "JMA Down",
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
                    { 58, 196, 125, 220 },
                    { 235, 80, 92, 220 });
                ConfigureSymmetricPane(slope);
                candidate.bindings.push_back(slope);

                IndicatorReferenceBinding zero = ReferenceBinding(
                    paneId,
                    "JMA Slope",
                    "indicator." + spec.id + ".slope.zero",
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
                    candidate.bindings.push_back(LineBinding(
                        spec,
                        outputs[index],
                        "price",
                        "Price",
                        suffixes[index],
                        labels[index],
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
                    { 182, 120, 255, 255 },
                    1.6f);
                ConfigureAdxPane(value);
                candidate.bindings.push_back(value);

                IndicatorReferenceBinding twenty = ReferenceBinding(
                    paneId,
                    "ADX",
                    "indicator." + spec.id + ".reference.20",
                    "20",
                    20.0);
                ConfigureAdxReference(twenty);
                candidate.references.push_back(twenty);

                IndicatorReferenceBinding twentyFive = ReferenceBinding(
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
