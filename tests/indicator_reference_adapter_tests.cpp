#include "../app/indicator_render_adapter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

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

    trading::app::IndicatorModuleSnapshot ReadySnapshot()
    {
        trading::app::IndicatorModuleSnapshot snapshot;
        snapshot.state = trading::app::IndicatorModuleState::Ready;
        snapshot.level = trading::app::FeatureLevel::Visible;
        snapshot.completedRevision = 7;
        snapshot.calculationRevision = 11;
        return snapshot;
    }

    trading::render::RenderDocument BaseDocument()
    {
        trading::render::RenderDocument document;
        document.workspaceId = "main";
        document.title = "005930";

        trading::render::Pane price;
        price.id = "price";
        price.title = "Price";

        trading::render::CandleSeries candles;
        candles.id = "market.price";
        candles.label = "005930";
        trading::Bar bar;
        bar.open = 100;
        bar.high = 110;
        bar.low = 90;
        bar.close = 105;
        bar.volume = 10;
        bar.closeTimestampMs = 1000;
        candles.bars.push_back(bar);
        price.candles.push_back(candles);
        document.panes.push_back(price);
        return document;
    }

    trading::app::IndicatorReferenceBinding Reference(
        const std::string& id,
        double value)
    {
        trading::app::IndicatorReferenceBinding reference;
        reference.paneId = "indicator.adx.pane";
        reference.paneTitle = "ADX";
        reference.paneHeightWeight = 0.25f;
        reference.paneValueScale = trading::render::PaneValueScale::Fixed;
        reference.fixedMinimum = 0.0;
        reference.fixedMaximum = 100.0;
        reference.valueDecimals = 2;
        reference.referenceId = id;
        reference.label = id;
        reference.value = value;
        reference.color = { 170, 174, 188, 200 };
        reference.width = 1.0f;
        return reference;
    }
}

int main()
{
    using namespace trading::app;
    using namespace trading::render;

    IndicatorRenderPlan invalid;
    invalid.references.push_back(Reference("", 20.0));
    IndicatorRenderAdapter adapter;
    std::string error;
    Check(!adapter.Configure(invalid, error),
          "empty indicator reference-line id must fail");

    invalid.references.clear();
    invalid.references.push_back(Reference(
        "indicator.adx.reference.nan",
        (std::numeric_limits<double>::quiet_NaN)()));
    Check(!adapter.Configure(invalid, error),
          "non-finite indicator reference-line value must fail");

    IndicatorRenderPlan duplicate;
    IndicatorOutputBinding line;
    line.indicatorId = "adx.trend";
    line.outputIndex = 0;
    line.kind = IndicatorRenderKind::Line;
    line.paneId = "indicator.adx.pane";
    line.paneTitle = "ADX";
    line.seriesId = "duplicate.id";
    line.label = "ADX";
    line.width = 1.0f;
    duplicate.bindings.push_back(line);
    duplicate.references.push_back(Reference("duplicate.id", 20.0));
    Check(!adapter.Configure(duplicate, error),
          "line and reference IDs must share one uniqueness domain");

    IndicatorRenderPlan plan;
    plan.references.push_back(Reference(
        "indicator.adx.reference.20",
        20.0));
    plan.references.push_back(Reference(
        "indicator.adx.reference.25",
        25.0));
    Check(adapter.Configure(plan, error),
          "valid indicator reference-line plan must configure");

    RenderDocument document = BaseDocument();
    const std::uint64_t revisionBefore = document.revision;
    const std::uint64_t structureBefore = document.structureRevision;
    Check(adapter.Apply(ReadySnapshot(), document, error),
          "generic indicator reference lines must apply");
    Check(document.panes.size() == 2U,
          "reference lines must create their configured pane once");
    const Pane& adx = document.panes[1];
    Check(adx.id == "indicator.adx.pane",
          "reference-line pane id mismatch");
    Check(adx.valueScale == PaneValueScale::Fixed &&
              adx.fixedMinimum == 0.0 &&
              adx.fixedMaximum == 100.0,
          "reference-line pane fixed range mismatch");
    Check(adx.referenceLines.size() == 2U,
          "ADX 20 and 25 reference lines must be published");
    Check(adx.referenceLines[0].id ==
              "indicator.adx.reference.20" &&
              adx.referenceLines[0].value == 20.0,
          "ADX 20 reference line mismatch");
    Check(adx.referenceLines[1].id ==
              "indicator.adx.reference.25" &&
              adx.referenceLines[1].value == 25.0,
          "ADX 25 reference line mismatch");
    Check(document.revision != revisionBefore,
          "reference-line contribution must advance document revision");
    Check(document.structureRevision != structureBefore,
          "reference-line contribution must advance structure revision");
    Check(ValidateRenderDocument(document, error),
          "reference-line render document must validate");
    Check(adapter.RetainedBytes() > 0U,
          "reference-line plan must report retained bytes");

    std::puts("[PASS] indicator_reference_adapter_tests");
    return 0;
}
