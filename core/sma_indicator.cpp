#include "sma_indicator.h"

#include <algorithm>
#include <cstddef>
#include <new>
#include <utility>
#include <vector>

namespace trading::indicators
{
    namespace
    {
        struct SmaState final
        {
            explicit SmaState(int requestedPeriod)
                : period(requestedPeriod),
                  window(static_cast<std::size_t>(requestedPeriod), 0.0)
            {
            }

            int period = 0;
            std::vector<double> window;
            std::size_t count = 0;
            std::size_t nextIndex = 0;
            std::size_t latestIndex = 0;
            double sum = 0.0;
            EpochMillis latestTimestampMs = 0;
            bool hasTimestamp = false;
        };

        void ResetSma(void* opaque) noexcept
        {
            SmaState& state = *static_cast<SmaState*>(opaque);
            std::fill(state.window.begin(), state.window.end(), 0.0);
            state.count = 0;
            state.nextIndex = 0;
            state.latestIndex = 0;
            state.sum = 0.0;
            state.latestTimestampMs = 0;
            state.hasTimestamp = false;
        }

        IndicatorValue UpdateSma(
            void* opaque,
            const Bar& bar) noexcept
        {
            SmaState& state = *static_cast<SmaState*>(opaque);

            IndicatorValue result;
            result.timestampMs = bar.closeTimestampMs;
            result.outputCount = 1;

            if (
                !IsValidPrice(bar.close) ||
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

            const double close = static_cast<double>(bar.close);
            if (
                state.hasTimestamp &&
                bar.closeTimestampMs == state.latestTimestampMs)
            {
                state.sum += close - state.window[state.latestIndex];
                state.window[state.latestIndex] = close;
                result.replaced = true;
            }
            else {
                if (state.count < state.window.size()) {
                    state.latestIndex = state.count;
                    state.window[state.latestIndex] = close;
                    state.sum += close;
                    ++state.count;
                    state.nextIndex = state.count % state.window.size();
                }
                else {
                    state.latestIndex = state.nextIndex;
                    state.sum += close - state.window[state.latestIndex];
                    state.window[state.latestIndex] = close;
                    state.nextIndex =
                        (state.nextIndex + 1U) % state.window.size();
                }

                state.latestTimestampMs = bar.closeTimestampMs;
                state.hasTimestamp = true;
            }

            if (state.count == static_cast<std::size_t>(state.period)) {
                result.SetOutput(
                    0,
                    state.sum / static_cast<double>(state.period));
            }
            return result;
        }

        void DestroySma(void* opaque) noexcept
        {
            delete static_cast<SmaState*>(opaque);
        }

        std::size_t SmaRetainedBytes(const void* opaque) noexcept
        {
            const SmaState& state = *static_cast<const SmaState*>(opaque);
            return
                sizeof(SmaState) +
                state.window.capacity() * sizeof(double);
        }

        IndicatorInstance CreateSma(
            const IndicatorSpec& spec,
            std::string& error)
        {
            error.clear();
            if (
                spec.parameters.size() != 1U ||
                spec.parameters.find("period") == spec.parameters.end())
            {
                error = "SMA requires only the period parameter";
                return {};
            }

            int period = 0;
            if (!TryGetIntegerParameter(
                    spec,
                    "period",
                    1,
                    100000,
                    period,
                    error))
            {
                return {};
            }

            try {
                SmaState* state = new SmaState(period);
                return IndicatorInstance(
                    "SMA",
                    state,
                    &ResetSma,
                    &UpdateSma,
                    &DestroySma,
                    &SmaRetainedBytes);
            }
            catch (const std::bad_alloc&) {
                error = "SMA state allocation failed";
                return {};
            }
        }
    }

    bool RegisterSmaIndicator(IndicatorRegistry& registry)
    {
        return registry.Register("SMA", &CreateSma);
    }
}
