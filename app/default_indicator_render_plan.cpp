#include "default_indicator_render_plan.h"

#include "indicator_configuration.h"

namespace trading::app
{
    bool BuildDefaultIndicatorRenderPlan(
        const std::vector<indicators::IndicatorSpec>& specs,
        IndicatorRenderPlan& plan,
        std::string& error)
    {
        std::vector<IndicatorInstanceDefinition> definitions;
        definitions.reserve(specs.size());
        for (const indicators::IndicatorSpec& spec : specs) {
            IndicatorInstanceDefinition definition;
            if (!CreateIndicatorDefinition(spec, definition, error)) {
                return false;
            }
            definitions.push_back(std::move(definition));
        }
        return BuildIndicatorRenderPlan(definitions, plan, error);
    }
}
