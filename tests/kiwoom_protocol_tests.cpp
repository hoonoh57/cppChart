#include "../core/json_lite.h"
#include "../core/kiwoom_protocol.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::exit(1);
    }

    void Check(bool condition, const char* message)
    {
        if (!condition) Fail(message);
    }

    void TestTokenProtocol()
    {
        const std::string body = trading::BuildTokenRequestBody(
            "app-key-with-\"quote",
            "secret\\value");

        const json_lite::ParseResult request = json_lite::Parse(body);
        Check(request.ok, "token request JSON must be valid");
        Check(
            request.value.Find("grant_type")->AsString() ==
                "client_credentials",
            "token grant_type mismatch");
        Check(
            request.value.Find("appkey")->AsString() ==
                "app-key-with-\"quote",
            "token app key escaping mismatch");
        Check(
            request.value.Find("secretkey")->AsString() ==
                "secret\\value",
            "token secret escaping mismatch");

        const trading::TokenResponse response =
            trading::ParseTokenResponse(
                "{"
                "\"expires_dt\":\"20260804000000\","
                "\"token_type\":\"bearer\","
                "\"token\":\"access-token\","
                "\"return_code\":0,"
                "\"return_msg\":\"정상적으로 처리되었습니다\""
                "}");

        Check(response.result.ok, "token response must succeed");
        Check(response.token == "access-token", "token value mismatch");
        Check(response.tokenType == "bearer", "token type mismatch");
        Check(response.expiresAt == "20260804000000",
              "token expiry mismatch");

        const trading::TokenResponse rejected =
            trading::ParseTokenResponse(
                "{\"return_code\":-101,\"return_msg\":\"invalid key\"}");

        Check(!rejected.result.ok, "rejected token response must fail");
        Check(rejected.result.returnCode == -101,
              "rejected token return code mismatch");
    }

    void TestWebSocketProtocol()
    {
        const std::string login =
            trading::BuildWebSocketLoginMessage("access-token");

        const json_lite::ParseResult loginJson = json_lite::Parse(login);
        Check(loginJson.ok, "WebSocket LOGIN JSON must be valid");
        Check(loginJson.value.Find("trnm")->AsString() == "LOGIN",
              "WebSocket LOGIN transaction mismatch");
        Check(loginJson.value.Find("token")->AsString() == "access-token",
              "WebSocket LOGIN token mismatch");

        const std::string registration =
            trading::BuildWebSocketRegistrationMessage(
                "1",
                true,
                { "005930" },
                { "00", "04" });

        const json_lite::ParseResult registrationJson =
            json_lite::Parse(registration);

        Check(registrationJson.ok,
              "WebSocket REG JSON must be valid");
        Check(registrationJson.value.Find("trnm")->AsString() == "REG",
              "WebSocket REG transaction mismatch");
        Check(registrationJson.value.Find("refresh")->AsString() == "1",
              "WebSocket REG refresh mismatch");

        const json_lite::Value& registrationData =
            registrationJson.value.Find("data")->AsArray().front();

        Check(registrationData.Find("item")->AsArray().front().AsString() ==
                  "005930",
              "WebSocket REG item mismatch");
        Check(registrationData.Find("type")->AsArray().size() == 2,
              "WebSocket REG type count mismatch");
        Check(registrationData.Find("type")->AsArray()[0].AsString() == "00",
              "WebSocket order event type mismatch");
        Check(registrationData.Find("type")->AsArray()[1].AsString() == "04",
              "WebSocket balance event type mismatch");

        Check(
            trading::IsWebSocketPingMessage(
                "{\"trnm\":\"PING\",\"timestamp\":123}"),
            "PING message must be recognized");
        Check(
            !trading::IsWebSocketPingMessage(
                "{\"trnm\":\"LOGIN\",\"return_code\":0}"),
            "LOGIN message must not be recognized as PING");
    }

    void TestOrderProtocol()
    {
        trading::StockOrderRequest marketBuy;
        marketBuy.side = trading::StockOrderSide::Buy;
        marketBuy.type = trading::StockOrderType::Market;
        marketBuy.code = "005930";
        marketBuy.quantity = 3;

        std::string error;
        const trading::RestRequest buyRequest =
            trading::BuildStockOrderRestRequest(
                marketBuy,
                "access-token",
                error);

        Check(error.empty(), "market buy request must be valid");
        Check(buyRequest.method == "POST", "order method mismatch");
        Check(buyRequest.path == "/api/dostk/ordr",
              "order path mismatch");
        Check(buyRequest.apiId == "kt10000",
              "buy api-id mismatch");
        Check(buyRequest.headers.at("authorization") ==
                  "Bearer access-token",
              "authorization header mismatch");

        const json_lite::ParseResult buyBody =
            json_lite::Parse(buyRequest.body);

        Check(buyBody.ok, "market buy body must be valid JSON");
        Check(buyBody.value.Find("dmst_stex_tp")->AsString() == "KRX",
              "mock order exchange must be KRX");
        Check(buyBody.value.Find("ord_qty")->AsString() == "3",
              "order quantity body mismatch");
        Check(buyBody.value.Find("trde_tp")->AsString() == "3",
              "market order type mismatch");
        Check(buyBody.value.Find("ord_uv")->AsString().empty(),
              "market order price must be empty");

        trading::StockOrderRequest limitSell;
        limitSell.side = trading::StockOrderSide::Sell;
        limitSell.type = trading::StockOrderType::Limit;
        limitSell.code = "000660";
        limitSell.quantity = 7;
        limitSell.limitPriceWon = 185000;

        const trading::RestRequest sellRequest =
            trading::BuildStockOrderRestRequest(
                limitSell,
                "access-token",
                error);

        Check(error.empty(), "limit sell request must be valid");
        Check(sellRequest.apiId == "kt10001",
              "sell api-id mismatch");

        const json_lite::ParseResult sellBody =
            json_lite::Parse(sellRequest.body);

        Check(sellBody.ok, "limit sell body must be valid JSON");
        Check(sellBody.value.Find("trde_tp")->AsString() == "0",
              "limit order type mismatch");
        Check(sellBody.value.Find("ord_uv")->AsString() == "185000",
              "limit order price mismatch");

        const trading::OrderResponse accepted =
            trading::ParseOrderResponse(
                "{"
                "\"ord_no\":\"1234567\","
                "\"dmst_stex_tp\":\"KRX\","
                "\"return_code\":0,"
                "\"return_msg\":\"\""
                "}");

        Check(accepted.result.ok, "order response must succeed");
        Check(accepted.orderNumber == "1234567",
              "order number mismatch");
        Check(accepted.exchange == "KRX", "order exchange mismatch");
    }

    void TestRealTimeEnvelope()
    {
        const trading::RealTimeEnvelope envelope =
            trading::ParseRealTimeEnvelope(
                "{"
                "\"trnm\":\"REAL\","
                "\"return_code\":0,"
                "\"data\":["
                "{"
                "\"type\":\"00\","
                "\"item\":\"005930\","
                "\"name\":\"주문체결\","
                "\"values\":{"
                "\"9203\":\"1234567\","
                "\"9001\":\"005930\","
                "\"911\":\"3\","
                "\"910\":\"70100\""
                "}"
                "},"
                "{"
                "\"type\":\"04\","
                "\"item\":\"005930\","
                "\"name\":\"잔고\","
                "\"values\":{"
                "\"930\":\"3\","
                "\"931\":\"70100\""
                "}"
                "}"
                "]"
                "}");

        Check(envelope.result.ok, "real-time envelope must parse");
        Check(envelope.transactionName == "REAL",
              "real-time transaction name mismatch");
        Check(envelope.records.size() == 2,
              "real-time record count mismatch");
        Check(envelope.records[0].type == "00",
              "order event record type mismatch");
        Check(envelope.records[0].values.at("9203") == "1234567",
              "order event value mismatch");
        Check(envelope.records[1].type == "04",
              "balance event record type mismatch");
        Check(envelope.records[1].values.at("930") == "3",
              "balance quantity value mismatch");
    }
}

int main()
{
    TestTokenProtocol();
    TestWebSocketProtocol();
    TestOrderProtocol();
    TestRealTimeEnvelope();

    std::puts("[PASS] kiwoom_protocol_tests");
    return 0;
}
