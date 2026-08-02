#pragma once

#include "kiwoom_protocol.h"
#include "trading_state.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace trading
{
    enum class OrderLifecycle
    {
        PendingRest,
        Accepted,
        PartiallyFilled,
        Filled,
        Rejected,
        Cancelled,
        OrphanBrokerEvent
    };

    struct OrderIntent final
    {
        std::string code;
        std::string name;
        StockOrderSide side = StockOrderSide::Buy;
        StockOrderType type = StockOrderType::Market;
        Quantity quantity = 0;
        PriceWon limitPriceWon = 0;
    };

    struct OrderRecord final
    {
        std::string clientIntentId;
        std::string brokerOrderNumber;
        std::string code;
        std::string name;
        StockOrderSide side = StockOrderSide::Buy;
        StockOrderType type = StockOrderType::Market;
        Quantity requestedQuantity = 0;
        Quantity cumulativeFilledQuantity = 0;
        PriceWon limitPriceWon = 0;
        OrderLifecycle lifecycle = OrderLifecycle::PendingRest;
        std::string error;
    };

    struct CreateOrderResult final
    {
        bool ok = false;
        OrderRecord order;
        std::string error;
    };

    struct BrokerFillEvent final
    {
        std::string brokerOrderNumber;
        std::string executionId;
        std::string code;
        std::string name;
        StockOrderSide side = StockOrderSide::Buy;
        Quantity cumulativeQuantity = 0;
        PriceWon fillPriceWon = 0;
        EpochMillis executionTimestampMs = 0;
    };

    struct BrokerFillApplyResult final
    {
        ApplyFillResult positionResult;
        std::string clientIntentId;
        bool orphanBrokerEvent = false;
        std::string error;
    };

    class OrderCoordinator final
    {
    public:
        explicit OrderCoordinator(TradingState& tradingState);

        OrderCoordinator(const OrderCoordinator&) = delete;
        OrderCoordinator& operator=(const OrderCoordinator&) = delete;

        void SetSubmissionAllowed(bool allowed);
        bool SubmissionAllowed() const;

        void BeginReconciliation();
        void CompleteReconciliation(bool success);
        bool ReconciliationInProgress() const;

        CreateOrderResult CreateOrder(
            const OrderIntent& intent);

        std::vector<CreateOrderResult> CreateLiquidationOrders(
            bool selectedOnly,
            StockOrderType type = StockOrderType::Market);

        bool BuildRestRequest(
            const std::string& clientIntentId,
            const std::string& bearerToken,
            RestRequest& request,
            std::string& error) const;

        bool MarkRestAccepted(
            const std::string& clientIntentId,
            const std::string& brokerOrderNumber,
            std::string& error);

        bool MarkRestRejected(
            const std::string& clientIntentId,
            const std::string& errorMessage,
            std::string& error);

        BrokerFillApplyResult ApplyBrokerFill(
            const BrokerFillEvent& event);

        std::vector<OrderRecord> SnapshotOrders() const;

        Quantity ReservedSellQuantity(
            const std::string& code) const;

        void Reset();

    private:
        static OrderSide ToStateSide(
            StockOrderSide side) noexcept;

        CreateOrderResult CreateOrderLocked(
            const OrderIntent& intent,
            const std::map<std::string, PositionSnapshot>& positions);

        static std::map<std::string, PositionSnapshot> IndexPositions(
            const std::vector<PositionSnapshot>& positions);

        void ReleaseOutstandingSellReservationLocked(
            const OrderRecord& order);

        TradingState& tradingState_;
        mutable std::mutex mutex_;
        std::uint64_t nextIntentSequence_ = 1;
        bool submissionAllowed_ = false;
        bool reconciliationInProgress_ = false;

        std::map<std::string, OrderRecord> ordersByClient_;
        std::unordered_map<std::string, std::string> brokerToClient_;
        std::map<std::string, OrderRecord> orphanByBroker_;
        std::unordered_map<std::string, Quantity> reservedSellQuantity_;
    };
}
