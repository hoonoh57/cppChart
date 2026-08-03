#include "jma_indicator.h"

#include <cmath>
#include <cstddef>
#include <new>
#include <string>

namespace trading::indicators
{
    namespace
    {
        struct JmaCalculationState final
        {
            double e0 = 0.0;
            double e1 = 0.0;
            double e2 = 0.0;
            double lastJma = 0.0;
            double warmSum = 0.0;
            int count = 0;
            bool initialized = false;
        };

        struct JmaState final
        {
            int period = 0;
            int phase = 0;
            int power = 0;
            JmaCalculationState current;
            JmaCalculationState beforeLatest;
            EpochMillis latestTimestampMs = 0;
            bool hasTimestamp = false;
        };

        double RoundTo(double value, double scale) noexcept
        {
            return std::nearbyint(value * scale) / scale;
        }

        double StepJma(
            JmaCalculationState& state,
            int period,
            int phase,
            int power,
            double source) noexcept
        {
            if (!state.initialized) {
                state.e0 = source;
                state.e1 = 0.0;
                state.e2 = 0.0;
                state.lastJma = source;
                state.initialized = true;
            }

            const double periodTerm = 0.45 * static_cast<double>(period - 1);
            const double beta = periodTerm / (periodTerm + 2.0);
            const double alpha = std::pow(beta, static_cast<double>(power));
            const double oneMinusAlpha = 1.0 - alpha;

            state.e0 = oneMinusAlpha * source + alpha * state.e0;
            state.e1 =
                (source - state.e0) * (1.0 - beta) + beta * state.e1;
            state.e2 =
                (state.e0 +
                    (static_cast<double>(phase) / 100.0 + 1.5) * state.e1 -
                    state.lastJma) *
                    oneMinusAlpha * oneMinusAlpha +
                alpha * alpha * state.e2;

            ++state.count;
            state.warmSum += source;
            const double current = state.count <= period
                ? RoundTo(
                    state.warmSum / static_cast<double>(state.count),
                    10000.0)
                : RoundTo(state.e2 + state.lastJma, 10000.0);
            state.lastJma = current;
            return current;
        }

        void ResetJma(void* opaque) noexcept
        {
            JmaState& state = *static_cast<JmaState*>(opaque);
            state.current = {};
            state.beforeLatest = {};
            state.latestTimestampMs = 0;
            state.hasTimestamp = false;
        }

        IndicatorValue UpdateJma(
            void* opaque,
            const Bar& bar) noexcept
        {
            JmaState& state = *static_cast<JmaState*>(opaque);

            IndicatorValue result;
            result.timestampMs = bar.closeTimestampMs;

            if (!IsValidPrice(bar.close) || bar.closeTimestampMs <= 0) {
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

            result.value = StepJma(
                state.current,
                state.period,
                state.phase,
                state.power,
                static_cast<double>(bar.close));
            result.ready = true;
            return result;
        }

        void DestroyJma(void* opaque) noexcept
        {
            delete static_cast<JmaState*>(opaque);
        }

        std::size_t JmaRetainedBytes(const void*) noexcept
        {
            return sizeof(JmaState);
        }

        IndicatorInstance CreateJma(
            const IndicatorSpec& spec,
            std::string& error)
        {
            error.clear();
            if (
                spec.parameters.size() != 3U ||
                spec.parameters.find("period") == spec.parameters.end() ||
                spec.parameters.find("phase") == spec.parameters.end() ||
                spec.parameters.find("power") == spec.parameters.end())
            {
                error = "JMA requires only period, phase, and power parameters";
                return {};
            }

            int period = 0;
            int phase = 0;
            int power = 0;
            if (!TryGetIntegerParameter(
                    spec,
                    "period",
                    1,
                    10000,
                    period,
                    error) ||
                !TryGetIntegerParameter(
                    spec,
                    "phase",
                    -100,
                    100,
                    phase,
                    error) ||
                !TryGetIntegerParameter(
                    spec,
                    "power",
                    1,
                    10000,
                    power,
                    error))
            {
                return {};
            }

            try {
                JmaState* state = new JmaState();
                state->period = period;
                state->phase = phase;
                state->power = power;
                return IndicatorInstance(
                    "JMA",
                    state,
                    &ResetJma,
                    &UpdateJma,
                    &DestroyJma,
                    &JmaRetainedBytes);
            }
            catch (const std::bad_alloc&) {
                error = "JMA state allocation failed";
                return {};
            }
        }
    }

    bool RegisterJmaIndicator(IndicatorRegistry& registry)
    {
        return registry.Register("JMA", &CreateJma);
    }
}
