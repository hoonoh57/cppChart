#include "order_coordinator.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace trading
{
    OrderCoordinator::OrderCoordinator(TradingState& tradingState)
        : tradingState_(tradingState)
    {
    }

    void OrderCoordinator::SetSubmissionAllowed(bool allowed)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        submissionAllowed_ = allowed && !reconciliationInProgress_;
    }

    bool OrderCoordinator::SubmissionAllowed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return submissionAllowed_ && !reconciliationInProgress_;
    }

    void OrderCoordinator::BeginReconciliation()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reconciliationInProgress_ = true;
        submissionAllowed_ = false;
    }

    void OrderCoordinator::CompleteReconciliation(bool success)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reconciliationInProgress_ = false;
        submissionAllowed_ = success;
    }

    bool OrderCoordinator::ReconciliationInProgress() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return reconciliationInProgress_;
    }

    CreateOrderResult OrderCoordinator::CreateOrder(
        const OrderIntent& intent)
    {
        const std::map<std::string, PositionSnapshot> positions =
            IndexPositions(tradingState_.SnapshotPositions());

        std::lock_guard<std::mutex> lock(mutex_);
        return CreateOrderLocked(intent, positions);
    }

    std::vector<CreateOrderResult>
    OrderCoordinator::CreateLiquidationOrders(
        bool selectedOnly,
        StockOrderType type)
    {
        const std::vector<PositionSnapshot> positionList =
            tradingState_.SnapshotPositions();

        const std::map<std::string, PositionSnapshot> positions =
            IndexPositions(positionList);

        std::vector<CreateOrderResult> results;
        std::lock_guard<std::mutex> lock(mutex_);

        if (!submissionAllowed_ || reconciliationInProgress_) {
            CreateOrderResult rejected;
            rejected.error =
                reconciliationInProgress_
                    ? "orders are blocked during reconciliation"
                    : "order submission is not allowed";
            results.push_back(std::move(rejected));
            return results;
        }

        for (const PositionSnapshot& position : positionList) {
            if (selectedOnly && !position.selected) continue;

            const Quantity reserved =
                ReservedSellQuantityLocked(position.code);

            const Quantity available =
                position.quantity > reserved
                    ? position.quantity - reserved
                    : 0;

            if (available <= 0) continue;

            OrderIntent intent;
            intent.code = position.code;
            intent.name = position.name;
            intent.side = StockOrderSide::Sell;
            intent.type = type;
            intent.quantity = available;
            results.push_back(
                CreateOrderLocked(intent, positions));
        }

        return results;
    }

    bool OrderCoordinator::BuildRestRequest(
        const std::string& clientIntentId,
        const std::string& bearerToken,
        RestRequest& request,
        std::string& error) const
    {
        OrderRecord order;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = ordersByClient_.find(clientIntentId);

            if (found == ordersByClient_.end()) {
                error = "client intent was not found";
                return false;
            }

            order = found->second;
        }

        if (order.lifecycle != OrderLifecycle::PendingRest) {
            error = "REST request can only be built for a pending intent";
            return false;
        }

        StockOrderRequest stockRequest;
        stockRequest.side = order.side;
        stockRequest.type = order.type;
        stockRequest.code = order.code;
        stockRequest.quantity = order.requestedQuantity;
        stockRequest.limitPriceWon = order.limitPriceWon;

        request = BuildStockOrderRestRequest(
            stockRequest,
            bearerToken,
            error);

        return error.empty();
    }

    bool OrderCoordinator::MarkRestAccepted(
        const std::string& clientIntentId,
        const std::string& brokerOrderNumber,
        std::string& error)
    {
        if (brokerOrderNumber.empty()) {
            error = "broker order number is required";
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto found = ordersByClient_.find(clientIntentId);

        if (found == ordersByClient_.end()) {
            error = "client intent was not found";
            return false;
        }

        OrderRecord& order = found->second;

        if (!order.brokerOrderNumber.empty()) {
            if (order.brokerOrderNumber == brokerOrderNumber) {
                error.clear();
                return true;
            }

            error = "client intent is already bound to another broker order";
            return false;
        }

        const auto brokerBinding =
            brokerToClient_.find(brokerOrderNumber);

        if (
            brokerBinding != brokerToClient_.end() &&
            brokerBinding->second != clientIntentId)
        {
            error = "broker order number is already bound";
            return false;
        }

        const auto orphan = orphanByBroker_.find(brokerOrderNumber);
        if (orphan != orphanByBroker_.end()) {
            const OrderRecord& orphanRecord = orphan->second;

            if (
                orphanRecord.code != order.code ||
                orphanRecord.side != order.side)
            {
                error = "orphan broker fill metadata does not match the intent";
                return false;
            }

            if (
                orphanRecord.cumulativeFilledQuantity >
                order.requestedQuantity)
            {
                error = "broker cumulative fill exceeds requested quantity";
                return false;
            }
        }

        order.brokerOrderNumber = brokerOrderNumber;
        order.lifecycle = OrderLifecycle::Accepted;
        brokerToClient_[brokerOrderNumber] = clientIntentId;

        if (orphan != orphanByBroker_.end()) {
            const OrderRecord orphanRecord = orphan->second;
            order.cumulativeFilledQuantity =
                orphanRecord.cumulativeFilledQuantity;

            if (order.side == StockOrderSide::Sell) {
                Quantity& reserved =
                    reservedSellQuantity_[order.code];

                reserved =
                    reserved > order.cumulativeFilledQuantity
                        ? reserved - order.cumulativeFilledQuantity
                        : 0;
            }

            order.lifecycle =
                order.cumulativeFilledQuantity >= order.requestedQuantity
                    ? OrderLifecycle::Filled
                    : OrderLifecycle::PartiallyFilled;

            orphanByBroker_.erase(orphan);
        }

        error.clear();
        return true;
    }

    bool OrderCoordinator::MarkRestRejected(
        const std::string& clientIntentId,
        const std::string& errorMessage,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = ordersByClient_.find(clientIntentId);

        if (found == ordersByClient_.end()) {
            error = "client intent was not found";
            return false;
        }

        OrderRecord& order = found->second;

        if (order.lifecycle == OrderLifecycle::Filled) {
            error = "a filled order cannot be rejected";
            return false;
        }

        ReleaseOutstandingSellReservationLocked(order);
        order.lifecycle = OrderLifecycle::Rejected;
        order.error = errorMessage;
        error.clear();
        return true;
    }

    BrokerFillApplyResult OrderCoordinator::ApplyBrokerFill(
        const BrokerFillEvent& event)
    {
        BrokerFillApplyResult result;

        if (event.brokerOrderNumber.empty()) {
            result.error = "broker order number is required";
            return result;
        }

        OrderRecord knownOrder;
        bool isKnown = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto binding =
                brokerToClient_.find(event.brokerOrderNumber);

            if (binding != brokerToClient_.end()) {
                const auto order =
                    ordersByClient_.find(binding->second);

                if (order != ordersByClient_.end()) {
                    knownOrder = order->second;
                    result.clientIntentId = binding->second;
                    isKnown = true;
                }
            }
        }

        if (isKnown) {
            if (
                knownOrder.code != event.code ||
                knownOrder.side != event.side)
            {
                result.error = "broker fill metadata does not match the accepted order";
                return result;
            }

            if (event.cumulativeQuantity > knownOrder.requestedQuantity) {
                result.error = "broker cumulative fill exceeds requested quantity";
                return result;
            }
        }

        CumulativeFill fill;
        fill.orderId = event.brokerOrderNumber;
        fill.executionId = event.executionId;
        fill.code = event.code;
        fill.name = event.name;
        fill.side = ToStateSide(event.side);
        fill.cumulativeQuantity = event.cumulativeQuantity;
        fill.fillPriceWon = event.fillPriceWon;
        fill.executionTimestampMs = event.executionTimestampMs;

        result.positionResult =
            tradingState_.ApplyCumulativeFill(fill);

        if (result.positionResult.status == ApplyFillStatus::Rejected) {
            result.error = result.positionResult.error;
            return result;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const auto binding =
            brokerToClient_.find(event.brokerOrderNumber);

        if (binding == brokerToClient_.end()) {
            OrderRecord& orphan =
                orphanByBroker_[event.brokerOrderNumber];

            orphan.brokerOrderNumber = event.brokerOrderNumber;
            orphan.code = event.code;
            orphan.name = event.name;
            orphan.side = event.side;
            orphan.requestedQuantity =
                (std::max)(
                    orphan.requestedQuantity,
                    event.cumulativeQuantity);
            orphan.cumulativeFilledQuantity =
                (std::max)(
                    orphan.cumulativeFilledQuantity,
                    event.cumulativeQuantity);
            orphan.lifecycle = OrderLifecycle::OrphanBrokerEvent;

            result.orphanBrokerEvent = true;
            return result;
        }

        auto orderFound = ordersByClient_.find(binding->second);
        if (orderFound == ordersByClient_.end()) {
            result.error = "broker binding refers to a missing client intent";
            return result;
        }

        OrderRecord& order = orderFound->second;
        result.clientIntentId = binding->second;

        if (result.positionResult.status == ApplyFillStatus::Applied) {
            order.cumulativeFilledQuantity = event.cumulativeQuantity;

            if (order.side == StockOrderSide::Sell) {
                Quantity& reserved =
                    reservedSellQuantity_[order.code];

                const Quantity applied =
                    result.positionResult.appliedQuantity;

                reserved = reserved > applied
                    ? reserved - applied
                    : 0;
            }

            order.lifecycle =
                order.cumulativeFilledQuantity >= order.requestedQuantity
                    ? OrderLifecycle::Filled
                    : OrderLifecycle::PartiallyFilled;
        }

        return result;
    }

    std::vector<OrderRecord> OrderCoordinator::SnapshotOrders() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<OrderRecord> result;
        result.reserve(
            ordersByClient_.size() +
            orphanByBroker_.size());

        for (const auto& entry : ordersByClient_) {
            result.push_back(entry.second);
        }

        for (const auto& entry : orphanByBroker_) {
            result.push_back(entry.second);
        }

        return result;
    }

    Quantity OrderCoordinator::ReservedSellQuantity(
        const std::string& code) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return ReservedSellQuantityLocked(code);
    }

    void OrderCoordinator::Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        nextIntentSequence_ = 1;
        submissionAllowed_ = false;
        reconciliationInProgress_ = false;
        ordersByClient_.clear();
        brokerToClient_.clear();
        orphanByBroker_.clear();
        reservedSellQuantity_.clear();
    }

    OrderSide OrderCoordinator::ToStateSide(
        StockOrderSide side) noexcept
    {
        return side == StockOrderSide::Buy
            ? OrderSide::Buy
            : OrderSide::Sell;
    }

    CreateOrderResult OrderCoordinator::CreateOrderLocked(
        const OrderIntent& intent,
        const std::map<std::string, PositionSnapshot>& positions)
    {
        CreateOrderResult result;

        if (!submissionAllowed_ || reconciliationInProgress_) {
            result.error =
                reconciliationInProgress_
                    ? "orders are blocked during reconciliation"
                    : "order submission is not allowed";
            return result;
        }

        if (intent.code.empty()) {
            result.error = "stock code is required";
            return result;
        }

        if (!IsValidQuantity(intent.quantity)) {
            result.error = "order quantity must be positive";
            return result;
        }

        if (
            intent.type == StockOrderType::Limit &&
            !IsValidPrice(intent.limitPriceWon))
        {
            result.error = "limit order price must be positive";
            return result;
        }

        if (intent.side == StockOrderSide::Sell) {
            const auto position = positions.find(intent.code);

            if (position == positions.end()) {
                result.error = "sell order has no matching position";
                return result;
            }

            const Quantity reserved =
                ReservedSellQuantityLocked(intent.code);

            const Quantity available =
                position->second.quantity > reserved
                    ? position->second.quantity - reserved
                    : 0;

            if (intent.quantity > available) {
                result.error =
                    "sell order exceeds the unreserved held quantity";
                return result;
            }
        }

        std::ostringstream identifier;
        identifier
            << "I-"
            << std::setw(10)
            << std::setfill('0')
            << nextIntentSequence_++;

        OrderRecord order;
        order.clientIntentId = identifier.str();
        order.code = intent.code;
        order.name = intent.name;
        order.side = intent.side;
        order.type = intent.type;
        order.requestedQuantity = intent.quantity;
        order.limitPriceWon = intent.limitPriceWon;
        order.lifecycle = OrderLifecycle::PendingRest;

        ordersByClient_[order.clientIntentId] = order;

        if (order.side == StockOrderSide::Sell) {
            reservedSellQuantity_[order.code] +=
                order.requestedQuantity;
        }

        result.ok = true;
        result.order = order;
        return result;
    }

    std::map<std::string, PositionSnapshot>
    OrderCoordinator::IndexPositions(
        const std::vector<PositionSnapshot>& positions)
    {
        std::map<std::string, PositionSnapshot> result;

        for (const PositionSnapshot& position : positions) {
            result[position.code] = position;
        }

        return result;
    }

    Quantity OrderCoordinator::ReservedSellQuantityLocked(
        const std::string& code) const noexcept
    {
        const auto found = reservedSellQuantity_.find(code);
        return found == reservedSellQuantity_.end()
            ? 0
            : found->second;
    }

    void OrderCoordinator::ReleaseOutstandingSellReservationLocked(
        const OrderRecord& order)
    {
        if (order.side != StockOrderSide::Sell) return;

        const Quantity outstanding =
            order.requestedQuantity > order.cumulativeFilledQuantity
                ? order.requestedQuantity - order.cumulativeFilledQuantity
                : 0;

        Quantity& reserved =
            reservedSellQuantity_[order.code];

        reserved = reserved > outstanding
            ? reserved - outstanding
            : 0;
    }
}
