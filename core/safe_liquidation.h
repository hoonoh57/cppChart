#pragma once

#include "kiwoom_reconciliation.h"
#include "order_coordinator.h"
#include "trading_state.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace trading
{
    class BrokerOpenOrderRegistry final
    {
    public:
        bool Replace(
            const std::vector<OpenOrderSnapshot>& orders,
            std::string& error);

        std::vector<OpenOrderSnapshot> Snapshot() const;

        Quantity UnknownReservedSellQuantity(
            const std::string& code,
            const std::set<std::string>& knownBrokerOrderNumbers) const;

        void Clear();

    private:
        mutable std::mutex mutex_;
        std::map<std::string, OpenOrderSnapshot> ordersByBrokerNumber_;
    };

    std::vector<LiquidationOrder> BuildBrokerAwareLiquidationPlan(
        const TradingState& tradingState,
        const OrderCoordinator& orderCoordinator,
        const BrokerOpenOrderRegistry& brokerOpenOrders,
        bool selectedOnly);
}
