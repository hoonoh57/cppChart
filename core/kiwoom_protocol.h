#pragma once

#include "market_types.h"

#include <map>
#include <string>
#include <vector>

namespace trading
{
    struct KiwoomEndpoints final
    {
        static constexpr const char* MockRestBase =
            "https://mockapi.kiwoom.com";
        static constexpr const char* MockWebSocket =
            "wss://mockapi.kiwoom.com:10000/api/dostk/websocket";
        static constexpr const char* TokenPath =
            "/oauth2/token";
        static constexpr const char* StockOrderPath =
            "/api/dostk/ordr";
    };

    struct ProtocolResult final
    {
        bool ok = false;
        int returnCode = -1;
        std::string returnMessage;
        std::string error;
    };

    struct TokenResponse final
    {
        ProtocolResult result;
        std::string tokenType;
        std::string token;
        std::string expiresAt;
    };

    struct OrderResponse final
    {
        ProtocolResult result;
        std::string orderNumber;
        std::string exchange;
    };

    enum class StockOrderSide
    {
        Buy,
        Sell
    };

    enum class StockOrderType
    {
        Limit,
        Market
    };

    struct StockOrderRequest final
    {
        StockOrderSide side = StockOrderSide::Buy;
        StockOrderType type = StockOrderType::Market;
        std::string code;
        Quantity quantity = 0;
        PriceWon limitPriceWon = 0;
    };

    struct RestRequest final
    {
        std::string method;
        std::string path;
        std::string apiId;
        std::map<std::string, std::string> headers;
        std::string body;
    };

    struct RealTimeRecord final
    {
        std::string type;
        std::string item;
        std::string name;
        std::map<std::string, std::string> values;
    };

    struct RealTimeEnvelope final
    {
        ProtocolResult result;
        std::string transactionName;
        std::vector<RealTimeRecord> records;
    };

    std::string BuildTokenRequestBody(
        const std::string& appKey,
        const std::string& secretKey);

    TokenResponse ParseTokenResponse(
        const std::string& json);

    std::string BuildWebSocketLoginMessage(
        const std::string& accessToken);

    std::string BuildWebSocketRegistrationMessage(
        const std::string& groupNumber,
        bool keepExisting,
        const std::vector<std::string>& items,
        const std::vector<std::string>& realTimeTypes);

    bool IsWebSocketPingMessage(
        const std::string& json);

    RestRequest BuildStockOrderRestRequest(
        const StockOrderRequest& request,
        const std::string& bearerToken,
        std::string& error);

    OrderResponse ParseOrderResponse(
        const std::string& json);

    RealTimeEnvelope ParseRealTimeEnvelope(
        const std::string& json);
}
