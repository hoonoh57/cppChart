#pragma once

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
