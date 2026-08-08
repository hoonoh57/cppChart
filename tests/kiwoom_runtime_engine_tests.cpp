#include "../core/kiwoom_runtime_engine.h"

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

    const trading::KiwoomRuntimeAction& OnlyAction(
        const std::vector<trading::KiwoomRuntimeAction>& actions,
        trading::KiwoomRuntimeActionType expected,
        const char* message)
    {
        Check(actions.size() == 1, message);
        Check(actions[0].type == expected, message);
        return actions[0];
    }

    trading::RuntimeConfig MakeConfig()
    {
        trading::RuntimeConfig config;
        config.mode = trading::RuntimeMode::KiwoomMock;
        config.appKey = "APP-KEY";
        config.secretKey = "SECRET-KEY";
        config.accountNumber = "12345678";
        return config;
    }

    struct Fixture final
    {
        trading::TradingState state;
        trading::OrderCoordinator orders{ state };
        trading::KiwoomGatewayCore gateway{ state, orders };
        trading::BrokerOpenOrderRegistry brokerOpenOrders;
        trading::KiwoomRuntimeEngine runtime{
            state,
            orders,
            gateway,
            brokerOpenOrders
        };
    };

    void BringRuntimeReady(Fixture& fixture)
    {
        const auto start = fixture.runtime.Start(MakeConfig());
        const auto& tokenAction = OnlyAction(
            start,
            trading::KiwoomRuntimeActionType::RequestTokenHttp,
            "start must request a token");
        Check(tokenAction.sensitive,
              "token request must be marked sensitive");
        Check(tokenAction.request.path == "/oauth2/token",
              "token endpoint mismatch");
        Check(tokenAction.request.body.find("SECRET-KEY") != std::string::npos,
              "token request must contain configured secret");

        const auto token = fixture.runtime.OnTokenHttpResponse(
            true,
            200,
            "{\"token_type\":\"bearer\",\"token\":\"ACCESS\","
            "\"expires_dt\":\"20260803120000\",\"return_code\":0}");
        OnlyAction(
            token,
            trading::KiwoomRuntimeActionType::ConnectWebSocket,
            "token success must connect WebSocket");

        const auto connected = fixture.runtime.OnWebSocketConnected();
        const auto& login = OnlyAction(
            connected,
            trading::KiwoomRuntimeActionType::SendWebSocketText,
            "WebSocket connect must send LOGIN");
        Check(login.sensitive,
              "LOGIN message contains the token and must be sensitive");
        Check(login.text.find("ACCESS") != std::string::npos,
              "LOGIN token mismatch");

        const auto loginAck = fixture.runtime.OnWebSocketMessage(
            "{\"trnm\":\"LOGIN\",\"return_code\":0}");
        const auto& registration = OnlyAction(
            loginAck,
            trading::KiwoomRuntimeActionType::SendWebSocketText,
            "LOGIN acknowledgement must register real-time types");
        Check(registration.text.find("\"00\"") != std::string::npos,
              "order execution type 00 must be registered");
        Check(registration.text.find("\"04\"") != std::string::npos,
              "balance type 04 must be registered");

        const auto regAck = fixture.runtime.OnWebSocketMessage(
            "{\"trnm\":\"REG\",\"return_code\":0}");
        const auto& openOrders = OnlyAction(
            regAck,
            trading::KiwoomRuntimeActionType::RequestOpenOrders,
            "REG acknowledgement must start reconciliation");
        Check(openOrders.request.apiId == "ka10075",
              "first reconciliation API must be ka10075");
        Check(!fixture.orders.SubmissionAllowed(),
              "orders must remain blocked during reconciliation");

        trading::Continuation next;
        next.continueYn = "Y";
        next.nextKey = "OPEN-NEXT";

        const auto openPage1 = fixture.runtime.OnOpenOrdersHttpResponse(
            true,
            200,
            "{\"oso\":[{\"ord_no\":\"MANUAL-S1\","
            "\"orig_ord_no\":\"\",\"stk_cd\":\"005930\","
            "\"stk_nm\":\"삼성전자\",\"ord_stt\":\"접수\","
            "\"ord_qty\":\"2\",\"ord_pric\":\"0\","
            "\"oso_qty\":\"2\",\"io_tp_nm\":\"매도\","
            "\"tm\":\"091500\"}],\"return_code\":0}",
            next);
        const auto& nextOpen = OnlyAction(
            openPage1,
            trading::KiwoomRuntimeActionType::RequestOpenOrders,
            "continuation must request the next open-order page");
        Check(nextOpen.request.headers.at("next-key") == "OPEN-NEXT",
              "open-order continuation key mismatch");

        const auto openPage2 = fixture.runtime.OnOpenOrdersHttpResponse(
            true,
            200,
            "{\"oso\":[],\"return_code\":0}");
        const auto& executions = OnlyAction(
            openPage2,
            trading::KiwoomRuntimeActionType::RequestExecutions,
            "open orders must advance to executions");
        Check(executions.request.apiId == "ka10076",
              "second reconciliation API must be ka10076");

        const auto executionPage = fixture.runtime.OnExecutionsHttpResponse(
            true,
            200,
            "{\"cntr\":[{\"ord_no\":\"OLD-BUY\","
            "\"orig_ord_no\":\"\",\"stk_cd\":\"A005930\","
            "\"stk_nm\":\"삼성전자\",\"io_tp_nm\":\"매수\","
            "\"ord_pric\":\"0\",\"ord_qty\":\"6\","
            "\"cntr_pric\":\"70000\",\"cntr_qty\":\"6\","
            "\"oso_qty\":\"0\",\"ord_stt\":\"체결\","
            "\"ord_tm\":\"091501\"}],\"return_code\":0}");
        const auto& balance = OnlyAction(
            executionPage,
            trading::KiwoomRuntimeActionType::RequestAccountBalance,
            "executions must advance to account balance");
        Check(balance.request.apiId == "kt00018",
              "third reconciliation API must be kt00018");

        const auto balancePage = fixture.runtime.OnAccountBalanceHttpResponse(
            true,
            200,
            "{\"tot_pur_amt\":\"840000\","
            "\"tot_evlt_amt\":\"852000\","
            "\"tot_evlt_pl\":\"12000\","
            "\"prsm_dpst_aset_amt\":\"100000000\","
            "\"acnt_evlt_remn_indv_tot\":[{"
            "\"stk_cd\":\"A005930\",\"stk_nm\":\"삼성전자\","
            "\"pur_pric\":\"70000\",\"rmnd_qty\":\"12\","
            "\"trde_able_qty\":\"10\",\"cur_prc\":\"71000\","
            "\"pur_amt\":\"840000\",\"evlt_amt\":\"852000\"}],"
            "\"return_code\":0}");
        Check(balancePage.empty(),
              "successful final balance page needs no further action");

        const trading::KiwoomRuntimeSnapshot ready =
            fixture.runtime.Snapshot();
        Check(ready.sessionState == trading::KiwoomSessionState::Ready,
              "session must become ready after reconciliation");
        Check(ready.orderSubmissionAllowed,
              "orders must be allowed only after reconciliation");
        Check(ready.positionCount == 1,
              "broker position must become authoritative");
        Check(ready.brokerOpenOrderCount == 1,
              "manual broker open order must be retained");
    }

    void TestReadyOrderFillAndSafeLiquidation()
    {
        Fixture fixture;
        BringRuntimeReady(fixture);

        trading::OrderIntent buy;
        buy.code = "005930";
        buy.name = "삼성전자";
        buy.side = trading::StockOrderSide::Buy;
        buy.type = trading::StockOrderType::Market;
        buy.quantity = 3;

        std::string error;
        const auto submit = fixture.runtime.SubmitOrder(buy, error);
        const auto& orderAction = OnlyAction(
            submit,
            trading::KiwoomRuntimeActionType::SubmitOrderHttp,
            "ready session must produce a REST order action");
        Check(error.empty(), "valid order must not return an error");
        Check(orderAction.request.apiId == "kt10000",
              "buy order API ID mismatch");
        Check(!orderAction.clientIntentId.empty(),
              "order action must carry its client intent ID");

        Check(fixture.runtime.OnOrderHttpResponse(
                  orderAction.clientIntentId,
                  true,
                  200,
                  "{\"ord_no\":\"NEW-BUY\",\"dmst_stex_tp\":\"KRX\","
                  "\"return_code\":0}",
                  error),
              "accepted REST order response must bind broker order number");

        const auto fillActions = fixture.runtime.OnWebSocketMessage(
            "{\"trnm\":\"REAL\",\"return_code\":0,\"data\":[{"
            "\"type\":\"00\",\"item\":\"005930\","
            "\"name\":\"주문체결\",\"values\":{"
            "\"9203\":\"NEW-BUY\",\"9001\":\"A005930\","
            "\"302\":\"삼성전자\",\"907\":\"2\","
            "\"900\":\"3\",\"902\":\"0\","
            "\"911\":\"3\",\"910\":\"72000\","
            "\"909\":\"EXEC-NEW-1\",\"908\":\"093000\"}}]}");
        Check(fillActions.empty(),
              "valid fill event must not force an external action");

        const auto positions = fixture.state.SnapshotPositions();
        Check(positions.size() == 1 && positions[0].quantity == 15,
              "new fill must add exactly three shares to broker balance");
        Check(positions[0].costBasisWon == 1056000,
              "fill must update exact integer cost basis");

        const auto oldFill = fixture.runtime.OnWebSocketMessage(
            "{\"trnm\":\"REAL\",\"return_code\":0,\"data\":[{"
            "\"type\":\"00\",\"item\":\"005930\","
            "\"name\":\"주문체결\",\"values\":{"
            "\"9203\":\"OLD-BUY\",\"9001\":\"A005930\","
            "\"302\":\"삼성전자\",\"907\":\"2\","
            "\"900\":\"6\",\"902\":\"0\","
            "\"911\":\"6\",\"910\":\"70000\","
            "\"909\":\"OLD-EXEC-REPLAY\",\"908\":\"091501\"}}]}");
        Check(oldFill.empty(),
              "replayed reconciled fill must be treated as duplicate");
        Check(fixture.state.SnapshotPositions()[0].quantity == 15,
              "replayed pre-reconnect execution must not change quantity");

        const auto liquidation =
            fixture.runtime.SubmitLiquidation(false, error);
        const auto& sellAction = OnlyAction(
            liquidation,
            trading::KiwoomRuntimeActionType::SubmitOrderHttp,
            "emergency liquidation must create one sell request");
        Check(sellAction.request.apiId == "kt10001",
              "liquidation API ID mismatch");
        Check(sellAction.request.body.find("\"ord_qty\":\"13\"") !=
                  std::string::npos,
              "liquidation must subtract the manual two-share sell order");

        const auto repeated =
            fixture.runtime.SubmitLiquidation(false, error);
        Check(repeated.empty(),
              "repeated liquidation must not reserve the same shares twice");
        Check(!error.empty(),
              "suppressed repeated liquidation must explain why no order exists");
    }

    void TestDisconnectBlocksOrdersAndSchedulesReconnect()
    {
        Fixture fixture;
        BringRuntimeReady(fixture);

        const auto disconnected =
            fixture.runtime.OnWebSocketClosed("physical disconnect");
        const auto& reconnect = OnlyAction(
            disconnected,
            trading::KiwoomRuntimeActionType::ScheduleReconnect,
            "disconnect must schedule reconnect");
        Check(reconnect.delayMilliseconds >= 1000,
              "reconnect must use a positive backoff");

        const trading::KiwoomRuntimeSnapshot snapshot =
            fixture.runtime.Snapshot();
        Check(!snapshot.orderSubmissionAllowed,
              "disconnect must immediately block orders");
        Check(snapshot.lastError.find("physical disconnect") !=
                  std::string::npos,
              "disconnect reason must remain visible");

        trading::OrderIntent buy;
        buy.code = "005930";
        buy.name = "삼성전자";
        buy.quantity = 1;
        std::string error;
        Check(fixture.runtime.SubmitOrder(buy, error).empty(),
              "order creation while disconnected must be blocked");
        Check(!error.empty(),
              "blocked disconnected order must return an error");
    }

    void TestTokenTransportFailureDoesNotExposeSecret()
    {
        Fixture fixture;
        fixture.runtime.Start(MakeConfig());
        const auto failed = fixture.runtime.OnTokenHttpResponse(
            false,
            0,
            {},
            "network unavailable");
        OnlyAction(
            failed,
            trading::KiwoomRuntimeActionType::ScheduleReconnect,
            "token transport failure must schedule reconnect");

        const trading::KiwoomRuntimeSnapshot snapshot =
            fixture.runtime.Snapshot();
        Check(snapshot.lastError.find("network unavailable") !=
                  std::string::npos,
              "transport failure must remain visible");
        Check(snapshot.lastError.find("SECRET-KEY") == std::string::npos,
              "runtime snapshot must never expose the secret key");
    }
}

int main()
{
    TestReadyOrderFillAndSafeLiquidation();
    TestDisconnectBlocksOrdersAndSchedulesReconnect();
    TestTokenTransportFailureDoesNotExposeSecret();

    std::puts("[PASS] kiwoom_runtime_engine_tests");
    return 0;
}
