#include "winhttp_kiwoom_transport.h"

#include <utility>

namespace trading::platform
{
    RuntimeTransportResponse WinHttpKiwoomTransport::SendRest(
        const std::string& baseUrl,
        const RestRequest& request,
        int timeoutMilliseconds)
    {
        const HttpResponse response =
            restClient_.Send(
                baseUrl,
                request,
                timeoutMilliseconds);

        RuntimeTransportResponse result;
        result.transportOk = response.transportOk;
        result.statusCode = response.statusCode;
        result.headers = response.headers;
        result.body = response.body;
        result.error = response.error;
        return result;
    }

    bool WinHttpKiwoomTransport::ConnectWebSocket(
        const std::string& url,
        int timeoutMilliseconds,
        std::string& error)
    {
        return webSocketClient_.Connect(
            url,
            timeoutMilliseconds,
            error);
    }

    bool WinHttpKiwoomTransport::SendWebSocketText(
        const std::string& text,
        std::string& error)
    {
        return webSocketClient_.SendText(text, error);
    }

    RuntimeReceiveResult WinHttpKiwoomTransport::ReceiveWebSocket()
    {
        const WebSocketReceiveResult received =
            webSocketClient_.Receive();

        RuntimeReceiveResult result;
        result.text = received.text;
        result.closeStatus = received.closeStatus;
        result.error = received.error;

        switch (received.kind) {
        case WebSocketReceiveKind::Text:
            result.kind = RuntimeReceiveKind::Text;
            break;
        case WebSocketReceiveKind::Closed:
            result.kind = RuntimeReceiveKind::Closed;
            break;
        default:
            result.kind = RuntimeReceiveKind::Error;
            break;
        }

        return result;
    }

    void WinHttpKiwoomTransport::CloseWebSocket()
    {
        webSocketClient_.Close();
    }

    bool WinHttpKiwoomTransport::IsWebSocketConnected() const noexcept
    {
        return webSocketClient_.IsConnected();
    }
}
