#include "obv_indicator.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace trading::indicators
{
    namespace
    {
        struct ObvCalculationState final
        {
            PriceWon previousClose = 0;
            double obv = 0.0;
            double signalSum = 0.0;
            std::size_t signalHead = 0;
            std::size_t signalCount = 0;
            bool hasPrevious = false;
        };

        struct ObvState final
        {
            explicit ObvState(int requestedSignalPeriod)
                : signalPeriod(requestedSignalPeriod),
                  signalValues(
                      static_cast<std::size_t>(requestedSignalPeriod),
                      0.0)
            {
            }

            int signalPeriod = 0;
            std::vector<double> signalValues;
            ObvCalculationState current;
            ObvCalculationState beforeLatest;
            double savedSignalHeadValue = 0.0;
            EpochMillis latestTimestampMs = 0;
            bool hasTimestamp = false;
        };

        void SaveBeforeLatest(ObvState& state) noexcept
        {
            state.beforeLatest = state.current;
            if (
                state.current.signalCount == state.signalValues.size() &&
                !state.signalValues.empty())
            {
                state.savedSignalHeadValue =
                    state.signalValues[state.current.signalHead];
            }
            else {
                state.savedSignalHeadValue = 0.0;
            }
        }

        void RestoreBeforeLatest(ObvState& state) noexcept
        {
            state.current = state.beforeLatest;
            if (
                state.current.signalCount == state.signalValues.size() &&
                !state.signalValues.empty())
            {
                state.signalValues[state.current.signalHead] =
                    state.savedSignalHeadValue;
            }
        }

        void PushSignal(ObvState& state, double value) noexcept
        {
            ObvCalculationState& calculation = state.current;
            const std::size_t period = state.signalValues.size();

            if (calculation.signalCount < period) {
                const std::size_t slot =
                    (calculation.signalHead + calculation.signalCount) % period;
                state.signalValues[slot] = value;
                ++calculation.signalCount;
                calculation.signalSum += value;
            }
            else {
                calculation.signalSum +=
                    value - state.signalValues[calculation.signalHead];
                state.signalValues[calculation.signalHead] = value;
                calculation.signalHead =
                    (calculation.signalHead + 1U) % period;
            }
        }

        void StepObv(
            ObvState& state,
            const Bar& bar,
            IndicatorValue& result) noexcept
        {
            ObvCalculationState& calculation = state.current;
            if (!calculation.hasPrevious) {
                calculation.obv = static_cast<double>(bar.volume);
                calculation.hasPrevious = true;
            }
            else if (bar.close > calculation.previousClose) {
                calculation.obv += static_cast<double>(bar.volume);
            }
            else if (bar.close < calculation.previousClose) {
                calculation.obv -= static_cast<double>(bar.volume);
            }

            calculation.previousClose = bar.close;
            PushSignal(state, calculation.obv);

            const float value = static_cast<float>(calculation.obv);
            const bool signalReady =
                calculation.signalCount == state.signalValues.size();
            const float signal = signalReady
                ? static_cast<float>(
                    calculation.signalSum /
                    static_cast<double>(state.signalPeriod))
                : (std::numeric_limits<float>::quiet_NaN)();
            const float direction = std::isnan(signal)
                ? (std::numeric_limits<float>::quiet_NaN)()
                : value > signal ? 1.0f : -1.0f;

            result.SetOutput(ObvValueOutput, static_cast<double>(value));
            result.SetOutput(ObvSignalOutput, static_cast<double>(signal));
            result.SetOutput(
                ObvDirectionOutput,
                static_cast<double>(direction));
        }

        void ResetObv(void* opaque) noexcept
        {
            ObvState& state = *static_cast<ObvState*>(opaque);
            state.current = {};
            state.beforeLatest = {};
            state.savedSignalHeadValue = 0.0;
            state.latestTimestampMs = 0;
            state.hasTimestamp = false;
            for (double& value : state.signalValues) value = 0.0;
        }

        IndicatorValue UpdateObv(
            void* opaque,
            const Bar& bar) noexcept
        {
            ObvState& state = *static_cast<ObvState*>(opaque);

            IndicatorValue result;
            result.timestampMs = bar.closeTimestampMs;

            if (
                !IsValidPrice(bar.close) ||
                bar.volume < 0 ||
                bar.closeTimestampMs <= 0)
            {
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
                state.hasTimestamp &&
                bar.closeTimestampMs == state.latestTimestampMs)
            {
                RestoreBeforeLatest(state);
                result.replaced = true;
            }
            else {
                SaveBeforeLatest(state);
                state.latestTimestampMs = bar.closeTimestampMs;
                state.hasTimestamp = true;
            }

            StepObv(state, bar, result);
            if (!std::isfinite(result.Value(ObvValueOutput))) {
                RestoreBeforeLatest(state);
                result = {};
                result.timestampMs = bar.closeTimestampMs;
                result.fault = IndicatorFault::InvalidInput;
            }
            return result;
        }

        void DestroyObv(void* opaque) noexcept
        {
            delete static_cast<ObvState*>(opaque);
        }

        std::size_t ObvRetainedBytes(const void* opaque) noexcept
        {
            const ObvState& state = *static_cast<const ObvState*>(opaque);
            return
                sizeof(ObvState) +
                state.signalValues.capacity() * sizeof(double);
        }

        IndicatorInstance CreateObv(
            const IndicatorSpec& spec,
            std::string& error)
        {
            error.clear();
            if (
                spec.parameters.size() != 1U ||
                spec.parameters.find("signal_period") ==
                    spec.parameters.end())
            {
                error = "OBV requires only the signal_period parameter";
                return {};
            }

            int signalPeriod = 0;
            if (!TryGetIntegerParameter(
                    spec,
                    "signal_period",
                    1,
                    10000,
                    signalPeriod,
                    error))
            {
                return {};
            }

            try {
                ObvState* state = new ObvState(signalPeriod);
                return IndicatorInstance(
                    "OBV",
                    state,
                    &ResetObv,
                    &UpdateObv,
                    &DestroyObv,
                    &ObvRetainedBytes);
            }
            catch (const std::bad_alloc&) {
                error = "OBV state allocation failed";
                return {};
            }
        }
    }

    bool RegisterObvIndicator(IndicatorRegistry& registry)
    {
        return registry.Register("OBV", &CreateObv);
    }
}
