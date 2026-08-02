#include "trading_state.h"

#include <limits>
#include <utility>

namespace trading
{
    ApplyFillResult TradingState::ApplyCumulativeFill(
        const CumulativeFill& fill)
    {
        ApplyFillResult result;

        if (fill.orderId.empty()) {
            result.error = "order id is required";
            return result;
        }
        if (fill.code.empty()) {
            result.error = "symbol code is required";
            return result;
        }
        if (!IsValidQuantity(fill.cumulativeQuantity)) {
            result.error = "cumulative quantity must be positive";
            return result;
        }
        if (!IsValidPrice(fill.fillPriceWon)) {
            result.error = "fill price must be positive";
            return result;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (
            !fill.executionId.empty() &&
            executionIds_.find(fill.executionId) != executionIds_.end())
        {
            result.status = ApplyFillStatus::Duplicate;
            return result;
        }

        auto progressIt = orderProgress_.find(fill.orderId);
        if (progressIt == orderProgress_.end()) {
            OrderProgress progress;
            progress.code = fill.code;
            progress.side = fill.side;
            progressIt = orderProgress_.emplace(
                fill.orderId,
                std::move(progress)).first;
        }
        else if (
            progressIt->second.code != fill.code ||
            progressIt->second.side != fill.side)
        {
            result.error = "order metadata changed after the first fill";
            return result;
        }

        OrderProgress& progress = progressIt->second;

        if (fill.cumulativeQuantity == progress.cumulativeApplied) {
            if (!fill.executionId.empty()) {
                executionIds_.insert(fill.executionId);
            }
            result.status = ApplyFillStatus::Duplicate;
            return result;
        }

        if (fill.cumulativeQuantity < progress.cumulativeApplied) {
            if (!fill.executionId.empty()) {
                executionIds_.insert(fill.executionId);
            }
            result.status = ApplyFillStatus::Stale;
            return result;
        }

        const Quantity deltaQuantity =
            fill.cumulativeQuantity - progress.cumulativeApplied;

        MoneyWon deltaNotional = 0;
        if (!TryCalculateNotional(
                fill.fillPriceWon,
                deltaQuantity,
                deltaNotional))
        {
            result.error = "fill notional overflow";
            return result;
        }

        MoneyWon realizedDelta = 0;

        if (fill.side == OrderSide::Buy) {
            PositionSnapshot& position = positions_[fill.code];

            if (position.code.empty()) {
                position.code = fill.code;
                position.name = fill.name;
            }
            else if (!fill.name.empty()) {
                position.name = fill.name;
            }

            if (
                position.quantity >
                (std::numeric_limits<Quantity>::max)() -
                deltaQuantity)
            {
                result.error = "position quantity overflow";
                return result;
            }

            MoneyWon updatedCostBasis = 0;
            if (!CheckedAdd(
                    position.costBasisWon,
                    deltaNotional,
                    updatedCostBasis))
            {
                result.error = "position cost basis overflow";
                return result;
            }

            position.quantity += deltaQuantity;
            position.costBasisWon = updatedCostBasis;
            position.currentPriceWon = fill.fillPriceWon;
        }
        else {
            auto positionIt = positions_.find(fill.code);
            if (positionIt == positions_.end()) {
                result.error = "sell fill has no matching position";
                return result;
            }

            PositionSnapshot& position = positionIt->second;
            if (deltaQuantity > position.quantity) {
                result.error = "sell fill exceeds the held quantity";
                return result;
            }

            const Quantity previousQuantity = position.quantity;
            const MoneyWon releasedCostBasis =
                deltaQuantity == previousQuantity
                    ? position.costBasisWon
                    : (
                        position.costBasisWon *
                        static_cast<MoneyWon>(deltaQuantity)) /
                        static_cast<MoneyWon>(previousQuantity);

            realizedDelta = deltaNotional - releasedCostBasis;

            MoneyWon updatedRealized = 0;
            if (!CheckedAdd(
                    realizedPnlWon_,
                    realizedDelta,
                    updatedRealized))
            {
                result.error = "realized PnL overflow";
                return result;
            }

            realizedPnlWon_ = updatedRealized;
            position.quantity -= deltaQuantity;
            position.costBasisWon -= releasedCostBasis;
            position.currentPriceWon = fill.fillPriceWon;

            if (position.quantity == 0) {
                positions_.erase(positionIt);
            }
        }

        progress.cumulativeApplied = fill.cumulativeQuantity;

        if (!fill.executionId.empty()) {
            executionIds_.insert(fill.executionId);
        }

        result.status = ApplyFillStatus::Applied;
        result.appliedQuantity = deltaQuantity;
        result.appliedNotionalWon = deltaNotional;
        result.realizedPnlWon = realizedDelta;
        return result;
    }

    bool TradingState::ReconcilePosition(
        const PositionSnapshot& brokerPosition,
        std::string& error)
    {
        if (brokerPosition.code.empty()) {
            error = "symbol code is required";
            return false;
        }
        if (brokerPosition.quantity < 0) {
            error = "broker quantity cannot be negative";
            return false;
        }
        if (brokerPosition.costBasisWon < 0) {
            error = "broker cost basis cannot be negative";
            return false;
        }
        if (
            brokerPosition.quantity > 0 &&
            brokerPosition.costBasisWon == 0)
        {
            error = "a held position must have a cost basis";
            return false;
        }
        if (
            brokerPosition.quantity == 0 &&
            brokerPosition.costBasisWon != 0)
        {
            error = "a flat position cannot have a cost basis";
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (brokerPosition.quantity == 0) {
            positions_.erase(brokerPosition.code);
            error.clear();
            return true;
        }

        positions_[brokerPosition.code] = brokerPosition;
        error.clear();
        return true;
    }

    bool TradingState::ReconcileOrderProgress(
        const std::string& orderId,
        const std::string& code,
        OrderSide side,
        Quantity cumulativeApplied,
        std::string& error)
    {
        if (orderId.empty()) {
            error = "order id is required";
            return false;
        }
        if (code.empty()) {
            error = "symbol code is required";
            return false;
        }
        if (cumulativeApplied < 0) {
            error = "cumulative quantity cannot be negative";
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto found = orderProgress_.find(orderId);

        if (found == orderProgress_.end()) {
            OrderProgress progress;
            progress.code = code;
            progress.side = side;
            progress.cumulativeApplied = cumulativeApplied;
            orderProgress_.emplace(orderId, std::move(progress));
            error.clear();
            return true;
        }

        OrderProgress& progress = found->second;
        if (progress.code != code || progress.side != side) {
            error = "reconciled order metadata conflicts with existing progress";
            return false;
        }

        if (cumulativeApplied > progress.cumulativeApplied) {
            progress.cumulativeApplied = cumulativeApplied;
        }

        error.clear();
        return true;
    }

    bool TradingState::UpdateCurrentPrice(
        const std::string& code,
        PriceWon priceWon)
    {
        if (code.empty() || !IsValidPrice(priceWon)) return false;

        std::lock_guard<std::mutex> lock(mutex_);
        auto found = positions_.find(code);
        if (found == positions_.end()) return false;

        found->second.currentPriceWon = priceWon;
        return true;
    }

    bool TradingState::SetSelected(
        const std::string& code,
        bool selected)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = positions_.find(code);
        if (found == positions_.end()) return false;

        found->second.selected = selected;
        return true;
    }

    std::vector<PositionSnapshot> TradingState::SnapshotPositions() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<PositionSnapshot> result;
        result.reserve(positions_.size());

        for (const auto& entry : positions_) {
            result.push_back(entry.second);
        }

        return result;
    }

    std::vector<LiquidationOrder> TradingState::BuildLiquidationPlan(
        bool selectedOnly) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<LiquidationOrder> result;
        result.reserve(positions_.size());

        for (const auto& entry : positions_) {
            const PositionSnapshot& position = entry.second;
            if (selectedOnly && !position.selected) continue;
            if (!IsValidQuantity(position.quantity)) continue;

            LiquidationOrder order;
            order.code = position.code;
            order.name = position.name;
            order.quantity = position.quantity;
            result.push_back(std::move(order));
        }

        return result;
    }

    MoneyWon TradingState::RealizedPnlWon() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return realizedPnlWon_;
    }

    void TradingState::Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        positions_.clear();
        orderProgress_.clear();
        executionIds_.clear();
        realizedPnlWon_ = 0;
    }

    bool TradingState::CheckedAdd(
        MoneyWon left,
        MoneyWon right,
        MoneyWon& out) noexcept
    {
        if (
            (right > 0 &&
             left > (std::numeric_limits<MoneyWon>::max)() - right) ||
            (right < 0 &&
             left < (std::numeric_limits<MoneyWon>::min)() - right))
        {
            out = 0;
            return false;
        }

        out = left + right;
        return true;
    }
}
