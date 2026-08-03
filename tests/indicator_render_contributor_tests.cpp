#include "../app/indicator_module.h"
#include "../app/indicator_render_contributor.h"

#include <cstdio>
#include <cstdlib>
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
        trading::Volume volume,
        trading::EpochMillis timestampMs)
    {
        trading::Bar bar;
        bar.open = close;
        bar.high = close + 2;
        bar.low = close - 2;
        bar.close = close;
        bar.volume = volume;
        bar.closeTimestampMs = timestampMs;
        bar.tickCount = 1;
        bar.tradingDateYmd = 20260803;
        return bar;
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

    std::vector<trading::indicators::IndicatorSpec> Specs()
    {
        auto sma = Spec("sma.fast", "SMA");
        sma.parameters.emplace("period", 3.0);

        auto jma = Spec("jma.main", "JMA");
        jma.parameters.emplace("period", 3.0);
        jma.parameters.emplace("phase", 0.0);
        jma.parameters.emplace("power", 2.0);

        auto vwap = Spec("vwap.session", "VWAP");
        vwap.parameters.emplace("std_dev_1", 1.0);
        vwap.parameters.emplace("std_dev_2", 2.0);

        auto obv = Spec("obv.flow", "OBV");
        obv.parameters.emplace("signal_period", 3.0);

        auto adx = Spec("adx.trend", "ADX");
        adx.parameters.emplace("period", 3.0);

        return { sma, jma, vwap, obv, adx };
    }

    trading::app::IndicatorModuleSnapshot BuildSnapshot(
        trading::app::IndicatorModule& module)
    {
        using namespace trading;
        using namespace trading::app;

        auto completed = std::make_shared<const std::vector<Bar>>(
            std::vector<Bar>{
                MakeBar(10, 100, 1000),
                MakeBar(11, 110, 2000),
                MakeBar(12, 120, 3000),
                MakeBar(13, 130, 4000),
                MakeBar(14, 140, 5000),
                MakeBar(15, 150, 6000),
                MakeBar(16, 160, 7000)
            });

        IndicatorMarketSource source;
        source.symbol = "005930";
        source.completedBars = completed;
        source.liveBar = MakeBar(17, 170, 8000);
        source.hasLiveBar = true;
        source.revision = 1;
        source.completedRevision = 1;

        std::string error;
        Check(module.Configure(Specs(), error),
              "indicator render fixture configuration failed");
        Check(module.SetLevel(FeatureLevel::Visible, error),
              "indicator render fixture level transition failed");
        Check(module.Update(source, error),
              "indicator render fixture calculation failed");
        return module.Snapshot();
    }

    trading::render::RenderDocument BaseDocument()
    {
        using namespace trading;
        using namespace trading::render;

        RenderDocument document;
        document.workspaceId = "main-chart";
        document.title = "005930";
        document.revision = 7;
        document.structureRevision = 3;

        Pane price;
        price.id = "price";
        price.title = "Price";

        CandleSeries candles;
        candles.id = "market.price";
        candles.label = "005930";
        for (int index = 0; index < 8; ++index) {
            candles.bars.push_back(MakeBar(
                10 + index,
                100 + index * 10,
                1000 + index * 1000));
        }
        price.candles.push_back(std::move(candles));
        document.panes.push_back(std::move(price));
        return document;
    }

    const trading::render::Pane* FindPane(
        const trading::render::RenderDocument& document,
        const std::string& id)
    {
        for (const trading::render::Pane& pane : document.panes) {
            if (pane.id == id) return &pane;
        }
        return nullptr;
    }

    bool HasLine(
        const trading::render::Pane& pane,
        const std::string& id)
    {
        for (const trading::render::LineSeries& line : pane.lines) {
            if (line.id == id) return true;
        }
        return false;
    }
}

int main()
{
    using namespace trading::app;
    using namespace trading::render;

    IndicatorModule module;
    const IndicatorModuleSnapshot snapshot = BuildSnapshot(module);
    RenderDocument document = BaseDocument();
    const std::uint64_t originalRevision = document.revision;
    const std::uint64_t originalStructureRevision =
        document.structureRevision;

    IndicatorRenderContributionStats stats;
    std::string error;
    Check(AppendIndicatorRenderContributions(
              snapshot,
              document,
              stats,
              error),
          "indicator render contributions must succeed");
    Check(ValidateRenderDocument(document, error),
          "indicator render document must validate");
    Check(document.panes.size() == 4U,
          "indicator contributions must add three lower panes");
    Check(stats.paneCount == 3U,
          "indicator contribution pane metric mismatch");
    Check(stats.lineSeriesCount == 11U,
          "indicator contribution line-series metric mismatch");
    Check(stats.histogramSeriesCount == 2U,
          "indicator contribution histogram metric mismatch");
    Check(stats.referenceLineCount == 3U,
          "indicator contribution reference-line metric mismatch");
    Check(document.revision != originalRevision,
          "indicator contribution must advance document revision");
    Check(document.structureRevision != originalStructureRevision,
          "indicator contribution must advance structure revision");

    const Pane* price = FindPane(document, "price");
    Check(price != nullptr, "price pane must remain present");
    Check(HasLine(*price, "indicator.sma.fast.value"),
          "SMA must publish a standard price line");
    Check(HasLine(*price, "indicator.jma.main.value"),
          "JMA Value must publish a standard price line");
    Check(HasLine(*price, "indicator.jma.main.up.segment.0"),
          "JMA Up must publish segmented standard lines");
    Check(HasLine(*price, "indicator.vwap.session.value"),
          "VWAP Value must publish a standard price line");
    Check(HasLine(*price, "indicator.vwap.session.upper2"),
          "VWAP Upper2 must publish a standard price line");

    const Pane* slope =
        FindPane(document, "indicator.jma.main.slope.pane");
    Check(slope != nullptr && slope->histograms.size() == 1U,
          "JMA slope must publish a histogram pane");
    Check(slope->referenceLines.size() == 1U,
          "JMA slope pane must publish a zero reference line");
    Check(slope->valueScale == PaneValueScale::Symmetric,
          "JMA slope pane must use symmetric scaling");

    const Pane* obv = FindPane(document, "indicator.obv.flow.pane");
    Check(obv != nullptr && obv->lines.size() == 2U,
          "OBV must publish Value and Signal lines");
    Check(obv->histograms.size() == 1U,
          "OBV must publish Direction histogram");

    const Pane* adx = FindPane(document, "indicator.adx.trend.pane");
    Check(adx != nullptr && adx->lines.size() == 1U,
          "ADX must publish a standard line");
    Check(adx->referenceLines.size() == 2U,
          "ADX must publish 20 and 25 reference lines");
    Check(adx->valueScale == PaneValueScale::Fixed &&
              adx->fixedMinimum == 0.0 &&
              adx->fixedMaximum == 100.0,
          "ADX pane must use fixed 0-100 scaling");

    Check(module.SetLevel(FeatureLevel::Off, error),
          "indicator module Off transition failed");
    RenderDocument offDocument = BaseDocument();
    Check(AppendIndicatorRenderContributions(
              module.Snapshot(),
              offDocument,
              stats,
              error),
          "Off indicator contribution must be a no-op");
    Check(stats.TotalSeries() == 0U && stats.paneCount == 0U,
          "Off indicator contribution must publish no work");
    Check(offDocument.panes.size() == 1U,
          "Off indicator contribution must not alter the document");

    RenderDocument missingPrice;
    missingPrice.workspaceId = "missing-price";
    Pane volume;
    volume.id = "volume";
    volume.title = "Volume";
    missingPrice.panes.push_back(volume);
    Check(!AppendIndicatorRenderContributions(
              snapshot,
              missingPrice,
              stats,
              error),
          "indicator contribution without price pane must fail closed");
    Check(error.find("price pane") != std::string::npos,
          "missing price pane fault must be explicit");

    std::puts("[PASS] indicator_render_contributor_tests");
    return 0;
}
