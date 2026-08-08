#include "vwap_indicator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <string>

namespace trading::indicators
{
    namespace
    {
        struct VwapCalculationState final
        {
            double priceVolume = 0.0;
            double volume = 0.0;
            double priceSquaredVolume = 0.0;
            TradingDateYmd lastTradingDateYmd = 0;
        };

        struct VwapState final
        {
            double stdDev1 = 0.0;
            double stdDev2 = 0.0;
            VwapCalculationState current;
            VwapCalculationState beforeLatest;
            EpochMillis latestTimestampMs = 0;
            bool hasTimestamp = false;
        };

        bool IsValidBar(const Bar& bar) noexcept
        {
            return
                IsValidPrice(bar.open) &&
                IsValidPrice(bar.high) &&
                IsValidPrice(bar.low) &&
                IsValidPrice(bar.close) &&
                bar.high >= bar.open &&
                bar.high >= bar.close &&
                bar.low <= bar.open &&
                bar.low <= bar.close &&
                bar.high >= bar.low &&
                bar.volume >= 0 &&
                bar.closeTimestampMs > 0 &&
                IsValidTradingDateYmd(bar.tradingDateYmd);
        }

        bool TryGetDoubleParameter(
            const IndicatorSpec& spec,
            const std::string& key,
            double minimum,
            double maximum,
            double& value,
            std::string& error)
        {
            const auto found = spec.parameters.find(key);
            if (found == spec.parameters.end()) {
                error = "missing indicator parameter: " + key;
                return false;
            }

            const double number = found->second;
            if (
                !std::isfinite(number) ||
                number < minimum ||
                number > maximum)
            {
                error = "invalid numeric indicator parameter: " + key;
                return false;
            }

            value = number;
            return true;
        }

        void SetMissingOutputs(IndicatorValue& result) noexcept
        {
            const double missing =
                (std::numeric_limits<double>::quiet_NaN)();
            result.SetOutput(VwapValueOutput, missing);
            result.SetOutput(VwapUpper1Output, missing);
            result.SetOutput(VwapLower1Output, missing);
            result.SetOutput(VwapUpper2Output, missing);
            result.SetOutput(VwapLower2Output, missing);
        }

        void StepVwap(
            VwapCalculationState& state,
            double stdDev1,
            double stdDev2,
            const Bar& bar,
            IndicatorValue& result) noexcept
        {
            if (
                state.lastTradingDateYmd != 0 &&
                bar.tradingDateYmd != state.lastTradingDateYmd)
            {
                state.priceVolume = 0.0;
                state.volume = 0.0;
                state.priceSquaredVolume = 0.0;
            }

            state.lastTradingDateYmd = bar.tradingDateYmd;
            const double typicalPrice =
                (static_cast<double>(bar.high) +
                    static_cast<double>(bar.low) +
                    static_cast<double>(bar.close)) /
                3.0;
            const double volume = static_cast<double>(bar.volume);
            state.priceVolume += typicalPrice * volume;
            state.volume += volume;
            state.priceSquaredVolume +=
                typicalPrice * typicalPrice * volume;

            if (state.volume <= 0.0) {
                SetMissingOutputs(result);
                return;
            }

            const double vwap = state.priceVolume / state.volume;
            const double variance = (std::max)(
                0.0,
                state.priceSquaredVolume / state.volume - vwap * vwap);
            const double deviation = std::sqrt(variance);

            result.SetOutput(
                VwapValueOutput,
                static_cast<double>(static_cast<float>(vwap)));
            result.SetOutput(
                VwapUpper1Output,
                static_cast<double>(static_cast<float>(
                    vwap + stdDev1 * deviation)));
            result.SetOutput(
                VwapLower1Output,
                static_cast<double>(static_cast<float>(
                    vwap - stdDev1 * deviation)));
            result.SetOutput(
                VwapUpper2Output,
                static_cast<double>(static_cast<float>(
                    vwap + stdDev2 * deviation)));
            result.SetOutput(
                VwapLower2Output,
                static_cast<double>(static_cast<float>(
                    vwap - stdDev2 * deviation)));
        }

        void ResetVwap(void* opaque) noexcept
        {
            VwapState& state = *static_cast<VwapState*>(opaque);
            state.current = {};
            state.beforeLatest = {};
            state.latestTimestampMs = 0;
            state.hasTimestamp = false;
        }

        IndicatorValue UpdateVwap(
            void* opaque,
            const Bar& bar) noexcept
        {
            VwapState& state = *static_cast<VwapState*>(opaque);

            IndicatorValue result;
            result.timestampMs = bar.closeTimestampMs;

            if (!IsValidBar(bar)) {
                result.fault = IndicatorFault::InvalidInput;
                return result;
            }
            if (
                state.hasTimestamp &&
                bar.closeTimestampMs < state.latestTimestampMs)
            {
                result.fault = IndicatorFault::TimestampMovedBackward;
                return result;
            }
            if (
                state.current.lastTradingDateYmd != 0 &&
                bar.tradingDateYmd < state.current.lastTradingDateYmd)
            {
                result.fault = IndicatorFault::InvalidInput;
                return result;
            }

            if (
                state.hasTimestamp &&
                bar.closeTimestampMs == state.latestTimestampMs)
            {
                state.current = state.beforeLatest;
                result.replaced = true;
            }
            else {
                state.beforeLatest = state.current;
                state.latestTimestampMs = bar.closeTimestampMs;
                state.hasTimestamp = true;
            }

            StepVwap(
                state.current,
                state.stdDev1,
                state.stdDev2,
                bar,
                result);
            return result;
        }

        void DestroyVwap(void* opaque) noexcept
        {
            delete static_cast<VwapState*>(opaque);
        }

        std::size_t VwapRetainedBytes(const void*) noexcept
        {
            return sizeof(VwapState);
        }

        IndicatorInstance CreateVwap(
            const IndicatorSpec& spec,
            std::string& error)
        {
            error.clear();
            if (
                spec.parameters.size() != 2U ||
                spec.parameters.find("std_dev_1") == spec.parameters.end() ||
                spec.parameters.find("std_dev_2") == spec.parameters.end())
            {
                error = "VWAP requires only std_dev_1 and std_dev_2 parameters";
                return {};
            }

            double stdDev1 = 0.0;
            double stdDev2 = 0.0;
            if (!TryGetDoubleParameter(
                    spec,
                    "std_dev_1",
                    0.0,
                    100.0,
                    stdDev1,
                    error) ||
                !TryGetDoubleParameter(
                    spec,
                    "std_dev_2",
                    0.0,
                    100.0,
                    stdDev2,
                    error))
            {
                return {};
            }

            try {
                VwapState* state = new VwapState();
                state->stdDev1 = stdDev1;
                state->stdDev2 = stdDev2;
                return IndicatorInstance(
                    "VWAP",
                    state,
                    &ResetVwap,
                    &UpdateVwap,
                    &DestroyVwap,
                    &VwapRetainedBytes);
            }
            catch (const std::bad_alloc&) {
                error = "VWAP state allocation failed";
                return {};
            }
        }
    }

    bool RegisterVwapIndicator(IndicatorRegistry& registry)
    {
        return registry.Register("VWAP", &CreateVwap);
    }
}
