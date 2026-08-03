#include "indicator_properties.h"

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
