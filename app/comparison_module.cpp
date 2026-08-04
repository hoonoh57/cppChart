#include "comparison_module.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <utility>

namespace trading::app
{
    namespace
    {
        std::size_t DynamicStringBytes(const std::string& value) noexcept
        {
            return value.empty() ? 0U : value.capacity();
        }

        bool SameSource(
            const ComparisonDefinition& left,
            const ComparisonDefinition& right) noexcept
        {
            return
                left.id == right.id &&
                left.kind == right.kind &&
                left.code == right.code;
        }
    }

    ComparisonModule::ComparisonModule() = default;

    bool ComparisonModule::SetLevel(
        FeatureLevel level,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
        if (level == FeatureLevel::Off) {
            for (Runtime& runtime : runtimes_) {
                runtime.state = ComparisonSeriesState::Empty;
                runtime.completedBars = EmptyBars();
                runtime.liveBar = {};
                runtime.hasLiveBar = false;
                runtime.continuation = {};
                runtime.error.clear();
                ++runtime.revision;
                ++runtime.completedRevision;
                ++runtime.liveRevision;
            }
            ++revision_;
            error_.clear();
        }
        error.clear();
        return true;
    }

    FeatureLevel ComparisonModule::Level() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return level_;
    }

    bool ComparisonModule::Configure(
        const std::vector<ComparisonDefinition>& definitions,
        std::string& error)
    {
        if (definitions.size() > 32U) {
            error = "comparison series limit is 32";
            return false;
        }

        std::set<std::string> ids;
        for (const ComparisonDefinition& definition : definitions) {
            if (!ValidateDefinition(definition, error)) return false;
            if (!ids.insert(definition.id).second) {
                error = "duplicate comparison id: " + definition.id;
                return false;
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Runtime> candidate;
        candidate.reserve(definitions.size());
        for (const ComparisonDefinition& definition : definitions) {
            Runtime runtime;
            const auto existing = std::find_if(
                runtimes_.begin(),
                runtimes_.end(),
                [&definition](const Runtime& value) {
                    return SameSource(value.definition, definition);
                });
            if (existing != runtimes_.end()) {
                runtime = *existing;
            }
            else {
                runtime.completedBars = EmptyBars();
            }
            runtime.definition = definition;
            ++runtime.revision;
            candidate.push_back(std::move(runtime));
        }

        runtimes_ = std::move(candidate);
        ++revision_;
        error_.clear();
        error.clear();
        return true;
    }

    bool ComparisonModule::BeginRequest(
        const std::string& comparisonId,
        int minuteUnit,
        std::string& error)
    {
        if (!IsSupportedMinuteUnit(minuteUnit)) {
            error = "unsupported comparison minute unit";
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!IsVisibleLevel(level_)) {
            error = level_ == FeatureLevel::Off
                ? "comparison module is Off"
                : "comparison module is Standby";
            return false;
        }
        for (Runtime& runtime : runtimes_) {
            if (runtime.definition.id != comparisonId) continue;
            runtime.minuteUnit = minuteUnit;
            runtime.state = ComparisonSeriesState::Loading;
            runtime.error.clear();
            runtime.continuation = {};
            ++runtime.revision;
            ++revision_;
            error.clear();
            return true;
        }
        error = "comparison definition was not found: " + comparisonId;
        return false;
    }

    ComparisonApplyResult ComparisonModule::ApplyMinuteBars(
        const MinuteBarsPage& page,
        const Continuation& continuation)
    {
        ComparisonApplyResult result;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!IsVisibleLevel(level_)) {
            result.stale = true;
            result.error = "comparison minute response ignored while inactive";
            return result;
        }

        std::vector<Runtime*> targets;
        for (Runtime& runtime : runtimes_) {
            if (runtime.definition.code != page.code ||
                Instrument(runtime.definition.kind) != page.instrument ||
                runtime.minuteUnit != page.minuteUnit)
            {
                continue;
            }
            targets.push_back(&runtime);
        }
        if (targets.empty()) {
            result.stale = true;
            result.error = "minute response does not match a comparison source";
            return result;
        }

        const std::string pageError = !page.result.error.empty()
            ? page.result.error
            : page.result.returnMessage;
        if (!page.result.ok || page.bars.empty()) {
            result.error = pageError.empty()
                ? "comparison minute response is empty"
                : pageError;
            for (Runtime* target : targets) {
                target->state = ComparisonSeriesState::Error;
                target->error = result.error;
                ++target->revision;
            }
            ++revision_;
            error_ = result.error;
            return result;
        }

        auto completed = std::make_shared<std::vector<Bar>>();
        if (page.bars.size() > 1U) {
            completed->assign(page.bars.begin(), page.bars.end() - 1);
        }
        const std::shared_ptr<const std::vector<Bar>> immutable = completed;
        const Bar live = page.bars.back();

        for (Runtime* target : targets) {
            target->completedBars = immutable;
            target->liveBar = live;
            target->hasLiveBar = true;
            target->continuation = continuation;
            target->state = ComparisonSeriesState::Ready;
            target->error.clear();
            ++target->revision;
            ++target->completedRevision;
            ++target->liveRevision;
            if (result.comparisonId.empty()) {
                result.comparisonId = target->definition.id;
            }
        }
        ++revision_;
        error_.clear();
        result.applied = true;
        result.code = page.code;
        return result;
    }

    ComparisonApplyResult ComparisonModule::ApplyStockTradeTick(
        const StockTradeTick& tick)
    {
        ComparisonApplyResult result;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!IsVisibleLevel(level_)) {
            result.stale = true;
            return result;
        }

        for (Runtime& runtime : runtimes_) {
            if (runtime.definition.kind != ComparisonInstrumentKind::Stock ||
                runtime.definition.code != tick.code ||
                runtime.state != ComparisonSeriesState::Ready ||
                !runtime.hasLiveBar)
            {
                continue;
            }
            ComparisonApplyResult applied = ApplyRealtimeLocked(
                runtime,
                tick.tradeTimeHhmmss,
                tick.priceWon,
                tick.tradeVolume,
                tick.cumulativeVolume);
            if (applied.applied) {
                result = applied;
                result.code = tick.code;
            }
            else if (!applied.stale) {
                result = applied;
            }
        }
        return result;
    }

    ComparisonApplyResult ComparisonModule::ApplyIndexValueTick(
        const IndexValueTick& tick)
    {
        ComparisonApplyResult result;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!IsVisibleLevel(level_)) {
            result.stale = true;
            return result;
        }

        for (Runtime& runtime : runtimes_) {
            if (runtime.definition.kind != ComparisonInstrumentKind::Index ||
                runtime.definition.code != tick.code ||
                runtime.state != ComparisonSeriesState::Ready ||
                !runtime.hasLiveBar)
            {
                continue;
            }
            ComparisonApplyResult applied = ApplyRealtimeLocked(
                runtime,
                tick.tradeTimeHhmmss,
                tick.value,
                tick.tradeVolume,
                tick.cumulativeVolume);
            if (applied.applied) {
                result = applied;
                result.code = tick.code;
            }
            else if (!applied.stale) {
                result = applied;
            }
        }
        return result;
    }

    void ComparisonModule::SetError(
        const std::string& comparisonId,
        const std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (Runtime& runtime : runtimes_) {
            if (runtime.definition.id != comparisonId) continue;
            runtime.state = ComparisonSeriesState::Error;
            runtime.error = error;
            ++runtime.revision;
            ++revision_;
            error_ = error;
            return;
        }
    }

    ComparisonModuleSnapshot ComparisonModule::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ComparisonModuleSnapshot result;
        result.level = level_;
        result.revision = revision_;
        result.error = error_;
        result.series.reserve(runtimes_.size());
        for (const Runtime& runtime : runtimes_) {
            ComparisonSeriesSnapshot item;
            item.definition = runtime.definition;
            item.state = runtime.state;
            item.minuteUnit = runtime.minuteUnit;
            item.completedBars = runtime.completedBars;
            item.liveBar = runtime.liveBar;
            item.hasLiveBar = runtime.hasLiveBar;
            item.barCount =
                (runtime.completedBars ? runtime.completedBars->size() : 0U) +
                (runtime.hasLiveBar ? 1U : 0U);
            item.continuation = runtime.continuation;
            item.error = runtime.error;
            item.revision = runtime.revision;
            item.completedRevision = runtime.completedRevision;
            item.liveRevision = runtime.liveRevision;
            result.retainedBytes +=
                (runtime.completedBars
                    ? runtime.completedBars->capacity() * sizeof(Bar)
                    : 0U) +
                (runtime.hasLiveBar ? sizeof(Bar) : 0U) +
                DynamicStringBytes(runtime.definition.id) +
                DynamicStringBytes(runtime.definition.code) +
                DynamicStringBytes(runtime.definition.displayName) +
                DynamicStringBytes(runtime.definition.paneId) +
                DynamicStringBytes(runtime.definition.paneTitle) +
                DynamicStringBytes(runtime.error);
            result.series.push_back(std::move(item));
        }
        return result;
    }

    const ComparisonDefinition* ComparisonModule::FindDefinition(
        const std::string& comparisonId,
        ComparisonDefinition& copy) const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Runtime& runtime : runtimes_) {
            if (runtime.definition.id != comparisonId) continue;
            copy = runtime.definition;
            return &copy;
        }
        return nullptr;
    }

    bool ComparisonModule::ValidateDefinition(
        const ComparisonDefinition& definition,
        std::string& error)
    {
        if (definition.id.empty()) {
            error = "comparison id is empty";
            return false;
        }
        if (definition.code.empty()) {
            error = "comparison code is empty";
            return false;
        }
        if (definition.code.size() > 12U) {
            error = "comparison code is too long";
            return false;
        }
        for (char value : definition.code) {
            if (std::isalnum(static_cast<unsigned char>(value)) == 0) {
                error = "comparison code must be alphanumeric";
                return false;
            }
        }
        if (definition.displayName.empty()) {
            error = "comparison display name is empty";
            return false;
        }
        if (!std::isfinite(definition.valueDivisor) ||
            definition.valueDivisor <= 0.0)
        {
            error = "comparison value divisor must be positive";
            return false;
        }
        if (!std::isfinite(definition.width) ||
            definition.width <= 0.0f || definition.width > 12.0f)
        {
            error = "comparison line width is invalid";
            return false;
        }
        if (!std::isfinite(definition.paneHeightWeight) ||
            definition.paneHeightWeight <= 0.0f)
        {
            error = "comparison pane height is invalid";
            return false;
        }
        if (definition.valueDecimals < 0 || definition.valueDecimals > 8) {
            error = "comparison decimals are invalid";
            return false;
        }
        error.clear();
        return true;
    }

    const char* ComparisonModule::StateName(
        ComparisonSeriesState state) noexcept
    {
        switch (state) {
        case ComparisonSeriesState::Loading: return "조회 중";
        case ComparisonSeriesState::Ready: return "준비";
        case ComparisonSeriesState::Error: return "오류";
        default: return "데이터 없음";
        }
    }

    std::shared_ptr<const std::vector<Bar>> ComparisonModule::EmptyBars()
    {
        static const auto empty =
            std::make_shared<const std::vector<Bar>>();
        return empty;
    }

    EpochMillis ComparisonModule::KstSessionDateStart(
        EpochMillis timestampMs) noexcept
    {
        constexpr EpochMillis DayMs = 24LL * 60LL * 60LL * 1000LL;
        constexpr EpochMillis KstOffsetMs = 9LL * 60LL * 60LL * 1000LL;
        return ((timestampMs + KstOffsetMs) / DayMs) * DayMs - KstOffsetMs;
    }

    bool ComparisonModule::IsVisibleLevel(FeatureLevel level) noexcept
    {
        return level == FeatureLevel::Visible ||
               level == FeatureLevel::Active;
    }

    MinuteBarInstrument ComparisonModule::Instrument(
        ComparisonInstrumentKind kind) noexcept
    {
        return kind == ComparisonInstrumentKind::Index
            ? MinuteBarInstrument::Index
            : MinuteBarInstrument::Stock;
    }

    ComparisonApplyResult ComparisonModule::ApplyRealtimeLocked(
        Runtime& runtime,
        int tradeTimeHhmmss,
        PriceWon price,
        Volume tradeVolume,
        Volume cumulativeVolume)
    {
        ComparisonApplyResult result;
        StockTradeTick tick;
        tick.code = runtime.definition.code;
        tick.tradeTimeHhmmss = tradeTimeHhmmss;
        tick.priceWon = price;
        tick.tradeVolume = tradeVolume;
        tick.cumulativeVolume = cumulativeVolume;

        std::vector<Bar> liveWindow;
        liveWindow.reserve(2U);
        liveWindow.push_back(runtime.liveBar);
        std::string mergeError;
        if (!MergeStockTradeIntoMinuteBars(
                liveWindow,
                runtime.minuteUnit,
                KstSessionDateStart(runtime.liveBar.closeTimestampMs),
                tick,
                mergeError))
        {
            result.stale =
                mergeError.find("stale stock trade") != std::string::npos;
            result.error = mergeError;
            return result;
        }

        if (liveWindow.size() == 1U) {
            runtime.liveBar = liveWindow.front();
        }
        else if (liveWindow.size() == 2U) {
            auto completed = std::make_shared<std::vector<Bar>>();
            if (runtime.completedBars) *completed = *runtime.completedBars;
            completed->push_back(liveWindow.front());
            runtime.completedBars = std::move(completed);
            runtime.liveBar = liveWindow.back();
            ++runtime.completedRevision;
        }
        else {
            result.error = "comparison realtime merge produced too many bars";
            return result;
        }

        ++runtime.revision;
        ++runtime.liveRevision;
        ++revision_;
        runtime.error.clear();
        result.applied = true;
        result.comparisonId = runtime.definition.id;
        return result;
    }

    std::string NextComparisonId(
        ComparisonInstrumentKind kind,
        const std::vector<ComparisonDefinition>& definitions)
    {
        const std::string prefix =
            kind == ComparisonInstrumentKind::Index
                ? "compare.index."
                : "compare.stock.";
        for (std::size_t index = 1U; index < 100000U; ++index) {
            const std::string candidate = prefix + std::to_string(index);
            bool exists = false;
            for (const ComparisonDefinition& definition : definitions) {
                if (definition.id == candidate) {
                    exists = true;
                    break;
                }
            }
            if (!exists) return candidate;
        }
        return prefix + "overflow";
    }
}
