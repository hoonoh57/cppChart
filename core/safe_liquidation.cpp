#include "safe_liquidation.h"

#include <algorithm>
#include <limits>

namespace trading
{
    bool BrokerOpenOrderRegistry::Replace(
        const std::vector<OpenOrderSnapshot>& orders,
        std::string& error)
    {
        std::map<std::string, OpenOrderSnapshot> validated;

        for (const OpenOrderSnapshot& order : orders) {
            if (order.brokerOrderNumber.empty()) {
                error = "broker order number is required";
                return false;
            }
            if (order.code.empty()) {
                error = "open order stock code is required";
                return false;
            }
            if (order.orderedQuantity <= 0) {
                error = "open order quantity must be positive";
                return false;
            }
            if (
                order.unfilledQuantity < 0 ||
                order.unfilledQuantity > order.orderedQuantity)
            {
                error = "open order unfilled quantity is invalid";
                return false;
            }
            if (!validated.emplace(order.brokerOrderNumber, order).second) {
                error = "duplicate broker order number: " +
                    order.brokerOrderNumber;
                return false;
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        ordersByBrokerNumber_.swap(validated);
        error.clear();
        return true;
    }

    std::vector<OpenOrderSnapshot>
    BrokerOpenOrderRegistry::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<OpenOrderSnapshot> result;
        result.reserve(ordersByBrokerNumber_.size());
        for (const auto& entry : ordersByBrokerNumber_) {
            result.push_back(entry.second);
        }
        return result;
    }

    Quantity BrokerOpenOrderRegistry::UnknownReservedSellQuantity(
        const std::string& code,
        const std::set<std::string>& knownBrokerOrderNumbers) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::int64_t total = 0;
        for (const auto& entry : ordersByBrokerNumber_) {
            const OpenOrderSnapshot& order = entry.second;
            if (order.code != code) continue;
            if (order.side != StockOrderSide::Sell) continue;
            if (knownBrokerOrderNumbers.find(order.brokerOrderNumber) !=
                knownBrokerOrderNumbers.end())
            {
                continue;
            }

            total += order.unfilledQuantity;
            if (total > (std::numeric_limits<Quantity>::max)()) {
                return (std::numeric_limits<Quantity>::max)();
            }
        }

        return static_cast<Quantity>(total);
    }

    void BrokerOpenOrderRegistry::Clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ordersByBrokerNumber_.clear();
    }

    std::vector<LiquidationOrder> BuildBrokerAwareLiquidationPlan(
        const TradingState& tradingState,
        const OrderCoordinator& orderCoordinator,
        const BrokerOpenOrderRegistry& brokerOpenOrders,
        bool selectedOnly)
    {
        const std::vector<PositionSnapshot> positions =
            tradingState.SnapshotPositions();
        const std::vector<OrderRecord> coordinatorOrders =
            orderCoordinator.SnapshotOrders();

        std::set<std::string> knownBrokerOrderNumbers;
        for (const OrderRecord& order : coordinatorOrders) {
            if (!order.brokerOrderNumber.empty()) {
                knownBrokerOrderNumbers.insert(order.brokerOrderNumber);
            }
        }

        std::vector<LiquidationOrder> result;
        result.reserve(positions.size());

        for (const PositionSnapshot& position : positions) {
            if (selectedOnly && !position.selected) continue;
            if (position.quantity <= 0) continue;

            const Quantity coordinatorReserved =
                orderCoordinator.ReservedSellQuantity(position.code);
            const Quantity externalReserved =
                brokerOpenOrders.UnknownReservedSellQuantity(
                    position.code,
                    knownBrokerOrderNumbers);

            const std::int64_t reservedWide =
                static_cast<std::int64_t>(coordinatorReserved) +
                static_cast<std::int64_t>(externalReserved);
            const Quantity reserved =
                reservedWide >= position.quantity
                    ? position.quantity
                    : static_cast<Quantity>(reservedWide);
            const Quantity available = position.quantity - reserved;
            if (available <= 0) continue;

            LiquidationOrder order;
            order.code = position.code;
            order.name = position.name;
            order.quantity = available;
            result.push_back(std::move(order));
        }

        return result;
    }
}
