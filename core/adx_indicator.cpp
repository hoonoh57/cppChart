#include "adx_indicator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <new>
#include <string>

namespace trading::indicators
{
    namespace
    {
        struct AdxCalculationState final
        {
            PriceWon previousHigh = 0;
            PriceWon previousLow = 0;
            PriceWon previousClose = 0;
            bool hasPrevious = false;
            int transitionCount = 0;
            double smoothedTrueRange = 0.0;
            double smoothedPlusDm = 0.0;
            double smoothedMinusDm = 0.0;
            int dxCount = 0;
            double dxSum = 0.0;
            double adx = 0.0;
            bool adxReady = false;
        };

        struct AdxState final
        {
            int period = 0;
            AdxCalculationState current;
            AdxCalculationState beforeLatest;
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
                bar.closeTimestampMs > 0;
        }

        double CalculateDx(
            double smoothedTrueRange,
            double smoothedPlusDm,
            double smoothedMinusDm) noexcept
        {
            if (smoothedTrueRange <= 0.0) return 0.0;

            const double plusDi =
                100.0 * smoothedPlusDm / smoothedTrueRange;
            const double minusDi =
                100.0 * smoothedMinusDm / smoothedTrueRange;
            const double denominator = plusDi + minusDi;
            if (denominator <= 0.0) return 0.0;
            return 100.0 * std::fabs(plusDi - minusDi) / denominator;
        }

        void ConsumeDx(
            AdxCalculationState& state,
            int period,
            double dx) noexcept
        {
            if (!state.adxReady) {
                state.dxSum += dx;
                ++state.dxCount;
                if (state.dxCount == period) {
                    state.adx =
                        state.dxSum / static_cast<double>(period);
                    state.adxReady = true;
                }
                return;
            }

            state.adx =
                (state.adx * static_cast<double>(period - 1) + dx) /
                static_cast<double>(period);
        }

        void StepAdx(
            AdxCalculationState& state,
            int period,
            const Bar& bar,
            bool& ready,
            double& value) noexcept
        {
            ready = false;
            value = 0.0;

            if (!state.hasPrevious) {
                state.previousHigh = bar.high;
                state.previousLow = bar.low;
                state.previousClose = bar.close;
                state.hasPrevious = true;
                return;
            }

            const double upMove =
                static_cast<double>(bar.high) -
                static_cast<double>(state.previousHigh);
            const double downMove =
                static_cast<double>(state.previousLow) -
                static_cast<double>(bar.low);
            const double plusDm =
                upMove > downMove && upMove > 0.0 ? upMove : 0.0;
            const double minusDm =
                downMove > upMove && downMove > 0.0 ? downMove : 0.0;
            const double highLow =
                static_cast<double>(bar.high) -
                static_cast<double>(bar.low);
            const double highClose = std::fabs(
                static_cast<double>(bar.high) -
                static_cast<double>(state.previousClose));
            const double lowClose = std::fabs(
                static_cast<double>(bar.low) -
                static_cast<double>(state.previousClose));
            const double trueRange =
                (std::max)(highLow, (std::max)(highClose, lowClose));

            if (state.transitionCount < period) {
                state.smoothedTrueRange += trueRange;
                state.smoothedPlusDm += plusDm;
                state.smoothedMinusDm += minusDm;
                ++state.transitionCount;

                if (state.transitionCount == period) {
                    ConsumeDx(
                        state,
                        period,
                        CalculateDx(
                            state.smoothedTrueRange,
                            state.smoothedPlusDm,
                            state.smoothedMinusDm));
                }
            }
            else {
                const double divisor = static_cast<double>(period);
                state.smoothedTrueRange =
                    state.smoothedTrueRange -
                    state.smoothedTrueRange / divisor +
                    trueRange;
                state.smoothedPlusDm =
                    state.smoothedPlusDm -
                    state.smoothedPlusDm / divisor +
                    plusDm;
                state.smoothedMinusDm =
                    state.smoothedMinusDm -
                    state.smoothedMinusDm / divisor +
                    minusDm;
                ConsumeDx(
                    state,
                    period,
                    CalculateDx(
                        state.smoothedTrueRange,
                        state.smoothedPlusDm,
                        state.smoothedMinusDm));
            }

            state.previousHigh = bar.high;
            state.previousLow = bar.low;
            state.previousClose = bar.close;

            ready = state.adxReady;
            if (ready) value = state.adx;
        }

        void ResetAdx(void* opaque) noexcept
        {
            AdxState& state = *static_cast<AdxState*>(opaque);
            state.current = {};
            state.beforeLatest = {};
            state.latestTimestampMs = 0;
            state.hasTimestamp = false;
        }

        IndicatorValue UpdateAdx(
            void* opaque,
            const Bar& bar) noexcept
        {
            AdxState& state = *static_cast<AdxState*>(opaque);

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

            StepAdx(
                state.current,
                state.period,
                bar,
                result.ready,
                result.value);
            return result;
        }

        void DestroyAdx(void* opaque) noexcept
        {
            delete static_cast<AdxState*>(opaque);
        }

        std::size_t AdxRetainedBytes(const void*) noexcept
        {
            return sizeof(AdxState);
        }

        IndicatorInstance CreateAdx(
            const IndicatorSpec& spec,
            std::string& error)
        {
            error.clear();
            if (
                spec.parameters.size() != 1U ||
                spec.parameters.find("period") == spec.parameters.end())
            {
                error = "ADX requires only the period parameter";
                return {};
            }

            int period = 0;
            if (!TryGetIntegerParameter(
                    spec,
                    "period",
                    1,
                    10000,
                    period,
                    error))
            {
                return {};
            }

            try {
                AdxState* state = new AdxState();
                state->period = period;
                return IndicatorInstance(
                    "ADX",
                    state,
                    &ResetAdx,
                    &UpdateAdx,
                    &DestroyAdx,
                    &AdxRetainedBytes);
            }
            catch (const std::bad_alloc&) {
                error = "ADX state allocation failed";
                return {};
            }
        }
    }

    bool RegisterAdxIndicator(IndicatorRegistry& registry)
    {
        return registry.Register("ADX", &CreateAdx);
    }
}
