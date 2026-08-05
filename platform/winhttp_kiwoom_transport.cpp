#include "winhttp_kiwoom_transport.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace trading::platform
{
    namespace
    {
        void ThrottleSymbolCatalogRequest()
        {
            using Clock = std::chrono::steady_clock;
            constexpr auto kMinimumInterval =
                std::chrono::milliseconds(1200);

            static std::mutex mutex;
            static Clock::time_point lastRequestAt{};

            std::lock_guard<std::mutex> lock(mutex);
            const Clock::time_point now = Clock::now();
            if (lastRequestAt != Clock::time_point{}) {
                const Clock::time_point allowedAt =
                    lastRequestAt + kMinimumInterval;
                if (now < allowedAt) {
                    std::this_thread::sleep_until(allowedAt);
                }
            }
            lastRequestAt = Clock::now();
        }
    }

    RuntimeTransportResponse WinHttpKiwoomTransport::SendRest(
        const std::string& baseUrl,
        const RestRequest& request,
        int timeoutMilliseconds)
    {
        if (request.apiId == "ka10099") {
            ThrottleSymbolCatalogRequest();
        }

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

        if (
            request.apiId == "ka10099" &&
            response.transportOk &&
            (response.statusCode < 200 || response.statusCode >= 300))
        {
            std::ostringstream error;
            error << "ka10099 HTTP " << response.statusCode;
            if (!response.body.empty()) {
                constexpr std::size_t kMaxLoggedBody = 512U;
                const std::size_t bodyLength =
                    std::min(response.body.size(), kMaxLoggedBody);
                error << ": " << response.body.substr(0U, bodyLength);
                if (response.body.size() > bodyLength) {
                    error << "...";
                }
            }
            result.transportOk = false;
            result.error = error.str();
        }

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
