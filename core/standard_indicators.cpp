#include "standard_indicators.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <new>
#include <string>
#include <vector>

namespace trading::indicators
{
    namespace
    {
        bool ValidBar(const Bar& bar) noexcept
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

        bool ExactParameters(
            const IndicatorSpec& spec,
            std::initializer_list<const char*> keys,
            std::string& error)
        {
            if (spec.parameters.size() != keys.size()) {
                error = spec.type + " parameter set is invalid";
                return false;
            }
            for (const char* key : keys) {
                if (spec.parameters.find(key) == spec.parameters.end()) {
                    error = spec.type + " requires parameter: " + key;
                    return false;
                }
            }
            return true;
        }

        bool DecimalParameter(
            const IndicatorSpec& spec,
            const char* key,
            double minimum,
            double maximum,
            double& value,
            std::string& error)
        {
            const auto found = spec.parameters.find(key);
            if (found == spec.parameters.end() ||
                !std::isfinite(found->second) ||
                found->second < minimum ||
                found->second > maximum)
            {
                error =
                    spec.type + " parameter is outside the allowed range: " +
                    key;
                return false;
            }
            value = found->second;
            return true;
        }

        struct EmaAccumulator final
        {
            int period = 0;
            int count = 0;
            double seedSum = 0.0;
            double value = 0.0;
            bool ready = false;

            void Configure(int requestedPeriod) noexcept
            {
                period = requestedPeriod;
            }

            bool Step(double input) noexcept
            {
                if (ready) {
                    const double alpha =
                        2.0 / static_cast<double>(period + 1);
                    value += alpha * (input - value);
                    return true;
                }

                seedSum += input;
                ++count;
                if (count == period) {
                    value = seedSum / static_cast<double>(period);
                    ready = true;
                }
                return ready;
            }
        };

        template <typename T>
        bool BeginUpdate(
            T& state,
            const Bar& bar,
            IndicatorValue& result) noexcept
        {
            result.timestampMs = bar.closeTimestampMs;
            if (!ValidBar(bar)) {
                result.fault = IndicatorFault::InvalidInput;
                return false;
            }
            if (
                state.hasTimestamp &&
                bar.closeTimestampMs < state.latestTimestampMs)
            {
                result.fault = IndicatorFault::TimestampMovedBackward;
                return false;
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
            return true;
        }

        // EMA ----------------------------------------------------------------
        struct EmaCalculation final
        {
            EmaAccumulator ema;
        };

        struct EmaState final
        {
            EmaCalculation current;
            EmaCalculation beforeLatest;
            EpochMillis latestTimestampMs = 0;
            bool hasTimestamp = false;
        };

        void ResetEma(void* opaque) noexcept
        {
            EmaState& state = *static_cast<EmaState*>(opaque);
            const int period = state.current.ema.period;
            state = {};
            state.current.ema.Configure(period);
            state.beforeLatest.ema.Configure(period);
        }

        IndicatorValue UpdateEma(void* opaque, const Bar& bar) noexcept
        {
            EmaState& state = *static_cast<EmaState*>(opaque);
            IndicatorValue result;
            result.outputCount = 1;
            if (!BeginUpdate(state, bar, result)) return result;

            if (state.current.ema.Step(static_cast<double>(bar.close))) {
                result.SetOutput(EmaValueOutput, state.current.ema.value);
            }
            return result;
        }

        void DestroyEma(void* opaque) noexcept
        {
            delete static_cast<EmaState*>(opaque);
        }

        std::size_t EmaBytes(const void*) noexcept
        {
            return sizeof(EmaState);
        }

        IndicatorInstance CreateEma(
            const IndicatorSpec& spec,
            std::string& error)
        {
            if (!ExactParameters(spec, { "period" }, error)) return {};
            int period = 0;
            if (!TryGetIntegerParameter(
                    spec, "period", 1, 100000, period, error))
            {
                return {};
            }
            try {
                EmaState* state = new EmaState();
                state->current.ema.Configure(period);
                state->beforeLatest.ema.Configure(period);
                error.clear();
                return IndicatorInstance(
                    "EMA", state, &ResetEma, &UpdateEma,
                    &DestroyEma, &EmaBytes);
            }
            catch (const std::bad_alloc&) {
                error = "EMA state allocation failed";
                return {};
            }
        }

        // Bollinger Bands ----------------------------------------------------
        struct BollingerState final
        {
            int period = 0;
            double deviation = 0.0;
            std::vector<double> window;
            std::size_t count = 0;
            std::size_t nextIndex = 0;
            std::size_t latestIndex = 0;
            double sum = 0.0;
            double sumSquares = 0.0;
            EpochMillis latestTimestampMs = 0;
            bool hasTimestamp = false;
        };

        void ResetBollinger(void* opaque) noexcept
        {
            BollingerState& state = *static_cast<BollingerState*>(opaque);
            std::fill(state.window.begin(), state.window.end(), 0.0);
            state.count = 0;
            state.nextIndex = 0;
            state.latestIndex = 0;
            state.sum = 0.0;
            state.sumSquares = 0.0;
            state.latestTimestampMs = 0;
            state.hasTimestamp = false;
        }

        IndicatorValue UpdateBollinger(
            void* opaque,
            const Bar& bar) noexcept
        {
            BollingerState& state =
                *static_cast<BollingerState*>(opaque);
            IndicatorValue result;
            result.timestampMs = bar.closeTimestampMs;
            result.outputCount = 3;
            if (!ValidBar(bar)) {
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
                const double previous = state.window[state.latestIndex];
                state.sum += close - previous;
                state.sumSquares += close * close - previous * previous;
                state.window[state.latestIndex] = close;
                result.replaced = true;
            }
            else {
                if (state.count < state.window.size()) {
                    state.latestIndex = state.count;
                    state.window[state.latestIndex] = close;
                    state.sum += close;
                    state.sumSquares += close * close;
                    ++state.count;
                    state.nextIndex = state.count % state.window.size();
                }
                else {
                    state.latestIndex = state.nextIndex;
                    const double previous = state.window[state.latestIndex];
                    state.sum += close - previous;
                    state.sumSquares += close * close - previous * previous;
                    state.window[state.latestIndex] = close;
                    state.nextIndex =
                        (state.nextIndex + 1U) % state.window.size();
                }
                state.latestTimestampMs = bar.closeTimestampMs;
                state.hasTimestamp = true;
            }

            if (state.count == state.window.size()) {
                const double divisor = static_cast<double>(state.period);
                const double middle = state.sum / divisor;
                const double variance = (std::max)(
                    0.0,
                    state.sumSquares / divisor - middle * middle);
                const double offset = state.deviation * std::sqrt(variance);
                result.SetOutput(BollingerMiddleOutput, middle);
                result.SetOutput(BollingerUpperOutput, middle + offset);
                result.SetOutput(BollingerLowerOutput, middle - offset);
            }
            return result;
        }

        void DestroyBollinger(void* opaque) noexcept
        {
            delete static_cast<BollingerState*>(opaque);
        }

        std::size_t BollingerBytes(const void* opaque) noexcept
        {
            const BollingerState& state =
                *static_cast<const BollingerState*>(opaque);
            return sizeof(BollingerState) +
                state.window.capacity() * sizeof(double);
        }

        IndicatorInstance CreateBollinger(
            const IndicatorSpec& spec,
            std::string& error)
        {
            if (!ExactParameters(
                    spec, { "period", "deviation" }, error))
            {
                return {};
            }
            int period = 0;
            double deviation = 0.0;
            if (!TryGetIntegerParameter(
                    spec, "period", 2, 100000, period, error) ||
                !DecimalParameter(
                    spec, "deviation", 0.0, 100.0, deviation, error))
            {
                return {};
            }
            try {
                BollingerState* state = new BollingerState();
                state->period = period;
                state->deviation = deviation;
                state->window.assign(
                    static_cast<std::size_t>(period), 0.0);
                error.clear();
                return IndicatorInstance(
                    "BOLLINGER", state, &ResetBollinger,
                    &UpdateBollinger, &DestroyBollinger,
                    &BollingerBytes);
            }
            catch (const std::bad_alloc&) {
                error = "Bollinger state allocation failed";
                return {};
            }
        }

        // RSI ----------------------------------------------------------------
        struct RsiCalculation final
        {
            PriceWon previousClose = 0;
            bool hasPrevious = false;
            int transitionCount = 0;
            double averageGain = 0.0;
            double averageLoss = 0.0;
            double seedGain = 0.0;
            double seedLoss = 0.0;
        };

        struct RsiState final
        {
            int period = 0;
            RsiCalculation current;
            RsiCalculation beforeLatest;
            EpochMillis latestTimestampMs = 0;
            bool hasTimestamp = false;
        };

        void ResetRsi(void* opaque) noexcept
        {
            RsiState& state = *static_cast<RsiState*>(opaque);
            const int period = state.period;
            state = {};
            state.period = period;
        }

        IndicatorValue UpdateRsi(void* opaque, const Bar& bar) noexcept
        {
            RsiState& state = *static_cast<RsiState*>(opaque);
            IndicatorValue result;
            result.outputCount = 1;
            if (!BeginUpdate(state, bar, result)) return result;

            RsiCalculation& calculation = state.current;
            if (!calculation.hasPrevious) {
                calculation.previousClose = bar.close;
                calculation.hasPrevious = true;
                return result;
            }

            const double change =
                static_cast<double>(bar.close) -
                static_cast<double>(calculation.previousClose);
            const double gain = change > 0.0 ? change : 0.0;
            const double loss = change < 0.0 ? -change : 0.0;
            if (calculation.transitionCount < state.period) {
                calculation.seedGain += gain;
                calculation.seedLoss += loss;
                ++calculation.transitionCount;
                if (calculation.transitionCount == state.period) {
                    calculation.averageGain =
                        calculation.seedGain /
                        static_cast<double>(state.period);
                    calculation.averageLoss =
                        calculation.seedLoss /
                        static_cast<double>(state.period);
                }
            }
            else {
                const double divisor = static_cast<double>(state.period);
                calculation.averageGain =
                    (calculation.averageGain * (divisor - 1.0) + gain) /
                    divisor;
                calculation.averageLoss =
                    (calculation.averageLoss * (divisor - 1.0) + loss) /
                    divisor;
            }
            calculation.previousClose = bar.close;

            if (calculation.transitionCount >= state.period) {
                double value = 100.0;
                if (calculation.averageLoss > 0.0) {
                    const double rs =
                        calculation.averageGain /
                        calculation.averageLoss;
                    value = 100.0 - 100.0 / (1.0 + rs);
                }
                else if (calculation.averageGain <= 0.0) {
                    value = 50.0;
                }
                result.SetOutput(RsiValueOutput, value);
            }
            return result;
        }

        void DestroyRsi(void* opaque) noexcept
        {
            delete static_cast<RsiState*>(opaque);
        }

        std::size_t RsiBytes(const void*) noexcept
        {
            return sizeof(RsiState);
        }

        IndicatorInstance CreateRsi(
            const IndicatorSpec& spec,
            std::string& error)
        {
            if (!ExactParameters(spec, { "period" }, error)) return {};
            int period = 0;
            if (!TryGetIntegerParameter(
                    spec, "period", 1, 10000, period, error))
            {
                return {};
            }
            try {
                RsiState* state = new RsiState();
                state->period = period;
                error.clear();
                return IndicatorInstance(
                    "RSI", state, &ResetRsi, &UpdateRsi,
                    &DestroyRsi, &RsiBytes);
            }
            catch (const std::bad_alloc&) {
                error = "RSI state allocation failed";
                return {};
            }
        }

        // MACD ---------------------------------------------------------------
        struct MacdCalculation final
        {
            EmaAccumulator fast;
            EmaAccumulator slow;
            EmaAccumulator signal;
        };

        struct MacdState final
        {
            int fastPeriod = 0;
            int slowPeriod = 0;
            int signalPeriod = 0;
            MacdCalculation current;
            MacdCalculation beforeLatest;
            EpochMillis latestTimestampMs = 0;
            bool hasTimestamp = false;
        };

        void ConfigureMacdCalculation(
            MacdCalculation& calculation,
            int fast,
            int slow,
            int signal) noexcept
        {
            calculation.fast.Configure(fast);
            calculation.slow.Configure(slow);
            calculation.signal.Configure(signal);
        }

        void ResetMacd(void* opaque) noexcept
        {
            MacdState& state = *static_cast<MacdState*>(opaque);
            const int fast = state.fastPeriod;
            const int slow = state.slowPeriod;
            const int signal = state.signalPeriod;
            state = {};
            state.fastPeriod = fast;
            state.slowPeriod = slow;
            state.signalPeriod = signal;
            ConfigureMacdCalculation(state.current, fast, slow, signal);
            ConfigureMacdCalculation(state.beforeLatest, fast, slow, signal);
        }

        IndicatorValue UpdateMacd(void* opaque, const Bar& bar) noexcept
        {
            MacdState& state = *static_cast<MacdState*>(opaque);
            IndicatorValue result;
            result.outputCount = 3;
            if (!BeginUpdate(state, bar, result)) return result;

            const double close = static_cast<double>(bar.close);
            const bool fastReady = state.current.fast.Step(close);
            const bool slowReady = state.current.slow.Step(close);
            if (!fastReady || !slowReady) return result;

            const double macd =
                state.current.fast.value - state.current.slow.value;
            result.SetOutput(MacdValueOutput, macd);
            if (state.current.signal.Step(macd)) {
                result.SetOutput(
                    MacdSignalOutput,
                    state.current.signal.value);
                result.SetOutput(
                    MacdHistogramOutput,
                    macd - state.current.signal.value);
            }
            return result;
        }

        void DestroyMacd(void* opaque) noexcept
        {
            delete static_cast<MacdState*>(opaque);
        }

        std::size_t MacdBytes(const void*) noexcept
        {
            return sizeof(MacdState);
        }

        IndicatorInstance CreateMacd(
            const IndicatorSpec& spec,
            std::string& error)
        {
            if (!ExactParameters(
                    spec,
                    { "fast_period", "slow_period", "signal_period" },
                    error))
            {
                return {};
            }
            int fast = 0;
            int slow = 0;
            int signal = 0;
            if (!TryGetIntegerParameter(
                    spec, "fast_period", 1, 10000, fast, error) ||
                !TryGetIntegerParameter(
                    spec, "slow_period", 2, 10000, slow, error) ||
                !TryGetIntegerParameter(
                    spec, "signal_period", 1, 10000, signal, error))
            {
                return {};
            }
            if (fast >= slow) {
                error = "MACD fast period must be smaller than slow period";
                return {};
            }
            try {
                MacdState* state = new MacdState();
                state->fastPeriod = fast;
                state->slowPeriod = slow;
                state->signalPeriod = signal;
                ConfigureMacdCalculation(state->current, fast, slow, signal);
                ConfigureMacdCalculation(
                    state->beforeLatest, fast, slow, signal);
                error.clear();
                return IndicatorInstance(
                    "MACD", state, &ResetMacd, &UpdateMacd,
                    &DestroyMacd, &MacdBytes);
            }
            catch (const std::bad_alloc&) {
                error = "MACD state allocation failed";
                return {};
            }
        }

        // DMI ----------------------------------------------------------------
        struct DmiCalculation final
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

        struct DmiState final
        {
            int period = 0;
            DmiCalculation current;
            DmiCalculation beforeLatest;
            EpochMillis latestTimestampMs = 0;
            bool hasTimestamp = false;
        };

        void ResetDmi(void* opaque) noexcept
        {
            DmiState& state = *static_cast<DmiState*>(opaque);
            const int period = state.period;
            state = {};
            state.period = period;
        }

        void ConsumeDmiDx(
            DmiCalculation& calculation,
            int period,
            double dx) noexcept
        {
            if (!calculation.adxReady) {
                calculation.dxSum += dx;
                ++calculation.dxCount;
                if (calculation.dxCount == period) {
                    calculation.adx =
                        calculation.dxSum /
                        static_cast<double>(period);
                    calculation.adxReady = true;
                }
                return;
            }
            calculation.adx =
                (calculation.adx * static_cast<double>(period - 1) + dx) /
                static_cast<double>(period);
        }

        IndicatorValue UpdateDmi(void* opaque, const Bar& bar) noexcept
        {
            DmiState& state = *static_cast<DmiState*>(opaque);
            IndicatorValue result;
            result.outputCount = 3;
            if (!BeginUpdate(state, bar, result)) return result;

            DmiCalculation& c = state.current;
            if (!c.hasPrevious) {
                c.previousHigh = bar.high;
                c.previousLow = bar.low;
                c.previousClose = bar.close;
                c.hasPrevious = true;
                return result;
            }

            const double upMove =
                static_cast<double>(bar.high) -
                static_cast<double>(c.previousHigh);
            const double downMove =
                static_cast<double>(c.previousLow) -
                static_cast<double>(bar.low);
            const double plusDm =
                upMove > downMove && upMove > 0.0 ? upMove : 0.0;
            const double minusDm =
                downMove > upMove && downMove > 0.0 ? downMove : 0.0;
            const double trueRange = (std::max)(
                static_cast<double>(bar.high - bar.low),
                (std::max)(
                    std::fabs(
                        static_cast<double>(bar.high) -
                        static_cast<double>(c.previousClose)),
                    std::fabs(
                        static_cast<double>(bar.low) -
                        static_cast<double>(c.previousClose))));

            if (c.transitionCount < state.period) {
                c.smoothedTrueRange += trueRange;
                c.smoothedPlusDm += plusDm;
                c.smoothedMinusDm += minusDm;
                ++c.transitionCount;
            }
            else {
                const double divisor = static_cast<double>(state.period);
                c.smoothedTrueRange =
                    c.smoothedTrueRange - c.smoothedTrueRange / divisor +
                    trueRange;
                c.smoothedPlusDm =
                    c.smoothedPlusDm - c.smoothedPlusDm / divisor + plusDm;
                c.smoothedMinusDm =
                    c.smoothedMinusDm - c.smoothedMinusDm / divisor + minusDm;
            }

            c.previousHigh = bar.high;
            c.previousLow = bar.low;
            c.previousClose = bar.close;

            if (c.transitionCount >= state.period &&
                c.smoothedTrueRange > 0.0)
            {
                const double plusDi =
                    100.0 * c.smoothedPlusDm / c.smoothedTrueRange;
                const double minusDi =
                    100.0 * c.smoothedMinusDm / c.smoothedTrueRange;
                result.SetOutput(DmiPlusOutput, plusDi);
                result.SetOutput(DmiMinusOutput, minusDi);
                const double denominator = plusDi + minusDi;
                const double dx = denominator > 0.0
                    ? 100.0 * std::fabs(plusDi - minusDi) / denominator
                    : 0.0;
                ConsumeDmiDx(c, state.period, dx);
                if (c.adxReady) {
                    result.SetOutput(DmiAdxOutput, c.adx);
                }
            }
            return result;
        }

        void DestroyDmi(void* opaque) noexcept
        {
            delete static_cast<DmiState*>(opaque);
        }

        std::size_t DmiBytes(const void*) noexcept
        {
            return sizeof(DmiState);
        }

        IndicatorInstance CreateDmi(
            const IndicatorSpec& spec,
            std::string& error)
        {
            if (!ExactParameters(spec, { "period" }, error)) return {};
            int period = 0;
            if (!TryGetIntegerParameter(
                    spec, "period", 1, 10000, period, error))
            {
                return {};
            }
            try {
                DmiState* state = new DmiState();
                state->period = period;
                error.clear();
                return IndicatorInstance(
                    "DMI", state, &ResetDmi, &UpdateDmi,
                    &DestroyDmi, &DmiBytes);
            }
            catch (const std::bad_alloc&) {
                error = "DMI state allocation failed";
                return {};
            }
        }

        // SuperTrend ---------------------------------------------------------
        struct SuperTrendCalculation final
        {
            PriceWon previousClose = 0;
            bool hasPrevious = false;
            int trueRangeCount = 0;
            double trueRangeSum = 0.0;
            double atr = 0.0;
            bool atrReady = false;
            double finalUpper = 0.0;
            double finalLower = 0.0;
            double superTrend = 0.0;
            bool trendReady = false;
            bool upTrend = true;
        };

        struct SuperTrendState final
        {
            int period = 0;
            double multiplier = 0.0;
            SuperTrendCalculation current;
            SuperTrendCalculation beforeLatest;
            EpochMillis latestTimestampMs = 0;
            bool hasTimestamp = false;
        };

        void ResetSuperTrend(void* opaque) noexcept
        {
            SuperTrendState& state =
                *static_cast<SuperTrendState*>(opaque);
            const int period = state.period;
            const double multiplier = state.multiplier;
            state = {};
            state.period = period;
            state.multiplier = multiplier;
        }

        IndicatorValue UpdateSuperTrend(
            void* opaque,
            const Bar& bar) noexcept
        {
            SuperTrendState& state =
                *static_cast<SuperTrendState*>(opaque);
            IndicatorValue result;
            result.outputCount = 3;
            if (!BeginUpdate(state, bar, result)) return result;

            SuperTrendCalculation& c = state.current;
            if (!c.hasPrevious) {
                c.previousClose = bar.close;
                c.hasPrevious = true;
                return result;
            }

            const double trueRange = (std::max)(
                static_cast<double>(bar.high - bar.low),
                (std::max)(
                    std::fabs(
                        static_cast<double>(bar.high) -
                        static_cast<double>(c.previousClose)),
                    std::fabs(
                        static_cast<double>(bar.low) -
                        static_cast<double>(c.previousClose))));

            if (!c.atrReady) {
                c.trueRangeSum += trueRange;
                ++c.trueRangeCount;
                if (c.trueRangeCount == state.period) {
                    c.atr =
                        c.trueRangeSum /
                        static_cast<double>(state.period);
                    c.atrReady = true;
                }
            }
            else {
                c.atr =
                    (c.atr * static_cast<double>(state.period - 1) +
                     trueRange) /
                    static_cast<double>(state.period);
            }

            if (c.atrReady) {
                const double midpoint =
                    (static_cast<double>(bar.high) +
                     static_cast<double>(bar.low)) * 0.5;
                const double basicUpper =
                    midpoint + state.multiplier * c.atr;
                const double basicLower =
                    midpoint - state.multiplier * c.atr;

                if (!c.trendReady) {
                    c.finalUpper = basicUpper;
                    c.finalLower = basicLower;
                    c.upTrend =
                        static_cast<double>(bar.close) >= midpoint;
                    c.superTrend =
                        c.upTrend ? c.finalLower : c.finalUpper;
                    c.trendReady = true;
                }
                else {
                    const double previousUpper = c.finalUpper;
                    const double previousLower = c.finalLower;
                    const double previousTrend = c.superTrend;
                    c.finalUpper =
                        basicUpper < previousUpper ||
                        static_cast<double>(c.previousClose) > previousUpper
                            ? basicUpper
                            : previousUpper;
                    c.finalLower =
                        basicLower > previousLower ||
                        static_cast<double>(c.previousClose) < previousLower
                            ? basicLower
                            : previousLower;

                    if (std::fabs(previousTrend - previousUpper) < 1e-9) {
                        c.upTrend =
                            static_cast<double>(bar.close) > c.finalUpper;
                    }
                    else {
                        c.upTrend =
                            static_cast<double>(bar.close) >= c.finalLower;
                    }
                    c.superTrend =
                        c.upTrend ? c.finalLower : c.finalUpper;
                }

                result.SetOutput(
                    SuperTrendValueOutput,
                    c.superTrend);
                if (c.upTrend) {
                    result.SetOutput(
                        SuperTrendUpOutput,
                        c.superTrend);
                }
                else {
                    result.SetOutput(
                        SuperTrendDownOutput,
                        c.superTrend);
                }
            }
            c.previousClose = bar.close;
            return result;
        }

        void DestroySuperTrend(void* opaque) noexcept
        {
            delete static_cast<SuperTrendState*>(opaque);
        }

        std::size_t SuperTrendBytes(const void*) noexcept
        {
            return sizeof(SuperTrendState);
        }

        IndicatorInstance CreateSuperTrend(
            const IndicatorSpec& spec,
            std::string& error)
        {
            if (!ExactParameters(
                    spec, { "period", "multiplier" }, error))
            {
                return {};
            }
            int period = 0;
            double multiplier = 0.0;
            if (!TryGetIntegerParameter(
                    spec, "period", 1, 10000, period, error) ||
                !DecimalParameter(
                    spec, "multiplier", 0.01, 100.0,
                    multiplier, error))
            {
                return {};
            }
            try {
                SuperTrendState* state = new SuperTrendState();
                state->period = period;
                state->multiplier = multiplier;
                error.clear();
                return IndicatorInstance(
                    "SUPERTREND", state, &ResetSuperTrend,
                    &UpdateSuperTrend, &DestroySuperTrend,
                    &SuperTrendBytes);
            }
            catch (const std::bad_alloc&) {
                error = "SuperTrend state allocation failed";
                return {};
            }
        }
    }

    bool RegisterEmaIndicator(IndicatorRegistry& registry)
    {
        return registry.Register("EMA", &CreateEma);
    }

    bool RegisterBollingerIndicator(IndicatorRegistry& registry)
    {
        return registry.Register("BOLLINGER", &CreateBollinger);
    }

    bool RegisterRsiIndicator(IndicatorRegistry& registry)
    {
        return registry.Register("RSI", &CreateRsi);
    }

    bool RegisterMacdIndicator(IndicatorRegistry& registry)
    {
        return registry.Register("MACD", &CreateMacd);
    }

    bool RegisterDmiIndicator(IndicatorRegistry& registry)
    {
        return registry.Register("DMI", &CreateDmi);
    }

    bool RegisterSuperTrendIndicator(IndicatorRegistry& registry)
    {
        return registry.Register("SUPERTREND", &CreateSuperTrend);
    }
}
