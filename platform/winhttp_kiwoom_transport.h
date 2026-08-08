#pragma once

#include "kiwoom_runtime_runner.h"
#include "winhttp_transport.h"

namespace trading::platform
{
    class WinHttpKiwoomTransport final
        : public IKiwoomRuntimeTransport
    {
    public:
        RuntimeTransportResponse SendRest(
            const std::string& baseUrl,
            const RestRequest& request,
            int timeoutMilliseconds) override;

        bool ConnectWebSocket(
            const std::string& url,
            int timeoutMilliseconds,
            std::string& error) override;

        bool SendWebSocketText(
            const std::string& text,
            std::string& error) override;

        RuntimeReceiveResult ReceiveWebSocket() override;

        void CloseWebSocket() override;

        bool IsWebSocketConnected() const noexcept override;

    private:
        WinHttpRestClient restClient_;
        WinHttpWebSocketClient webSocketClient_;
    };
}
