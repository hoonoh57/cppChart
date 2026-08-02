#pragma once

#include "kiwoom_protocol.h"
#include "market_types.h"

#include <string>
#include <vector>

namespace trading
{
    enum class ReconciliationSide
    {
        All,
        Sell,
        Buy
    };

    struct Continuation final
    {
        std::string continueYn;
        std::string nextKey;
    };

    struct OpenOrderSnapshot final
    {
        std::string brokerOrderNumber;
        std::string originalOrderNumber;
        std::string code;
        std::string name;
        StockOrderSide side = StockOrderSide::Buy;
        Quantity orderedQuantity = 0;
        Quantity unfilledQuantity = 0;
        PriceWon orderPriceWon = 0;
        std::string orderStatus;
        std::string orderTime;
    };

    struct ExecutionSnapshot final
    {
        std::string brokerOrderNumber;
        std::string originalOrderNumber;
        std::string code;
        std::string name;
        StockOrderSide side = StockOrderSide::Buy;
        Quantity orderedQuantity = 0;
        Quantity cumulativeFilledQuantity = 0;
        Quantity unfilledQuantity = 0;
        PriceWon orderPriceWon = 0;
        PriceWon fillPriceWon = 0;
        std::string orderStatus;
        std::string orderTime;
    };

    struct OpenOrdersResponse final
    {
        ProtocolResult result;
        std::vector<OpenOrderSnapshot> orders;
    };

    struct ExecutionsResponse final
    {
        ProtocolResult result;
        std::vector<ExecutionSnapshot> executions;
    };

    struct AccountBalanceResponse final
    {
        ProtocolResult result;
        MoneyWon totalPurchaseWon = 0;
        MoneyWon totalEvaluationWon = 0;
        MoneyWon totalEvaluationPnlWon = 0;
        MoneyWon estimatedAssetWon = 0;
        std::vector<PositionSnapshot> positions;
    };

    RestRequest BuildOpenOrdersRestRequest(
        const std::string& bearerToken,
        const std::string& stockCode = {},
        ReconciliationSide side = ReconciliationSide::All,
        const Continuation& continuation = {});

    RestRequest BuildExecutionsRestRequest(
        const std::string& bearerToken,
        const std::string& stockCode = {},
        ReconciliationSide side = ReconciliationSide::All,
        const std::string& beforeOrderNumber = {},
        const Continuation& continuation = {});

    RestRequest BuildAccountBalanceRestRequest(
        const std::string& bearerToken,
        const Continuation& continuation = {});

    OpenOrdersResponse ParseOpenOrdersResponse(
        const std::string& json);

    ExecutionsResponse ParseExecutionsResponse(
        const std::string& json);

    AccountBalanceResponse ParseAccountBalanceResponse(
        const std::string& json);
}
