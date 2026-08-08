#include "indicator_module.h"

#include "../core/adx_indicator.h"
#include "../core/jma_indicator.h"
#include "../core/obv_indicator.h"
#include "../core/sma_indicator.h"
#include "../core/standard_indicators.h"
#include "../core/vwap_indicator.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <utility>

namespace trading::app
{
    namespace
    {
        bool IsCalculatingLevel(FeatureLevel level) noexcept
        {
            return
                level == FeatureLevel::Visible ||
                level == FeatureLevel::Active;
        }

        std::uint64_t ElapsedMicros(
            const std::chrono::steady_clock::time_point& start) noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count());
        }
    }

    IndicatorModule::IndicatorModule()
    {
        const bool registered =
            indicators::RegisterSmaIndicator(registry_) &&
            indicators::RegisterJmaIndicator(registry_) &&
            indicators::RegisterObvIndicator(registry_) &&
            indicators::RegisterAdxIndicator(registry_) &&
            indicators::RegisterVwapIndicator(registry_) &&
            indicators::RegisterEmaIndicator(registry_) &&
            indicators::RegisterBollingerIndicator(registry_) &&
            indicators::RegisterRsiIndicator(registry_) &&
            indicators::RegisterMacdIndicator(registry_) &&
            indicators::RegisterDmiIndicator(registry_) &&
            indicators::RegisterSuperTrendIndicator(registry_);
        if (!registered) {
            state_ = IndicatorModuleState::Error;
            error_ = "builtin indicator registration failed";
        }
    }

    bool IndicatorModule::SetLevel(
        FeatureLevel level,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == IndicatorModuleState::Error && specs_.empty()) {
            error = error_;
            return false;
        }

        if (level == FeatureLevel::Off) {
            level_ = level;
            ClearCalculatedStateLocked();
            state_ = IndicatorModuleState::Empty;
            error_.clear();
            metrics_.retainedBytes = 0;
            metrics_.symbolCount = 0;
            metrics_.renderSeriesCount = 0;
            error.clear();
            return true;
        }

        if (runtimes_.empty() && !specs_.empty()) {
            if (!BuildRuntimesLocked(error)) {
                state_ = IndicatorModuleState::Error;
                error_ = error;
                return false;
            }
        }

        level_ = level;
        if (state_ == IndicatorModuleState::Error) {
            state_ = IndicatorModuleState::Empty;
            error_.clear();
        }
        metrics_.retainedBytes = EstimateRetainedBytesLocked();
        error.clear();
        return true;
    }

    FeatureLevel IndicatorModule::Level() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return level_;
    }

    bool IndicatorModule::Configure(
        const std::vector<indicators::IndicatorSpec>& specs,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::set<std::string> ids;
        for (const indicators::IndicatorSpec& spec : specs) {
            if (!ids.insert(spec.id).second) {
                error = "duplicate indicator id: " + spec.id;
                return false;
            }

            std::string createError;
            indicators::IndicatorInstance validation =
                registry_.Create(spec, createError);
            if (!validation.IsValid()) {
                error = createError;
                return false;
            }
        }

        specs_ = specs;
        ClearCalculatedStateLocked();
        state_ = IndicatorModuleState::Empty;
        error_.clear();

        if (level_ != FeatureLevel::Off && !specs_.empty()) {
            if (!BuildRuntimesLocked(error)) {
                state_ = IndicatorModuleState::Error;
                error_ = error;
                return false;
            }
        }

        metrics_.retainedBytes = EstimateRetainedBytesLocked();
        metrics_.renderSeriesCount = 0;
        error.clear();
        return true;
    }

    bool IndicatorModule::Update(
        const IndicatorMarketSource& source,
        std::string& error)
    {
        const auto started = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);

        if (!IsCalculatingLevel(level_)) {
            error = level_ == FeatureLevel::Off
                ? "indicator module is Off"
                : "indicator module is Standby";
            return false;
        }
        if (source.symbol.empty()) {
            error = "indicator source symbol is empty";
            return false;
        }
        if (!source.completedBars) {
            error = "indicator completed-bar history is missing";
            return false;
        }
        if (source.completedBars->empty() && !source.hasLiveBar) {
            error = "indicator source bars are empty";
            return false;
        }
        if (specs_.empty()) {
            error = "indicator configuration is empty";
            return false;
        }
        if (
            state_ == IndicatorModuleState::Ready &&
            sourceRevision_ == source.revision &&
            symbol_ == source.symbol)
        {
            error.clear();
            return true;
        }

        const bool symbolChanged =
            !symbol_.empty() && symbol_ != source.symbol;
        const bool completedChanged =
            symbolChanged ||
            state_ != IndicatorModuleState::Ready ||
            completedRevision_ != source.completedRevision ||
            runtimes_.size() != specs_.size();

        if (!completedChanged) {
            for (const Runtime& runtime : runtimes_) {
                if (
                    runtime.hasLiveValue &&
                    (
                        !source.hasLiveBar ||
                        runtime.liveValue.timestampMs !=
                            source.liveBar.closeTimestampMs))
                {
                    error =
                        "indicator live timestamp changed without completed revision";
                    state_ = IndicatorModuleState::Error;
                    error_ = error;
                    return false;
                }
            }
        }

        if (completedChanged) {
            std::vector<Runtime> candidates;
            candidates.reserve(specs_.size());

            for (const indicators::IndicatorSpec& spec : specs_) {
                std::string createError;
                indicators::IndicatorInstance instance =
                    registry_.Create(spec, createError);
                if (!instance.IsValid()) {
                    error = createError;
                    state_ = IndicatorModuleState::Error;
                    error_ = error;
                    return false;
                }

                std::vector<indicators::IndicatorValue> completed;
                std::string calculationError;
                if (!indicators::CalculateBatch(
                        instance,
                        *source.completedBars,
                        completed,
                        calculationError))
                {
                    error = spec.id + ": " + calculationError;
                    state_ = IndicatorModuleState::Error;
                    error_ = error;
                    return false;
                }

                Runtime runtime;
                runtime.spec = spec;
                runtime.instance = std::move(instance);
                runtime.completedValues =
                    std::make_shared<const std::vector<
                        indicators::IndicatorValue>>(
                        std::move(completed));

                if (source.hasLiveBar) {
                    runtime.liveValue =
                        runtime.instance.Update(source.liveBar);
                    if (
                        runtime.liveValue.fault !=
                        indicators::IndicatorFault::None)
                    {
                        error =
                            spec.id + ": " +
                            indicators::IndicatorFaultMessage(
                                runtime.liveValue.fault);
                        state_ = IndicatorModuleState::Error;
                        error_ = error;
                        return false;
                    }
                    runtime.hasLiveValue = true;
                }

                candidates.push_back(std::move(runtime));
            }

            runtimes_ = std::move(candidates);
        }
        else if (source.hasLiveBar) {
            for (Runtime& runtime : runtimes_) {
                const indicators::IndicatorValue value =
                    runtime.instance.Update(source.liveBar);
                if (value.fault != indicators::IndicatorFault::None) {
                    error =
                        runtime.spec.id + ": " +
                        indicators::IndicatorFaultMessage(value.fault);
                    state_ = IndicatorModuleState::Error;
                    error_ = error;
                    return false;
                }
                runtime.liveValue = value;
                runtime.hasLiveValue = true;
            }
            ++metrics_.mergedEventCount;
        }

        symbol_ = source.symbol;
        sourceRevision_ = source.revision;
        completedRevision_ = source.completedRevision;
        ++calculationRevision_;
        ++metrics_.eventCount;
        metrics_.lastProcessingMicros = ElapsedMicros(started);
        metrics_.maxProcessingMicros = (std::max)(
            metrics_.maxProcessingMicros,
            metrics_.lastProcessingMicros);
        metrics_.symbolCount = 1;
        metrics_.renderSeriesCount = 0;
        for (const Runtime& runtime : runtimes_) {
            const indicators::IndicatorValue* value = nullptr;
            if (runtime.hasLiveValue) {
                value = &runtime.liveValue;
            }
            else if (
                runtime.completedValues &&
                !runtime.completedValues->empty())
            {
                value = &runtime.completedValues->back();
            }
            if (value != nullptr) {
                metrics_.renderSeriesCount += value->outputCount;
            }
        }
        metrics_.retainedBytes = EstimateRetainedBytesLocked();
        state_ = IndicatorModuleState::Ready;
        error_.clear();
        error.clear();
        return true;
    }

    IndicatorModuleSnapshot IndicatorModule::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        IndicatorModuleSnapshot result;
        result.state = state_;
        result.level = level_;
        result.symbol = symbol_;
        result.sourceRevision = sourceRevision_;
        result.completedRevision = completedRevision_;
        result.calculationRevision = calculationRevision_;
        result.metrics = metrics_;
        result.error = error_;
        result.series.reserve(runtimes_.size());

        for (const Runtime& runtime : runtimes_) {
            IndicatorSeriesSnapshot series;
            series.spec = runtime.spec;
            series.completedValues = runtime.completedValues;
            series.liveValue = runtime.liveValue;
            series.hasLiveValue = runtime.hasLiveValue;
            series.retainedBytes = runtime.instance.RetainedBytes();
            if (runtime.completedValues) {
                series.retainedBytes +=
                    runtime.completedValues->capacity() *
                    sizeof(indicators::IndicatorValue);
            }
            result.series.push_back(std::move(series));
        }
        return result;
    }

    const char* IndicatorModule::StateName(
        IndicatorModuleState state) noexcept
    {
        switch (state) {
        case IndicatorModuleState::Ready: return "Ready";
        case IndicatorModuleState::Error: return "Error";
        default: return "Empty";
        }
    }

    bool IndicatorModule::BuildRuntimesLocked(std::string& error)
    {
        std::vector<Runtime> candidates;
        candidates.reserve(specs_.size());
        for (const indicators::IndicatorSpec& spec : specs_) {
            std::string createError;
            indicators::IndicatorInstance instance =
                registry_.Create(spec, createError);
            if (!instance.IsValid()) {
                error = createError;
                return false;
            }

            Runtime runtime;
            runtime.spec = spec;
            runtime.instance = std::move(instance);
            runtime.completedValues =
                std::make_shared<const std::vector<
                    indicators::IndicatorValue>>();
            candidates.push_back(std::move(runtime));
        }
        runtimes_ = std::move(candidates);
        error.clear();
        return true;
    }

    void IndicatorModule::ClearCalculatedStateLocked() noexcept
    {
        runtimes_.clear();
        symbol_.clear();
        sourceRevision_ = 0;
        completedRevision_ = 0;
        ++calculationRevision_;
        metrics_.queueDepth = 0;
        metrics_.retainedBytes = 0;
        metrics_.symbolCount = 0;
        metrics_.renderSeriesCount = 0;
    }

    std::size_t IndicatorModule::EstimateRetainedBytesLocked() const noexcept
    {
        std::size_t bytes =
            specs_.capacity() * sizeof(indicators::IndicatorSpec) +
            runtimes_.capacity() * sizeof(Runtime) +
            symbol_.capacity() +
            error_.capacity();

        for (const indicators::IndicatorSpec& spec : specs_) {
            bytes += spec.id.capacity();
            bytes += spec.type.capacity();
            bytes +=
                spec.parameters.size() *
                sizeof(std::pair<const std::string, double>);
            for (const auto& parameter : spec.parameters) {
                bytes += parameter.first.capacity();
            }
        }

        for (const Runtime& runtime : runtimes_) {
            bytes += runtime.instance.RetainedBytes();
            if (runtime.completedValues) {
                bytes +=
                    runtime.completedValues->capacity() *
                    sizeof(indicators::IndicatorValue);
            }
        }
        return bytes;
    }
}
