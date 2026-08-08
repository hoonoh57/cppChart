#pragma once

#include "kiwoom_events.h"
#include "kiwoom_protocol.h"
#include "order_coordinator.h"
#include "trading_state.h"

#include <string>
#include <vector>

namespace trading
{
    struct GatewayEventReport final
    {
        int orderRecords = 0;
        int fillEventsApplied = 0;
        int fillEventsDuplicate = 0;
        int fillEventsStale = 0;
        int balanceRecords = 0;
        int balanceUpdatesApplied = 0;
        std::vector<std::string> errors;

        bool Ok() const noexcept
        {
            return errors.empty();
        }
    };

    class KiwoomGatewayCore final
    {
    public:
        KiwoomGatewayCore(
            TradingState& tradingState,
            OrderCoordinator& orderCoordinator);

        KiwoomGatewayCore(const KiwoomGatewayCore&) = delete;
        KiwoomGatewayCore& operator=(const KiwoomGatewayCore&) = delete;

        GatewayEventReport ApplyRealTimeEnvelope(
            const RealTimeEnvelope& envelope,
            EpochMillis sessionDateStartMs = 0);

        bool ApplyAuthoritativePositions(
            const std::vector<PositionSnapshot>& brokerPositions,
            std::string& error);

    private:
        TradingState& tradingState_;
        OrderCoordinator& orderCoordinator_;
    };
}
