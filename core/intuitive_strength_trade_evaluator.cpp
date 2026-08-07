#include "intuitive_strength_trade_evaluator.h"

#include <algorithm>
#include <cmath>

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

        bool IsBuyGate(const StrengthPoint& point) noexcept
        {
            return
                point.inEvaluationWindow &&
                point.fresh &&
                point.bullishRegime &&
                point.barsSinceCross >= 0 &&
                point.crossJmaSlopePercent > 0.0;
        }

        bool IsUsableNextBar(
            const MemberSeries& member,
            const MemberStrengthSeries& strength,
            std::size_t index,
            EpochMillis asOfTime,
            const StrengthConfig& config)
        {
            if (index + 1U >= member.bars.size() ||
                index + 1U >= strength.points.size())
            {
                return false;
            }
            const StrengthPoint& next = strength.points[index + 1U];
            if (!next.inSession || next.asOf > asOfTime) return false;
            if (config.evaluationEnd > 0 && next.asOf > config.evaluationEnd) {
                return false;
            }
            return member.bars[index + 1U].open > 0.0;
        }

        double NetLiquidationReturnPercent(
            double entryPrice,
            double exitPrice,
            const TradeCostConfig& costs)
        {
            if (entryPrice <= kEpsilon || exitPrice <= kEpsilon) return 0.0;
            const double slippage = (std::max)(0.0, costs.slippageBps) / 10000.0;
            const double buyFee = (std::max)(0.0, costs.feePercentEachSide) / 100.0;
            const double sellFee = buyFee;
            const double sellTax = (std::max)(0.0, costs.sellTaxPercent) / 100.0;

            const double slippedBuy = entryPrice * (1.0 + slippage);
            const double slippedSell = exitPrice * (1.0 - slippage);
            const double cashOut = slippedBuy * (1.0 + buyFee);
            const double cashIn = slippedSell * (1.0 - sellFee - sellTax);
            return SafePercent(cashIn, cashOut);
        }

        void CalculateExcursions(
            const MemberSeries& member,
            std::size_t entryIndex,
            std::size_t finalHoldingIndex,
            double entryPrice,
            double& mfePercent,
            double& maePercent)
        {
            mfePercent = 0.0;
            maePercent = 0.0;
            if (entryPrice <= kEpsilon || entryIndex >= member.bars.size()) return;
            finalHoldingIndex = (std::min)(finalHoldingIndex, member.bars.size() - 1U);
            if (finalHoldingIndex < entryIndex) return;

            double bestHigh = entryPrice;
            double worstLow = entryPrice;
            for (std::size_t index = entryIndex; index <= finalHoldingIndex; ++index) {
                bestHigh = (std::max)(bestHigh, member.bars[index].high);
                worstLow = (std::min)(worstLow, member.bars[index].low);
            }
            mfePercent = SafePercent(bestHigh, entryPrice);
            maePercent = SafePercent(worstLow, entryPrice);
        }

        double Compound(const std::vector<double>& returns)
        {
            double equity = 1.0;
            for (double value : returns) {
                equity *= 1.0 + value / 100.0;
            }
            return (equity - 1.0) * 100.0;
        }
    }

    MemberTradeEvaluation EvaluateCausalJmaTrades(
        const MemberSeries& member,
        const MemberStrengthSeries& strength,
        const StrengthConfig& config,
        EpochMillis asOfTime,
        const TradeCostConfig& costs)
    {
        MemberTradeEvaluation result;
        result.memberIndex = strength.memberIndex;
        result.code = member.code;
        result.name = member.name;

        const std::size_t count = (std::min)(member.bars.size(), strength.points.size());
        if (count == 0U || asOfTime <= 0) return result;

        std::size_t lastVisible = count;
        for (std::size_t index = 0U; index < count; ++index) {
            if (strength.points[index].asOf > asOfTime) {
                lastVisible = index;
                break;
            }
        }
        if (lastVisible == 0U) return result;
        --lastVisible;
        result.sessionReturnPercent = strength.points[lastVisible].sessionReturnPercent;

        std::vector<double> waveReturns;
        bool waveOpen = false;
        CrossWaveCapture wave;
        for (std::size_t index = 0U; index <= lastVisible; ++index) {
            const StrengthPoint& point = strength.points[index];
            if (!point.inSession) continue;

            if (point.crossUp) {
                wave = {};
                wave.crossUpIndex = index;
                wave.crossUpTime = point.asOf;
                wave.entryClose = member.bars[index].close;
                waveOpen = wave.entryClose > 0.0;
            }

            if (waveOpen && point.crossDown) {
                wave.crossDownIndex = index;
                wave.crossDownTime = point.asOf;
                wave.exitClose = member.bars[index].close;
                wave.returnPercent = SafePercent(wave.exitClose, wave.entryClose);
                wave.closed = true;
                result.waves.push_back(wave);
                waveReturns.push_back(wave.returnPercent);
                waveOpen = false;
            }
        }
        if (waveOpen) {
            wave.crossDownIndex = lastVisible;
            wave.crossDownTime = strength.points[lastVisible].asOf;
            wave.exitClose = member.bars[lastVisible].close;
            wave.returnPercent = SafePercent(wave.exitClose, wave.entryClose);
            wave.closed = false;
            result.waves.push_back(wave);
            waveReturns.push_back(wave.returnPercent);
        }
        result.crossWaveCompoundPercent = Compound(waveReturns);

        std::vector<double> grossReturns;
        std::vector<double> netReturns;
        bool positionOpen = false;
        CausalTrade trade;

        for (std::size_t index = 0U; index <= lastVisible; ++index) {
            const StrengthPoint& point = strength.points[index];
            if (!point.inSession) continue;

            if (!positionOpen && IsBuyGate(point) &&
                IsUsableNextBar(member, strength, index, asOfTime, config))
            {
                const std::size_t entryIndex = index + 1U;
                trade = {};
                trade.signalIndex = index;
                trade.signalTime = point.asOf;
                trade.entryIndex = entryIndex;
                trade.entryTime = strength.points[entryIndex].asOf;
                trade.entryPrice = member.bars[entryIndex].open;
                positionOpen = trade.entryPrice > 0.0;
                continue;
            }

            if (positionOpen && point.crossDown &&
                IsUsableNextBar(member, strength, index, asOfTime, config))
            {
                const std::size_t exitIndex = index + 1U;
                trade.exitSignalIndex = index;
                trade.exitSignalTime = point.asOf;
                trade.exitIndex = exitIndex;
                trade.exitTime = strength.points[exitIndex].asOf;
                trade.exitPrice = member.bars[exitIndex].open;
                trade.grossReturnPercent = SafePercent(
                    trade.exitPrice,
                    trade.entryPrice);
                trade.netReturnPercent = NetLiquidationReturnPercent(
                    trade.entryPrice,
                    trade.exitPrice,
                    costs);
                CalculateExcursions(
                    member,
                    trade.entryIndex,
                    index,
                    trade.entryPrice,
                    trade.mfePercent,
                    trade.maePercent);
                trade.closed = true;
                result.bestMfePercent = (std::max)(
                    result.bestMfePercent,
                    trade.mfePercent);
                result.worstMaePercent = (std::min)(
                    result.worstMaePercent,
                    trade.maePercent);
                result.trades.push_back(trade);
                grossReturns.push_back(trade.grossReturnPercent);
                netReturns.push_back(trade.netReturnPercent);
                ++result.closedTradeCount;
                positionOpen = false;
            }
        }

        if (positionOpen) {
            trade.exitIndex = lastVisible;
            trade.exitTime = strength.points[lastVisible].asOf;
            trade.exitPrice = member.bars[lastVisible].close;
            trade.grossReturnPercent = SafePercent(
                trade.exitPrice,
                trade.entryPrice);
            trade.netReturnPercent = NetLiquidationReturnPercent(
                trade.entryPrice,
                trade.exitPrice,
                costs);
            CalculateExcursions(
                member,
                trade.entryIndex,
                lastVisible,
                trade.entryPrice,
                trade.mfePercent,
                trade.maePercent);
            trade.closed = false;
            result.bestMfePercent = (std::max)(
                result.bestMfePercent,
                trade.mfePercent);
            result.worstMaePercent = (std::min)(
                result.worstMaePercent,
                trade.maePercent);
            result.trades.push_back(trade);
            grossReturns.push_back(trade.grossReturnPercent);
            netReturns.push_back(trade.netReturnPercent);
            result.hasOpenTrade = true;
        }

        result.executionGrossCompoundPercent = Compound(grossReturns);
        result.executionNetCompoundPercent = Compound(netReturns);
        return result;
    }
}
