#include "../app/indicator_configuration.h"

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

    const trading::app::IndicatorOutputBinding* FindOutput(
        const trading::app::IndicatorRenderPlan& plan,
        const std::string& id)
    {
        for (const auto& output : plan.bindings) {
            if (output.seriesId == id) return &output;
        }
        return nullptr;
    }
}

int main()
{
    using namespace trading::app;

    std::vector<IndicatorInstanceDefinition> definitions =
        InitialIndicatorDefinitions();
    Check(definitions.size() == 5U,
          "initial indicator definition count mismatch");
    Check(IndicatorCatalog().size() == 5U,
          "indicator catalog count mismatch");
    Check(VisibleIndicatorSpecs(definitions).size() == 5U,
          "initial visible indicator spec count mismatch");

    IndicatorRenderPlan plan;
    std::string error;
    Check(BuildIndicatorRenderPlan(definitions, plan, error),
          "initial dynamic render plan must build");
    Check(plan.bindings.size() == 14U,
          "initial dynamic output count mismatch");
    Check(plan.references.size() == 3U,
          "initial dynamic reference count mismatch");

    IndicatorInstanceDefinition* jma =
        FindIndicatorDefinition(definitions, "jma.20");
    Check(jma != nullptr, "initial JMA definition missing");
    jma->visible = false;
    Check(VisibleIndicatorSpecs(definitions).size() == 4U,
          "hidden indicator must stop calculation");
    Check(BuildIndicatorRenderPlan(definitions, plan, error),
          "hidden-indicator plan must build");
    Check(FindOutput(plan, "indicator.jma.20.value") == nullptr,
          "hidden indicator must not contribute render output");

    jma->visible = true;
    const std::string duplicateId =
        NextIndicatorInstanceId("JMA", definitions);
    IndicatorInstanceDefinition duplicate;
    Check(DuplicateIndicatorDefinition(
              *jma,
              duplicateId,
              2U,
              duplicate,
              error),
          "JMA duplicate must build");
    Check(duplicate.spec.id != jma->spec.id,
          "duplicate indicator id must be unique");
    Check(duplicate.outputs.front().paneId ==
              jma->outputs.front().paneId,
          "duplicate must preserve source pane placement");
    Check(duplicate.outputs.front().primaryColor.red !=
              jma->outputs.front().primaryColor.red ||
              duplicate.outputs.front().primaryColor.green !=
              jma->outputs.front().primaryColor.green ||
              duplicate.outputs.front().primaryColor.blue !=
              jma->outputs.front().primaryColor.blue,
          "duplicate must receive a distinguishable color variant");
    definitions.push_back(duplicate);

    Check(BuildIndicatorRenderPlan(definitions, plan, error),
          "multi-instance indicator plan must build");
    Check(FindOutput(
              plan,
              "indicator." + duplicateId + ".value") != nullptr,
          "duplicated indicator output is missing");

    IndicatorRenderAdapter adapter;
    Check(adapter.Configure(plan, error),
          "multi-instance render plan must satisfy adapter contract");

    IndicatorInstanceDefinition* duplicated =
        FindIndicatorDefinition(definitions, duplicateId);
    Check(duplicated != nullptr,
          "duplicated indicator definition missing");
    Check(MoveIndicatorToPane(
              *duplicated,
              "indicator.shared.overlay",
              "Shared overlay",
              error),
          "indicator pane remap must succeed");
    for (const auto& output : duplicated->outputs) {
        Check(output.paneId == "indicator.shared.overlay",
              "all duplicated outputs must share remapped pane");
    }
    for (const auto& reference : duplicated->references) {
        Check(reference.paneId == "indicator.shared.overlay",
              "all duplicated references must share remapped pane");
    }

    duplicated->outputs.front().visible = false;
    duplicated->references.clear();
    Check(BuildIndicatorRenderPlan(definitions, plan, error),
          "partially hidden output plan must build");
    Check(FindOutput(
              plan,
              "indicator." + duplicateId + ".value") == nullptr,
          "hidden output must not contribute a render series");

    std::vector<IndicatorInstanceDefinition> duplicateIds = definitions;
    duplicateIds.back().spec.id = duplicateIds.front().spec.id;
    Check(!BuildIndicatorRenderPlan(duplicateIds, plan, error),
          "duplicate instance ids must fail closed");

    std::puts("[PASS] indicator_configuration_tests");
    return 0;
}
