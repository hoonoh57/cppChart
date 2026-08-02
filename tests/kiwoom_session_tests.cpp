#include "../core/json_lite.h"
#include "../core/kiwoom_protocol.h"
#include "../core/kiwoom_session.h"
#include "../core/runtime_config.h"

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

    trading::RuntimeConfig MockConfig()
    {
        trading::RuntimeConfig config;
        config.mode = trading::RuntimeMode::KiwoomMock;
        config.appKey = "mock-app";
        config.secretKey = "mock-secret";
        config.accountNumber = "12345678";
        return config;
    }

    void TestHappyPath()
    {
        trading::KiwoomSession session;

        std::vector<trading::KiwoomSessionAction> actions =
            session.Start(MockConfig());

        Check(actions.size() == 1,
              "session start must request one token action");
        Check(actions[0].type ==
                  trading::KiwoomSessionActionType::RequestToken,
              "session start action must request a token");
        Check(actions[0].sensitive,
              "token request action must be marked sensitive");
        Check(!session.Snapshot().orderSubmissionAllowed,
              "orders must be blocked before login and reconciliation");

        actions = session.OnTokenResponse(
            "{"
            "\"expires_dt\":\"20260804000000\","
            "\"token_type\":\"bearer\","
            "\"token\":\"access-token\","
            "\"return_code\":0,"
            "\"return_msg\":\"\""
            "}");

        Check(actions.size() == 1,
              "token response must request a WebSocket connection");
        Check(actions[0].type ==
                  trading::KiwoomSessionActionType::ConnectWebSocket,
              "token response action mismatch");
        Check(actions[0].text ==
                  "wss://mockapi.kiwoom.com:10000/api/dostk/websocket",
              "WebSocket endpoint mismatch");

        actions = session.OnWebSocketConnected();
        Check(actions.size() == 1,
              "WebSocket connection must produce LOGIN");
        Check(actions[0].type ==
                  trading::KiwoomSessionActionType::SendWebSocketText,
              "LOGIN action type mismatch");
        Check(actions[0].sensitive,
              "LOGIN action containing the token must be sensitive");

        const json_lite::ParseResult login =
            json_lite::Parse(actions[0].text);
        Check(login.ok, "LOGIN payload must be valid JSON");
        Check(login.value.Find("trnm")->AsString() == "LOGIN",
              "LOGIN payload transaction mismatch");
        Check(login.value.Find("token")->AsString() == "access-token",
              "LOGIN payload token mismatch");

        actions = session.OnWebSocketMessage(
            "{\"trnm\":\"LOGIN\",\"return_code\":0,\"return_msg\":\"\"}");

        Check(actions.size() == 1,
              "LOGIN success must produce REG");
        Check(actions[0].type ==
                  trading::KiwoomSessionActionType::SendWebSocketText,
              "REG action type mismatch");

        const json_lite::ParseResult registration =
            json_lite::Parse(actions[0].text);
        Check(registration.ok, "REG payload must be valid JSON");
        Check(registration.value.Find("trnm")->AsString() == "REG",
              "REG transaction mismatch");

        const json_lite::Value& registrationItem =
            registration.value.Find("data")->AsArray().front();
        Check(registrationItem.Find("type")->AsArray().size() == 2,
              "REG must subscribe to two real-time types");
        Check(registrationItem.Find("type")->AsArray()[0].AsString() == "00",
              "REG must subscribe to order events");
        Check(registrationItem.Find("type")->AsArray()[1].AsString() == "04",
              "REG must subscribe to balance events");

        actions = session.OnWebSocketMessage(
            "{\"trnm\":\"REG\",\"return_code\":0,\"return_msg\":\"\"}");

        Check(actions.size() == 1,
              "REG success must start reconciliation");
        Check(actions[0].type ==
                  trading::KiwoomSessionActionType::StartReconciliation,
              "reconciliation action mismatch");
        Check(!session.Snapshot().orderSubmissionAllowed,
              "orders must remain blocked during reconciliation");

        actions = session.OnReconciliationCompleted(true);
        Check(actions.empty(),
              "successful reconciliation needs no outbound action");
        Check(session.Snapshot().state ==
                  trading::KiwoomSessionState::Ready,
              "session must become ready after reconciliation");
        Check(session.Snapshot().orderSubmissionAllowed,
              "orders must be allowed only after reconciliation");
        Check(session.Snapshot().reconnectAttempt == 0,
              "successful startup must reset reconnect attempts");
    }

    void TestPingAndReconnect()
    {
        trading::KiwoomSession session;
        session.Start(MockConfig());
        session.OnTokenResponse(
            "{\"token\":\"access-token\",\"return_code\":0}");
        session.OnWebSocketConnected();
        session.OnWebSocketMessage(
            "{\"trnm\":\"LOGIN\",\"return_code\":0}");
        session.OnWebSocketMessage(
            "{\"trnm\":\"REG\",\"return_code\":0}");
        session.OnReconciliationCompleted(true);

        const std::string ping =
            "{\"trnm\":\"PING\",\"timestamp\":123456}";

        std::vector<trading::KiwoomSessionAction> actions =
            session.OnWebSocketMessage(ping);

        Check(actions.size() == 1,
              "PING must produce one echo action");
        Check(actions[0].type ==
                  trading::KiwoomSessionActionType::SendWebSocketText,
              "PING echo action mismatch");
        Check(actions[0].text == ping,
              "PING must be echoed unchanged");

        actions = session.OnWebSocketClosed("network lost");
        Check(!actions.empty(),
              "disconnect must schedule reconnect");
        Check(actions[0].type ==
                  trading::KiwoomSessionActionType::ScheduleReconnect,
              "disconnect action mismatch");
        Check(actions[0].delayMilliseconds == 1000,
              "first reconnect delay must be one second");
        Check(!session.Snapshot().orderSubmissionAllowed,
              "disconnect must block order submission");

        actions = session.OnReconnectTimer();
        Check(actions.size() == 1,
              "reconnect timer must request WebSocket connect");
        Check(actions[0].type ==
                  trading::KiwoomSessionActionType::ConnectWebSocket,
              "reconnect timer action mismatch");

        actions = session.OnWebSocketClosed("connect failed");
        Check(actions[0].delayMilliseconds == 2000,
              "second reconnect delay must be two seconds");
    }

    void TestFailureGates()
    {
        trading::KiwoomSession missingConfig;
        trading::RuntimeConfig invalid;
        invalid.mode = trading::RuntimeMode::KiwoomMock;

        std::vector<trading::KiwoomSessionAction> actions =
            missingConfig.Start(invalid);

        Check(actions.size() == 1,
              "missing credentials must enter observe mode");
        Check(actions[0].type ==
                  trading::KiwoomSessionActionType::EnterObserveMode,
              "missing credentials action mismatch");
        Check(missingConfig.Snapshot().state ==
                  trading::KiwoomSessionState::ConfigurationError,
              "missing credentials state mismatch");

        trading::KiwoomSession session;
        session.Start(MockConfig());
        session.OnTokenResponse(
            "{\"token\":\"access-token\",\"return_code\":0}");
        session.OnWebSocketConnected();
        session.OnWebSocketMessage(
            "{\"trnm\":\"LOGIN\",\"return_code\":0}");
        session.OnWebSocketMessage(
            "{\"trnm\":\"REG\",\"return_code\":0}");

        actions = session.OnReconciliationCompleted(
            false,
            "position mismatch");

        Check(actions.size() == 1,
              "reconciliation failure must enter observe mode");
        Check(actions[0].type ==
                  trading::KiwoomSessionActionType::EnterObserveMode,
              "reconciliation failure action mismatch");
        Check(session.Snapshot().state ==
                  trading::KiwoomSessionState::Faulted,
              "reconciliation failure state mismatch");
        Check(!session.Snapshot().orderSubmissionAllowed,
              "faulted session must block orders");

        actions = session.OnTokenExpired();
        Check(actions.size() == 1,
              "token expiry must request a new token");
        Check(actions[0].type ==
                  trading::KiwoomSessionActionType::RequestToken,
              "token expiry action mismatch");
    }

    void TestLocalMockNoNetwork()
    {
        trading::RuntimeConfig config;
        config.mode = trading::RuntimeMode::LocalMock;

        trading::KiwoomSession session;
        const std::vector<trading::KiwoomSessionAction> actions =
            session.Start(config);

        Check(actions.empty(),
              "LOCAL_MOCK must not start Kiwoom network actions");
        Check(session.Snapshot().state ==
                  trading::KiwoomSessionState::Stopped,
              "LOCAL_MOCK Kiwoom session must remain stopped");
    }
}

int main()
{
    TestHappyPath();
    TestPingAndReconnect();
    TestFailureGates();
    TestLocalMockNoNetwork();

    std::puts("[PASS] kiwoom_session_tests");
    return 0;
}
