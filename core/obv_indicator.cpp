#include "obv_indicator.h"

#include <cstddef>
#include <new>
#include <string>

namespace trading::indicators
{
    namespace
    {
        struct ObvCalculationState final
        {
            PriceWon previousClose = 0;
            long double obv = 0.0L;
            bool hasPrevious = false;
        };

        struct ObvState final
        {
            ObvCalculationState current;
            ObvCalculationState beforeLatest;
            EpochMillis latestTimestampMs = 0;
            bool hasTimestamp = false;
        };

        double StepObv(
            ObvCalculationState& state,
            const Bar& bar) noexcept
        {
            if (!state.hasPrevious) {
                state.obv = static_cast<long double>(bar.volume);
                state.hasPrevious = true;
            }
            else if (bar.close > state.previousClose) {
                state.obv += static_cast<long double>(bar.volume);
            }
            else if (bar.close < state.previousClose) {
                state.obv -= static_cast<long double>(bar.volume);
            }

            state.previousClose = bar.close;
            return static_cast<double>(state.obv);
        }

        void ResetObv(void* opaque) noexcept
        {
            ObvState& state = *static_cast<ObvState*>(opaque);
            state.current = {};
            state.beforeLatest = {};
            state.latestTimestampMs = 0;
            state.hasTimestamp = false;
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
                state.current = state.beforeLatest;
                result.replaced = true;
            }
            else {
                state.beforeLatest = state.current;
                state.latestTimestampMs = bar.closeTimestampMs;
                state.hasTimestamp = true;
            }

            result.value = StepObv(state.current, bar);
            result.ready = true;
            return result;
        }

        void DestroyObv(void* opaque) noexcept
        {
            delete static_cast<ObvState*>(opaque);
        }

        std::size_t ObvRetainedBytes(const void*) noexcept
        {
            return sizeof(ObvState);
        }

        IndicatorInstance CreateObv(
            const IndicatorSpec& spec,
            std::string& error)
        {
            error.clear();
            if (!spec.parameters.empty()) {
                error = "OBV does not accept parameters";
                return {};
            }

            try {
                ObvState* state = new ObvState();
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
