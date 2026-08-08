#include "../platform/winhttp_transport.h"

#include <cstdio>
#include <cstdlib>
#include <string>

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

    void TestRestValidation()
    {
        trading::platform::WinHttpRestClient client;

        const trading::platform::HttpResponse invalid =
            client.PostJson(
                "not-a-valid-url",
                "{}",
                1000);

        Check(!invalid.transportOk,
              "invalid REST URL must fail before network access");
        Check(!invalid.error.empty(),
              "invalid REST URL must return an error");

        trading::RestRequest request;
        request.method = "POST";
        request.path = "/api/dostk/ordr";
        request.headers["content-type"] =
            "application/json;charset=UTF-8";
        request.body = "{}";

        const trading::platform::HttpResponse invalidBase =
            client.Send("invalid-base", request, 1000);

        Check(!invalidBase.transportOk,
              "invalid REST base URL must fail");
    }

    void TestWebSocketValidation()
    {
        trading::platform::WinHttpWebSocketClient socket;
        Check(!socket.IsConnected(),
              "new WebSocket client must be disconnected");

        std::string error;
        Check(!socket.SendText("{}", error),
              "send on a disconnected WebSocket must fail");
        Check(!error.empty(),
              "disconnected send must provide an error");

        error.clear();
        Check(!socket.Connect(
                  "https://mockapi.kiwoom.com/api/dostk/websocket",
                  1000,
                  error),
              "WebSocket URL without ws/wss scheme must fail");
        Check(!error.empty(),
              "invalid WebSocket scheme must provide an error");

        socket.Close();
        Check(!socket.IsConnected(),
              "closing a disconnected WebSocket must remain safe");
    }
}

int main()
{
    TestRestValidation();
    TestWebSocketValidation();

    std::puts("[PASS] winhttp_transport_tests");
    return 0;
}
