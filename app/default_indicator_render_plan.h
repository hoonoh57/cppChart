#pragma once

#include "indicator_render_adapter.h"

#include <string>
#include <vector>

namespace trading::app
{
    bool BuildDefaultIndicatorRenderPlan(
        const std::vector<indicators::IndicatorSpec>& specs,
        IndicatorRenderPlan& plan,
        std::string& error);
}
