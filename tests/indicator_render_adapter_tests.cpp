#include "../app/indicator_render_adapter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
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

    trading::Bar MakeBar(
        trading::PriceWon close,
        trading::EpochMillis timestampMs)
    {
        trading::Bar bar;
        bar.open = close;
        bar.high = close + 1;
        bar.low = close - 1;
        bar.close = close;
        bar.volume = 100;
        bar.closeTimestampMs = timestampMs;
        bar.tickCount = 1;
        bar.tradingDateYmd = 20260803;
        return bar;
    }

    trading::render::RenderDocument BaseDocument()
    {
        trading::render::RenderDocument document;
        document.workspaceId = "main";
        document.title = "005930";
        document.revision = 10;
        document.structureRevision = 3;

        trading::render::Pane price;
        price.id = "price";
        price.title = "Price";
        trading::render::CandleSeries candles;
        candles.id = "stock.candles";
        candles.label = "005930";
        for (int index = 0; index < 6; ++index) {
            candles.bars.push_back(
                MakeBar(10 + index, 1000 + index * 1000));
        }
        price.candles.push_back(std::move(candles));
        document.panes.push_back(std::move(price));
        return document;
    }

    trading::app::IndicatorModuleSnapshot Snapshot(
        std::shared_ptr<const std::vector<
            trading::indicators::IndicatorValue>> completed,
        double liveValue,
        double liveSlope,
        std::uint64_t calculationRevision,
        std::uint64_t completedRevision)
    {
        using namespace trading::app;
        using namespace trading::indicators;

        IndicatorModuleSnapshot snapshot;
        snapshot.state = IndicatorModuleState::Ready;
        snapshot.level = FeatureLevel::Visible;
        snapshot.symbol = "005930";
        snapshot.calculationRevision = calculationRevision;
        snapshot.completedRevision = completedRevision;

        IndicatorSeriesSnapshot series;
        series.spec.id = "jma.main";
        series.spec.type = "JMA";
        series.completedValues = std::move(completed);
        series.hasLiveValue = true;
        series.liveValue.timestampMs = 6000;
        series.liveValue.SetOutput(1, liveValue);
        series.liveValue.SetOutput(3, liveSlope);
        snapshot.series.push_back(std::move(series));
        return snapshot;
    }

    std::shared_ptr<const std::vector<
        trading::indicators::IndicatorValue>> CompletedValues()
    {
        using trading::indicators::IndicatorValue;
        auto values = std::make_shared<std::vector<IndicatorValue>>();
        for (int index = 0; index < 5; ++index) {
            IndicatorValue value;
            value.timestampMs = 1000 + index * 1000;
            if (index == 2) {
                value.SetOutput(
                    1,
                    (std::numeric_limits<double>::quiet_NaN)());
            }
            else {
                value.SetOutput(1, 10.0 + index);
            }
            value.SetOutput(3, index % 2 == 0 ? 1.0 : -1.0);
            values->push_back(value);
        }
        return values;
    }

    trading::app::IndicatorRenderPlan Plan()
    {
        using namespace trading::app;

        IndicatorRenderPlan plan;

        IndicatorOutputBinding up;
        up.indicatorId = "jma.main";
        up.outputIndex = 1;
        up.kind = IndicatorRenderKind::Line;
        up.paneId = "price";
        up.seriesId = "jma.up";
        up.label = "JMA Up";
        up.width = 1.5f;
        plan.bindings.push_back(up);

        IndicatorOutputBinding slope;
        slope.indicatorId = "jma.main";
        slope.outputIndex = 3;
        slope.kind = IndicatorRenderKind::Histogram;
        slope.paneId = "jma.slope";
        slope.paneTitle = "JMA Slope";
        slope.paneHeightWeight = 0.25f;
        slope.paneValueScale =
            trading::render::PaneValueScale::Symmetric;
        slope.seriesId = "jma.slope.histogram";
        slope.label = "Slope";
        plan.bindings.push_back(slope);

        return plan;
    }
}

int main()
{
    using namespace trading::app;

    IndicatorRenderAdapter invalid;
    IndicatorRenderPlan invalidPlan = Plan();
    invalidPlan.bindings[1].seriesId = "jma.up";
    std::string error;
    Check(!invalid.Configure(invalidPlan, error),
          "duplicate render series ids must fail closed");

    IndicatorRenderAdapter adapter;
    Check(adapter.Configure(Plan(), error),
          "valid indicator render plan must configure");
    Check(adapter.PlanRevision() > 0,
          "indicator render plan revision must advance");

    const auto completed = CompletedValues();
    IndicatorModuleSnapshot firstSnapshot =
        Snapshot(completed, 15.0, -2.0, 11, 7);
    trading::render::RenderDocument first = BaseDocument();
    Check(adapter.Apply(firstSnapshot, first, error),
          "indicator render contribution must apply");
    Check(first.panes.size() == 2U,
          "indicator histogram pane must be created generically");
    Check(first.panes[0].lines.size() == 2U,
          "NaN line gap must split into two finite line segments");
    Check(first.panes[0].lines[0].id == "jma.up",
          "first line segment id mismatch");
    Check(first.panes[0].lines[1].id == "jma.up.segment.2",
          "second line segment id mismatch");
    Check(first.panes[0].lines[0].points.size() == 2U,
          "first line segment point count mismatch");
    Check(first.panes[0].lines[1].points.size() == 3U,
          "live tail must extend the final finite line segment");
    Check(first.panes[0].lines[1].points.HasLiveTail(),
          "final line segment must expose a live tail");
    Check(first.panes[1].histograms.size() == 1U,
          "indicator histogram contribution count mismatch");
    Check(first.panes[1].histograms[0].points.size() == 6U,
          "histogram must share completed points plus live tail");
    Check(first.panes[1].histograms[0].points.HasLiveTail(),
          "histogram live tail contract is missing");

    const auto firstLinePrefix =
        first.panes[0].lines[1].points.SharedPrefix();
    const auto firstHistogramPrefix =
        first.panes[1].histograms[0].points.SharedPrefix();
    const std::uint64_t firstStructureRevision =
        first.structureRevision;
    const std::uint64_t firstDocumentRevision = first.revision;

    IndicatorModuleSnapshot replacement =
        Snapshot(completed, 16.0, 3.0, 12, 7);
    trading::render::RenderDocument second = BaseDocument();
    Check(adapter.Apply(replacement, second, error),
          "live-only indicator render update must apply");
    Check(
        second.panes[0].lines[1].points.SharedPrefix().get() ==
            firstLinePrefix.get(),
        "completed line render points must be reused on live updates");
    Check(
        second.panes[1].histograms[0].points.SharedPrefix().get() ==
            firstHistogramPrefix.get(),
        "completed histogram render points must be reused on live updates");
    Check(second.structureRevision == firstStructureRevision,
          "live-only update must preserve render structure revision");
    Check(second.revision != firstDocumentRevision,
          "live-only update must advance render document revision");
    Check(second.panes[0].lines[1].points.LiveTail().value == 16.0,
          "line live-tail replacement value mismatch");
    Check(second.panes[1].histograms[0].points.LiveTail().value == 3.0,
          "histogram live-tail replacement value mismatch");

    IndicatorRenderPlan missingPlan = Plan();
    missingPlan.bindings[0].indicatorId = "missing";
    IndicatorRenderAdapter missing;
    Check(missing.Configure(missingPlan, error),
          "missing-source plan must configure structurally");
    trading::render::RenderDocument missingDocument = BaseDocument();
    Check(!missing.Apply(firstSnapshot, missingDocument, error),
          "missing indicator render source must fail closed");

    Check(adapter.RetainedBytes() > 0U,
          "indicator render cache retained-byte metric must be positive");

    std::puts("[PASS] indicator_render_adapter_tests");
    return 0;
}
