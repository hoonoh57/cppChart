#pragma once

#include "kiwoom_protocol.h"
#include "market_types.h"

#include <string>

namespace trading
{
    enum class EventDecodeStatus
    {
        NotApplicable,
        NoExecution,
        Decoded,
        Invalid
    };

    struct KiwoomOrderExecution final
    {
        std::string brokerOrderNumber;
        std::string originalOrderNumber;
        std::string executionId;
        std::string code;
        std::string name;
        StockOrderSide side = StockOrderSide::Buy;
        Quantity orderedQuantity = 0;
        Quantity cumulativeQuantity = 0;
        Quantity unfilledQuantity = 0;
        Quantity lastFillQuantity = 0;
        PriceWon fillPriceWon = 0;
        EpochMillis executionTimestampMs = 0;
        std::string orderStatus;
    };

    struct OrderExecutionDecodeResult final
    {
        EventDecodeStatus status = EventDecodeStatus::NotApplicable;
        KiwoomOrderExecution event;
        std::string error;
    };

    struct KiwoomBalanceUpdate final
    {
        std::string code;
        std::string name;
        Quantity quantity = 0;
        Quantity availableQuantity = 0;
        PriceWon averagePriceWon = 0;
        PriceWon currentPriceWon = 0;
        MoneyWon costBasisWon = 0;
    };

    struct BalanceDecodeResult final
    {
        EventDecodeStatus status = EventDecodeStatus::NotApplicable;
        KiwoomBalanceUpdate event;
        std::string error;
    };

    OrderExecutionDecodeResult DecodeKiwoomOrderExecution(
        const RealTimeRecord& record,
        EpochMillis sessionDateStartMs = 0);

    BalanceDecodeResult DecodeKiwoomBalanceUpdate(
        const RealTimeRecord& record);

    PositionSnapshot ToPositionSnapshot(
        const KiwoomBalanceUpdate& update);
}
