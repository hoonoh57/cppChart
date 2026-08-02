#pragma once

#include "../core/kiwoom_protocol.h"

#include <memory>
#include <string>

namespace trading::platform
{
    struct HttpResponse final
    {
        bool transportOk = false;
        unsigned long statusCode = 0;
        std::string body;
        std::string error;
    };

    class WinHttpRestClient final
    {
    public:
        WinHttpRestClient() = default;
        WinHttpRestClient(const WinHttpRestClient&) = delete;
        WinHttpRestClient& operator=(const WinHttpRestClient&) = delete;

        HttpResponse Send(
            const std::string& baseUrl,
            const RestRequest& request,
            int timeoutMilliseconds = 15000) const;

        HttpResponse PostJson(
            const std::string& fullUrl,
            const std::string& body,
            int timeoutMilliseconds = 15000) const;
    };

    enum class WebSocketReceiveKind
    {
        Text,
        Closed,
        Error
    };

    struct WebSocketReceiveResult final
    {
        WebSocketReceiveKind kind = WebSocketReceiveKind::Error;
        std::string text;
        unsigned short closeStatus = 0;
        std::string error;
    };

    class WinHttpWebSocketClient final
    {
    public:
        WinHttpWebSocketClient();
        ~WinHttpWebSocketClient();

        WinHttpWebSocketClient(const WinHttpWebSocketClient&) = delete;
        WinHttpWebSocketClient& operator=(const WinHttpWebSocketClient&) = delete;

        bool Connect(
            const std::string& url,
            int timeoutMilliseconds,
            std::string& error);

        bool SendText(
            const std::string& text,
            std::string& error);

        WebSocketReceiveResult Receive();

        void Close(
            unsigned short status = 1000,
            const std::string& reason = {});

        bool IsConnected() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
