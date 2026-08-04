#pragma once

#include "../core/kiwoom_market_data.h"
#include "../core/kiwoom_index_realtime.h"
#include "feature_registry.h"
#include "../render/render_document.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace trading::app
{
    enum class ComparisonInstrumentKind
    {
        Stock,
        Index
    };

    enum class ComparisonPlacement
    {
        SeparatePane,
        PriceSecondaryAxis
    };

    enum class ComparisonSeriesState
    {
        Empty,
        Loading,
        Ready,
        Error
    };

    struct ComparisonDefinition final
    {
        std::string id;
        ComparisonInstrumentKind kind = ComparisonInstrumentKind::Stock;
        std::string code;
        std::string displayName;
        bool visible = true;
        ComparisonPlacement placement = ComparisonPlacement::SeparatePane;
        std::string paneId;
        std::string paneTitle;
        double valueDivisor = 1.0;
        render::ColorRgba color{ 64, 210, 225, 255 };
        float width = 1.5f;
        render::LineStyle style = render::LineStyle::Solid;
        float paneHeightWeight = 0.35f;
        int valueDecimals = 2;
    };

    struct ComparisonSeriesSnapshot final
    {
        ComparisonDefinition definition;
        ComparisonSeriesState state = ComparisonSeriesState::Empty;
        int minuteUnit = 1;
        std::shared_ptr<const std::vector<Bar>> completedBars;
        Bar liveBar;
        bool hasLiveBar = false;
        std::size_t barCount = 0;
        Continuation continuation;
        std::string error;
        std::uint64_t revision = 0;
        std::uint64_t completedRevision = 0;
        std::uint64_t liveRevision = 0;
    };

    struct ComparisonModuleSnapshot final
    {
        FeatureLevel level = FeatureLevel::Off;
        std::vector<ComparisonSeriesSnapshot> series;
        std::uint64_t revision = 0;
        std::size_t retainedBytes = 0;
        std::string error;
    };

    struct ComparisonApplyResult final
    {
        bool applied = false;
        bool stale = false;
        std::string comparisonId;
        std::string code;
        std::string error;
    };

    class ComparisonModule final
    {
    public:
        ComparisonModule();
        ComparisonModule(const ComparisonModule&) = delete;
        ComparisonModule& operator=(const ComparisonModule&) = delete;

        bool SetLevel(FeatureLevel level, std::string& error);
        FeatureLevel Level() const noexcept;

        bool Configure(
            const std::vector<ComparisonDefinition>& definitions,
            std::string& error);

        bool BeginRequest(
            const std::string& comparisonId,
            int minuteUnit,
            std::string& error);

        ComparisonApplyResult ApplyMinuteBars(
            const MinuteBarsPage& page,
            const Continuation& continuation);

        ComparisonApplyResult ApplyStockTradeTick(
            const StockTradeTick& tick);

        ComparisonApplyResult ApplyIndexValueTick(
            const IndexValueTick& tick);

        void SetError(
            const std::string& comparisonId,
            const std::string& error);

        ComparisonModuleSnapshot Snapshot() const;

        bool FindDefinition(
            const std::string& comparisonId,
            ComparisonDefinition& copy) const noexcept;

        static bool ValidateDefinition(
            const ComparisonDefinition& definition,
            std::string& error);

        static const char* StateName(
            ComparisonSeriesState state) noexcept;

    private:
        struct Runtime final
        {
            ComparisonDefinition definition;
            ComparisonSeriesState state = ComparisonSeriesState::Empty;
            int minuteUnit = 1;
            std::shared_ptr<const std::vector<Bar>> completedBars;
            Bar liveBar;
            bool hasLiveBar = false;
            Continuation continuation;
            std::string error;
            std::uint64_t revision = 0;
            std::uint64_t completedRevision = 0;
            std::uint64_t liveRevision = 0;
        };

        static std::shared_ptr<const std::vector<Bar>> EmptyBars();
        static EpochMillis KstSessionDateStart(EpochMillis timestampMs) noexcept;
        static bool IsVisibleLevel(FeatureLevel level) noexcept;
        static MinuteBarInstrument Instrument(
            ComparisonInstrumentKind kind) noexcept;

        ComparisonApplyResult ApplyRealtimeLocked(
            Runtime& runtime,
            int tradeTimeHhmmss,
            PriceWon price,
            Volume tradeVolume,
            Volume cumulativeVolume);

        mutable std::mutex mutex_;
        FeatureLevel level_ = FeatureLevel::Visible;
        std::vector<Runtime> runtimes_;
        std::uint64_t revision_ = 0;
        std::string error_;
    };

    std::string NextComparisonId(
        ComparisonInstrumentKind kind,
        const std::vector<ComparisonDefinition>& definitions);
}
