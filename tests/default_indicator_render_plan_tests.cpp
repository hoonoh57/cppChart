#include "../app/default_indicator_render_plan.h"

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
        const std::string& id,
        const std::string& type)
    {
        trading::indicators::IndicatorSpec spec;
        spec.id = id;
        spec.type = type;
        return spec;
    }

    std::vector<trading::indicators::IndicatorSpec> InitialSpecs()
    {
        auto sma = Spec("sma.fast", "SMA");
        sma.parameters.emplace("period", 20.0);

        auto jma = Spec("jma.main", "JMA");
        jma.parameters.emplace("period", 20.0);
        jma.parameters.emplace("phase", 0.0);
        jma.parameters.emplace("power", 2.0);

        auto vwap = Spec("vwap.session", "VWAP");
        vwap.parameters.emplace("std_dev_1", 1.0);
        vwap.parameters.emplace("std_dev_2", 2.0);

        auto obv = Spec("obv.flow", "OBV");
        obv.parameters.emplace("signal_period", 20.0);

        auto adx = Spec("adx.trend", "ADX");
        adx.parameters.emplace("period", 14.0);

        return { sma, jma, vwap, obv, adx };
    }

    const trading::app::IndicatorOutputBinding* FindBinding(
        const trading::app::IndicatorRenderPlan& plan,
        const std::string& id)
    {
        for (const auto& binding : plan.bindings) {
            if (binding.seriesId == id) return &binding;
        }
        return nullptr;
    }

    const trading::app::IndicatorReferenceBinding* FindReference(
        const trading::app::IndicatorRenderPlan& plan,
        const std::string& id)
    {
        for (const auto& reference : plan.references) {
            if (reference.referenceId == id) return &reference;
        }
        return nullptr;
    }
}

int main()
{
    using namespace trading::app;
    using namespace trading::render;

    IndicatorRenderPlan plan;
    std::string error;
    Check(BuildDefaultIndicatorRenderPlan(
              InitialSpecs(),
              plan,
              error),
          "initial indicator default render plan must build");
    Check(plan.bindings.size() == 14U,
          "initial indicator plan output binding count mismatch");
    Check(plan.references.size() == 3U,
          "initial indicator plan reference count mismatch");

    const IndicatorOutputBinding* sma =
        FindBinding(plan, "indicator.sma.fast.value");
    Check(sma != nullptr && sma->paneId == "price" &&
              sma->kind == IndicatorRenderKind::Line,
          "SMA must map to a standard price line");

    const IndicatorOutputBinding* jmaUp =
        FindBinding(plan, "indicator.jma.main.up");
    Check(jmaUp != nullptr && jmaUp->paneId == "price" &&
              jmaUp->outputIndex ==
                  trading::indicators::JmaUpOutput,
          "JMA Up output mapping mismatch");

    const IndicatorOutputBinding* jmaSlope =
        FindBinding(plan, "indicator.jma.main.slope");
    Check(jmaSlope != nullptr &&
              jmaSlope->kind == IndicatorRenderKind::Histogram &&
              jmaSlope->paneValueScale == PaneValueScale::Symmetric,
          "JMA slope must map to a symmetric histogram pane");
    Check(FindReference(
              plan,
              "indicator.jma.main.slope.zero") != nullptr,
          "JMA slope zero reference must be configured");

    Check(FindBinding(
              plan,
              "indicator.vwap.session.value") != nullptr &&
              FindBinding(
                  plan,
                  "indicator.vwap.session.upper1") != nullptr &&
              FindBinding(
                  plan,
                  "indicator.vwap.session.lower1") != nullptr &&
              FindBinding(
                  plan,
                  "indicator.vwap.session.upper2") != nullptr &&
              FindBinding(
                  plan,
                  "indicator.vwap.session.lower2") != nullptr,
          "VWAP five-output price overlay contract mismatch");

    const IndicatorOutputBinding* obvDirection =
        FindBinding(plan, "indicator.obv.flow.direction");
    Check(obvDirection != nullptr &&
              obvDirection->kind == IndicatorRenderKind::Histogram,
          "OBV Direction must map to a standard histogram");

    const IndicatorOutputBinding* adx =
        FindBinding(plan, "indicator.adx.trend.value");
    Check(adx != nullptr &&
              adx->paneValueScale == PaneValueScale::Fixed &&
              adx->fixedMinimum == 0.0 &&
              adx->fixedMaximum == 100.0,
          "ADX must map to a fixed 0-100 pane");
    const IndicatorReferenceBinding* adx20 =
        FindReference(plan, "indicator.adx.trend.reference.20");
    const IndicatorReferenceBinding* adx25 =
        FindReference(plan, "indicator.adx.trend.reference.25");
    Check(adx20 != nullptr && adx20->value == 20.0 &&
              adx25 != nullptr && adx25->value == 25.0,
          "ADX 20/25 reference plan mismatch");

    IndicatorRenderAdapter adapter;
    Check(adapter.Configure(plan, error),
          "default render plan must satisfy generic adapter contract");

    std::vector<trading::indicators::IndicatorSpec> duplicate =
        InitialSpecs();
    duplicate[1].id = duplicate[0].id;
    Check(!BuildDefaultIndicatorRenderPlan(
              duplicate,
              plan,
              error),
          "duplicate default indicator ids must fail closed");

    auto unsupported = Spec("custom.one", "CUSTOM");
    Check(!BuildDefaultIndicatorRenderPlan(
              { unsupported },
              plan,
              error),
          "unsupported default indicator type must fail closed");

    std::puts("[PASS] default_indicator_render_plan_tests");
    return 0;
}
