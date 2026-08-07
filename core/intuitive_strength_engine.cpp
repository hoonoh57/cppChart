#include "intuitive_strength_engine.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>

namespace trading::stock_pool::intuitive
{
    namespace
    {
        constexpr double kEpsilon = 1.0e-12;

        double SafePercent(double current, double previous)
        {
            if (!std::isfinite(current) || !std::isfinite(previous) ||
                std::abs(previous) <= kEpsilon)
            {
                return 0.0;
            }
            return (current / previous - 1.0) * 100.0;
        }

        class Ema final
        {
        public:
            explicit Ema(int period)
                : alpha_(2.0 / (static_cast<double>((std::max)(1, period)) + 1.0))
            {
            }

            double Step(double value)
            {
                if (!initialized_) {
                    value_ = value;
                    initialized_ = true;
                }
                else {
                    value_ += alpha_ * (value - value_);
                }
                return value_;
            }

        private:
            double alpha_ = 1.0;
            double value_ = 0.0;
            bool initialized_ = false;
        };

        struct JmaStep final
        {
            double value = 0.0;
            double slopePercent = 0.0;
            int direction = 0;
        };

        class Jma final
        {
        public:
            Jma(int period, int phase, int power)
                : period_((std::max)(1, period)),
                  phase_(phase),
                  power_((std::max)(1, power))
            {
            }

            JmaStep Step(double source)
            {
                if (!initialized_) {
                    e0_ = source;
                    e1_ = 0.0;
                    e2_ = 0.0;
                    lastJma_ = source;
                    initialized_ = true;
                }

                const double beta =
                    0.45 * static_cast<double>(period_ - 1) /
                    (0.45 * static_cast<double>(period_ - 1) + 2.0);
                const double alpha = std::pow(beta, power_);
                e0_ = (1.0 - alpha) * source + alpha * e0_;
                e1_ = (source - e0_) * (1.0 - beta) + beta * e1_;
                e2_ =
                    (e0_ + (static_cast<double>(phase_) / 100.0 + 1.5) * e1_ - lastJma_) *
                        std::pow(1.0 - alpha, 2.0) +
                    std::pow(alpha, 2.0) * e2_;

                ++count_;
                warmSum_ += source;
                const double current = count_ <= period_
                    ? warmSum_ / static_cast<double>(count_)
                    : e2_ + lastJma_;
                const double previous = lastJma_;

                if (current > previous) direction_ = 1;
                else if (current < previous) direction_ = -1;
                else if (direction_ == 0) direction_ = 1;

                const double slope = SafePercent(current, previous);
                lastJma_ = current;
                return JmaStep{current, slope, direction_};
            }

        private:
            int period_ = 1;
            int phase_ = 50;
            int power_ = 2;
            double e0_ = 0.0;
            double e1_ = 0.0;
            double e2_ = 0.0;
            double lastJma_ = 0.0;
            double warmSum_ = 0.0;
            int direction_ = 0;
            int count_ = 0;
            bool initialized_ = false;
        };

        double TrueRange(const Bar& bar, double previousClose)
        {
            const double range = bar.high - bar.low;
            if (previousClose <= 0.0) return (std::max)(0.0, range);
            return (std::max)(
                range,
                (std::max)(
                    std::abs(bar.high - previousClose),
                    std::abs(bar.low - previousClose)));
        }

        double VolumeProxy(
            const Bar& bar,
            double previousCumulativeTurnover)
        {
            const double turnover = (std::max)(
                0.0,
                bar.cumulativeTurnover - previousCumulativeTurnover);
            const double typical =
                (bar.open + bar.high + bar.low + bar.close) * 0.25;
            return typical > kEpsilon ? turnover / typical : 0.0;
        }

        MemberStrengthSeries CalculateMember(
            const MemberSeries& member,
            std::size_t memberIndex,
            const StrengthConfig& config)
        {
            MemberStrengthSeries output;
            output.memberIndex = memberIndex;
            output.code = member.code;
            output.name = member.name;
            output.market = member.market;
            output.points.reserve(member.bars.size());
            if (member.bars.empty()) return output;

            Jma fastJma(config.fastJmaPeriod, config.jmaPhase, config.jmaPower);
            Jma slowJma(config.slowJmaPeriod, config.jmaPhase, config.jmaPower);
            Ema macdFast(config.macdFastPeriod);
            Ema macdSlow(config.macdSlowPeriod);
            Ema macdSignal(config.macdSignalPeriod);
            Ema obvSignal(config.obvSignalPeriod);

            std::deque<double> atrWindow;
            double atrSum = 0.0;
            std::deque<double> volumeWindow;
            double rollingVolume = 0.0;
            double obv = 0.0;
            double previousClose = 0.0;
            double previousTurnover = 0.0;
            double previousFast = 0.0;
            double previousSlow = 0.0;
            bool hasPreviousJma = false;

            bool activeWave = false;
            int barsSinceCross = -1;
            double crossFast = 0.0;
            double crossClose = 0.0;
            double crossSlope = 0.0;

            const double sessionAnchor = member.bars.front().open > 0.0
                ? member.bars.front().open
                : member.bars.front().close;

            for (std::size_t index = 0U; index < member.bars.size(); ++index) {
                const Bar& bar = member.bars[index];
                StrengthPoint point;
                point.asOf = bar.closeTimestampMs;
                point.close = bar.close;
                point.sessionReturnPercent =
                    SafePercent(bar.close, sessionAnchor);

                const JmaStep fast = fastJma.Step(bar.close);
                const JmaStep slow = slowJma.Step(bar.close);
                point.fastJma = fast.value;
                point.slowJma = slow.value;
                point.fastJmaSlopePercent = fast.slopePercent;
                point.slowJmaSlopePercent = slow.slopePercent;

                const double fastMacd = macdFast.Step(bar.close);
                const double slowMacd = macdSlow.Step(bar.close);
                const double macd = fastMacd - slowMacd;
                const double signal = macdSignal.Step(macd);
                const double histogram = macd - signal;

                const double tr = TrueRange(bar, previousClose);
                atrWindow.push_back(tr);
                atrSum += tr;
                while (atrWindow.size() >
                       static_cast<std::size_t>((std::max)(1, config.atrPeriod)))
                {
                    atrSum -= atrWindow.front();
                    atrWindow.pop_front();
                }
                const double atr = atrWindow.empty()
                    ? 0.0
                    : atrSum / static_cast<double>(atrWindow.size());
                point.macdHistogramAtr = atr > kEpsilon
                    ? histogram / atr
                    : 0.0;

                const double volume = VolumeProxy(bar, previousTurnover);
                if (index > 0U) {
                    if (bar.close > previousClose) obv += volume;
                    else if (bar.close < previousClose) obv -= volume;
                }
                const double obvSignalValue = obvSignal.Step(obv);
                volumeWindow.push_back(volume);
                rollingVolume += volume;
                while (volumeWindow.size() > static_cast<std::size_t>(
                           (std::max)(1, config.obvNormalizationBars)))
                {
                    rollingVolume -= volumeWindow.front();
                    volumeWindow.pop_front();
                }
                point.obvImpulse = rollingVolume > kEpsilon
                    ? (obv - obvSignalValue) / rollingVolume
                    : 0.0;

                const bool warmed = index + 1U >= static_cast<std::size_t>(
                    (std::max)(config.fastJmaPeriod, config.slowJmaPeriod));
                if (warmed && hasPreviousJma) {
                    point.crossUp =
                        previousFast < previousSlow && fast.value > slow.value;
                    point.crossDown =
                        previousFast > previousSlow && fast.value < slow.value;
                }
                point.bullishRegime = fast.value > slow.value;

                if (point.crossDown) {
                    activeWave = false;
                    barsSinceCross = -1;
                    crossFast = 0.0;
                    crossClose = 0.0;
                    crossSlope = 0.0;
                }
                if (point.crossUp) {
                    activeWave = true;
                    barsSinceCross = 0;
                    crossFast = fast.value;
                    crossClose = bar.close;
                    crossSlope = fast.slopePercent;
                }

                if (activeWave && point.bullishRegime) {
                    point.barsSinceCross = barsSinceCross;
                    point.crossJmaSlopePercent = crossSlope;
                    point.waveJmaGainPercent = SafePercent(fast.value, crossFast);
                    point.priceExtensionPercent = SafePercent(bar.close, crossClose);
                    point.fresh =
                        barsSinceCross >= 0 &&
                        barsSinceCross <= (std::max)(0, config.maxFreshBars);
                    ++barsSinceCross;
                }

                output.points.push_back(point);
                previousClose = bar.close;
                previousTurnover = bar.cumulativeTurnover;
                previousFast = fast.value;
                previousSlow = slow.value;
                hasPreviousJma = true;
            }

            return output;
        }
    }

    std::vector<MemberStrengthSeries> CalculateStrengthSeries(
        const std::vector<MemberSeries>& members,
        const StrengthConfig& config)
    {
        std::vector<MemberStrengthSeries> result;
        result.reserve(members.size());
        for (std::size_t index = 0U; index < members.size(); ++index) {
            result.push_back(CalculateMember(members[index], index, config));
        }
        return result;
    }

    StrengthSnapshot BuildStrengthSnapshot(
        const std::vector<MemberStrengthSeries>& series,
        std::size_t asOfIndex,
        const StrengthConfig&)
    {
        StrengthSnapshot snapshot;
        snapshot.asOfIndex = asOfIndex;
        for (const MemberStrengthSeries& member : series) {
            if (member.points.empty() || asOfIndex >= member.points.size()) continue;
            StrengthRow row;
            row.memberIndex = member.memberIndex;
            row.code = member.code;
            row.name = member.name;
            row.market = member.market;
            row.point = member.points[asOfIndex];
            row.buyEligible =
                row.point.fresh &&
                row.point.bullishRegime &&
                row.point.barsSinceCross >= 0 &&
                row.point.crossJmaSlopePercent > 0.0;
            snapshot.rows.push_back(std::move(row));
        }

        std::stable_sort(
            snapshot.rows.begin(),
            snapshot.rows.end(),
            [](const StrengthRow& left, const StrengthRow& right) {
                if (left.buyEligible != right.buyEligible)
                    return left.buyEligible > right.buyEligible;
                if (left.buyEligible) {
                    if (left.point.crossJmaSlopePercent !=
                        right.point.crossJmaSlopePercent)
                    {
                        return left.point.crossJmaSlopePercent >
                            right.point.crossJmaSlopePercent;
                    }
                    if (left.point.fastJmaSlopePercent !=
                        right.point.fastJmaSlopePercent)
                    {
                        return left.point.fastJmaSlopePercent >
                            right.point.fastJmaSlopePercent;
                    }
                }
                if (left.point.sessionReturnPercent !=
                    right.point.sessionReturnPercent)
                {
                    return left.point.sessionReturnPercent >
                        right.point.sessionReturnPercent;
                }
                return left.code < right.code;
            });

        int priority = 1;
        for (StrengthRow& row : snapshot.rows) {
            if (row.buyEligible) row.buyPriority = priority++;
        }
        if (!snapshot.rows.empty()) snapshot.asOf = snapshot.rows.front().point.asOf;
        return snapshot;
    }

    const char* BuyStateName(const StrengthRow& row) noexcept
    {
        if (row.buyEligible) return "매수유효";
        if (row.point.barsSinceCross >= 0 && row.point.bullishRegime) {
            if (!row.point.fresh) return "돌파만료";
            return "상승관찰";
        }
        if (row.point.bullishRegime) return "상승/교차대기";
        return "하락/대기";
    }
}
