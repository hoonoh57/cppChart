#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif

#include "winhttp_transport.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

namespace trading::platform
{
    namespace
    {
        class InternetHandle final
        {
        public:
            InternetHandle() = default;

            explicit InternetHandle(HINTERNET value) noexcept
                : value_(value)
            {
            }

            ~InternetHandle()
            {
                Reset();
            }

            InternetHandle(const InternetHandle&) = delete;
            InternetHandle& operator=(const InternetHandle&) = delete;

            InternetHandle(InternetHandle&& other) noexcept
                : value_(other.Release())
            {
            }

            InternetHandle& operator=(InternetHandle&& other) noexcept
            {
                if (this != &other) {
                    Reset(other.Release());
                }
                return *this;
            }

            HINTERNET Get() const noexcept
            {
                return value_;
            }

            explicit operator bool() const noexcept
            {
                return value_ != nullptr;
            }

            HINTERNET Release() noexcept
            {
                HINTERNET value = value_;
                value_ = nullptr;
                return value;
            }

            void Reset(HINTERNET value = nullptr) noexcept
            {
                if (value_ != nullptr) {
                    WinHttpCloseHandle(value_);
                }
                value_ = value;
            }

        private:
            HINTERNET value_ = nullptr;
        };

        struct UrlParts final
        {
            std::wstring host;
            std::wstring path;
            INTERNET_PORT port = 0;
            bool secure = false;
        };

        std::wstring Utf8ToWide(const std::string& value)
        {
            if (value.empty()) return {};

            const int length = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);

            if (length <= 0) return {};

            std::wstring result(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                result.data(),
                length);
            return result;
        }

        std::string WideToUtf8(const std::wstring& value)
        {
            if (value.empty()) return {};

            const int length = WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);

            if (length <= 0) return {};

            std::string result(static_cast<std::size_t>(length), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                result.data(),
                length,
                nullptr,
                nullptr);
            return result;
        }

        std::string WindowsErrorMessage(DWORD error)
        {
            wchar_t* buffer = nullptr;
            const DWORD length = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                    FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<wchar_t*>(&buffer),
                0,
                nullptr);

            std::wstring message;
            if (length > 0 && buffer != nullptr) {
                message.assign(buffer, buffer + length);
                LocalFree(buffer);

                while (
                    !message.empty() &&
                    (message.back() == L'\r' ||
                     message.back() == L'\n' ||
                     message.back() == L' '))
                {
                    message.pop_back();
                }
            }

            std::ostringstream out;
            out << "WinHTTP error " << error;
            const std::string text = WideToUtf8(message);
            if (!text.empty()) out << ": " << text;
            return out.str();
        }

        bool CrackUrl(
            const std::string& source,
            bool webSocket,
            UrlParts& parts,
            std::string& error)
        {
            std::string normalized = source;
            bool secureWebSocket = false;

            if (webSocket) {
                if (normalized.rfind("wss://", 0) == 0) {
                    secureWebSocket = true;
                    normalized.replace(0, 6, "https://");
                }
                else if (normalized.rfind("ws://", 0) == 0) {
                    normalized.replace(0, 5, "http://");
                }
                else {
                    error = "WebSocket URL must use ws:// or wss://";
                    return false;
                }
            }

            const std::wstring url = Utf8ToWide(normalized);
            if (url.empty()) {
                error = "URL is empty or is not valid UTF-8";
                return false;
            }

            URL_COMPONENTS components{};
            components.dwStructSize = sizeof(components);
            components.dwSchemeLength = static_cast<DWORD>(-1);
            components.dwHostNameLength = static_cast<DWORD>(-1);
            components.dwUrlPathLength = static_cast<DWORD>(-1);
            components.dwExtraInfoLength = static_cast<DWORD>(-1);

            if (!WinHttpCrackUrl(
                    url.c_str(),
                    static_cast<DWORD>(url.size()),
                    0,
                    &components))
            {
                error = WindowsErrorMessage(GetLastError());
                return false;
            }

            parts.host.assign(
                components.lpszHostName,
                components.dwHostNameLength);

            if (
                components.lpszUrlPath != nullptr &&
                components.dwUrlPathLength > 0)
            {
                parts.path.assign(
                    components.lpszUrlPath,
                    components.dwUrlPathLength);
            }
            else {
                parts.path = L"/";
            }

            if (
                components.lpszExtraInfo != nullptr &&
                components.dwExtraInfoLength > 0)
            {
                parts.path.append(
                    components.lpszExtraInfo,
                    components.dwExtraInfoLength);
            }

            parts.port = components.nPort;
            parts.secure = webSocket
                ? secureWebSocket
                : components.nScheme == INTERNET_SCHEME_HTTPS;

            if (parts.host.empty()) {
                error = "URL host is empty";
                return false;
            }

            error.clear();
            return true;
        }

        std::string JoinUrl(
            const std::string& baseUrl,
            const std::string& path)
        {
            if (baseUrl.empty()) return path;
            if (path.empty()) return baseUrl;

            const bool baseSlash = baseUrl.back() == '/';
            const bool pathSlash = path.front() == '/';

            if (baseSlash && pathSlash) {
                return baseUrl + path.substr(1);
            }
            if (!baseSlash && !pathSlash) {
                return baseUrl + '/' + path;
            }
            return baseUrl + path;
        }

        std::wstring BuildHeaderBlock(
            const std::map<std::string, std::string>& headers)
        {
            std::wstring result;

            for (const auto& entry : headers) {
                const std::wstring name = Utf8ToWide(entry.first);
                const std::wstring value = Utf8ToWide(entry.second);

                result += name;
                result += L": ";
                result += value;
                result += L"\r\n";
            }

            return result;
        }

        HttpResponse SendFullRequest(
            const std::string& method,
            const std::string& fullUrl,
            const std::map<std::string, std::string>& headers,
            const std::string& body,
            int timeoutMilliseconds)
        {
            HttpResponse response;
            UrlParts url;

            if (!CrackUrl(fullUrl, false, url, response.error)) {
                return response;
            }

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY WINHTTP_ACCESS_TYPE_DEFAULT_PROXY
#endif

            InternetHandle session(WinHttpOpen(
                L"cppChart/1.0",
                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0));

            if (!session) {
                response.error = WindowsErrorMessage(GetLastError());
                return response;
            }

            WinHttpSetTimeouts(
                session.Get(),
                timeoutMilliseconds,
                timeoutMilliseconds,
                timeoutMilliseconds,
                timeoutMilliseconds);

            InternetHandle connection(WinHttpConnect(
                session.Get(),
                url.host.c_str(),
                url.port,
                0));

            if (!connection) {
                response.error = WindowsErrorMessage(GetLastError());
                return response;
            }

            const std::wstring methodWide = Utf8ToWide(method);
            const DWORD flags = url.secure
                ? WINHTTP_FLAG_SECURE
                : 0;

            InternetHandle request(WinHttpOpenRequest(
                connection.Get(),
                methodWide.c_str(),
                url.path.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                flags));

            if (!request) {
                response.error = WindowsErrorMessage(GetLastError());
                return response;
            }

            const std::wstring headerBlock = BuildHeaderBlock(headers);
            const wchar_t* headerPointer = headerBlock.empty()
                ? WINHTTP_NO_ADDITIONAL_HEADERS
                : headerBlock.c_str();
            const DWORD headerLength = headerBlock.empty()
                ? 0
                : static_cast<DWORD>(headerBlock.size());

            LPVOID bodyPointer = body.empty()
                ? WINHTTP_NO_REQUEST_DATA
                : const_cast<char*>(body.data());
            const DWORD bodyLength =
                static_cast<DWORD>(body.size());

            if (!WinHttpSendRequest(
                    request.Get(),
                    headerPointer,
                    headerLength,
                    bodyPointer,
                    bodyLength,
                    bodyLength,
                    0))
            {
                response.error = WindowsErrorMessage(GetLastError());
                return response;
            }

            if (!WinHttpReceiveResponse(request.Get(), nullptr)) {
                response.error = WindowsErrorMessage(GetLastError());
                return response;
            }

            DWORD statusCode = 0;
            DWORD statusSize = sizeof(statusCode);

            if (!WinHttpQueryHeaders(
                    request.Get(),
                    WINHTTP_QUERY_STATUS_CODE |
                        WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    &statusCode,
                    &statusSize,
                    WINHTTP_NO_HEADER_INDEX))
            {
                response.error = WindowsErrorMessage(GetLastError());
                return response;
            }

            response.statusCode = statusCode;

            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(
                        request.Get(),
                        &available))
                {
                    response.error = WindowsErrorMessage(GetLastError());
                    return response;
                }

                if (available == 0) break;

                std::vector<char> buffer(available);
                DWORD read = 0;

                if (!WinHttpReadData(
                        request.Get(),
                        buffer.data(),
                        available,
                        &read))
                {
                    response.error = WindowsErrorMessage(GetLastError());
                    return response;
                }

                response.body.append(buffer.data(), read);
            }

            response.transportOk = true;
            response.error.clear();
            return response;
        }
    }

    HttpResponse WinHttpRestClient::Send(
        const std::string& baseUrl,
        const RestRequest& request,
        int timeoutMilliseconds) const
    {
        return SendFullRequest(
            request.method,
            JoinUrl(baseUrl, request.path),
            request.headers,
            request.body,
            timeoutMilliseconds);
    }

    HttpResponse WinHttpRestClient::PostJson(
        const std::string& fullUrl,
        const std::string& body,
        int timeoutMilliseconds) const
    {
        return SendFullRequest(
            "POST",
            fullUrl,
            {
                {
                    "content-type",
                    "application/json;charset=UTF-8"
                }
            },
            body,
            timeoutMilliseconds);
    }

    struct WinHttpWebSocketClient::Impl final
    {
        std::mutex stateMutex;
        std::mutex sendMutex;
        InternetHandle session;
        InternetHandle connection;
        InternetHandle socket;
        std::atomic<bool> connected{ false };
    };

    WinHttpWebSocketClient::WinHttpWebSocketClient()
        : impl_(std::make_unique<Impl>())
    {
    }

    WinHttpWebSocketClient::~WinHttpWebSocketClient()
    {
        Close();
    }

    bool WinHttpWebSocketClient::Connect(
        const std::string& urlText,
        int timeoutMilliseconds,
        std::string& error)
    {
        Close();

        UrlParts url;
        if (!CrackUrl(urlText, true, url, error)) {
            return false;
        }

        std::lock_guard<std::mutex> stateLock(impl_->stateMutex);

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY WINHTTP_ACCESS_TYPE_DEFAULT_PROXY
#endif

        impl_->session.Reset(WinHttpOpen(
            L"cppChart/1.0",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0));

        if (!impl_->session) {
            error = WindowsErrorMessage(GetLastError());
            return false;
        }

        WinHttpSetTimeouts(
            impl_->session.Get(),
            timeoutMilliseconds,
            timeoutMilliseconds,
            timeoutMilliseconds,
            timeoutMilliseconds);

        impl_->connection.Reset(WinHttpConnect(
            impl_->session.Get(),
            url.host.c_str(),
            url.port,
            0));

        if (!impl_->connection) {
            error = WindowsErrorMessage(GetLastError());
            impl_->session.Reset();
            return false;
        }

        const DWORD flags = url.secure
            ? WINHTTP_FLAG_SECURE
            : 0;

        InternetHandle request(WinHttpOpenRequest(
            impl_->connection.Get(),
            L"GET",
            url.path.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags));

        if (!request) {
            error = WindowsErrorMessage(GetLastError());
            impl_->connection.Reset();
            impl_->session.Reset();
            return false;
        }

        if (!WinHttpSetOption(
                request.Get(),
                WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                nullptr,
                0))
        {
            error = WindowsErrorMessage(GetLastError());
            impl_->connection.Reset();
            impl_->session.Reset();
            return false;
        }

        if (!WinHttpSendRequest(
                request.Get(),
                WINHTTP_NO_ADDITIONAL_HEADERS,
                0,
                WINHTTP_NO_REQUEST_DATA,
                0,
                0,
                0))
        {
            error = WindowsErrorMessage(GetLastError());
            impl_->connection.Reset();
            impl_->session.Reset();
            return false;
        }

        if (!WinHttpReceiveResponse(request.Get(), nullptr)) {
            error = WindowsErrorMessage(GetLastError());
            impl_->connection.Reset();
            impl_->session.Reset();
            return false;
        }

        HINTERNET socket = WinHttpWebSocketCompleteUpgrade(
            request.Get(),
            0);

        if (socket == nullptr) {
            error = WindowsErrorMessage(GetLastError());
            impl_->connection.Reset();
            impl_->session.Reset();
            return false;
        }

        impl_->socket.Reset(socket);
        impl_->connected.store(true, std::memory_order_release);
        error.clear();
        return true;
    }

    bool WinHttpWebSocketClient::SendText(
        const std::string& text,
        std::string& error)
    {
        std::lock_guard<std::mutex> sendLock(impl_->sendMutex);

        if (!impl_->connected.load(std::memory_order_acquire)) {
            error = "WebSocket is not connected";
            return false;
        }

        const DWORD result = WinHttpWebSocketSend(
            impl_->socket.Get(),
            WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
            const_cast<char*>(text.data()),
            static_cast<DWORD>(text.size()));

        if (result != ERROR_SUCCESS) {
            error = WindowsErrorMessage(result);
            impl_->connected.store(false, std::memory_order_release);
            return false;
        }

        error.clear();
        return true;
    }

    WebSocketReceiveResult WinHttpWebSocketClient::Receive()
    {
        WebSocketReceiveResult result;

        if (!impl_->connected.load(std::memory_order_acquire)) {
            result.error = "WebSocket is not connected";
            return result;
        }

        std::vector<char> complete;
        std::vector<char> buffer(8192);

        for (;;) {
            DWORD read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type =
                WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE;

            const DWORD receiveResult = WinHttpWebSocketReceive(
                impl_->socket.Get(),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &read,
                &type);

            if (receiveResult != ERROR_SUCCESS) {
                result.error = WindowsErrorMessage(receiveResult);
                result.kind = WebSocketReceiveKind::Error;
                impl_->connected.store(false, std::memory_order_release);
                return result;
            }

            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                USHORT status = 0;
                std::vector<char> reason(256);
                DWORD reasonLength = 0;

                const DWORD queryResult =
                    WinHttpWebSocketQueryCloseStatus(
                        impl_->socket.Get(),
                        &status,
                        reason.data(),
                        static_cast<DWORD>(reason.size()),
                        &reasonLength);

                result.kind = WebSocketReceiveKind::Closed;
                result.closeStatus = status;

                if (queryResult == ERROR_SUCCESS && reasonLength > 0) {
                    result.text.assign(
                        reason.data(),
                        reason.data() + reasonLength);
                }

                impl_->connected.store(false, std::memory_order_release);
                return result;
            }

            if (
                type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE ||
                type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
            {
                result.error = "binary WebSocket message is not supported";
                result.kind = WebSocketReceiveKind::Error;
                return result;
            }

            complete.insert(
                complete.end(),
                buffer.begin(),
                buffer.begin() + read);

            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                result.kind = WebSocketReceiveKind::Text;
                result.text.assign(complete.begin(), complete.end());
                return result;
            }
        }
    }

    void WinHttpWebSocketClient::Close(
        unsigned short status,
        const std::string& reason)
    {
        std::lock_guard<std::mutex> stateLock(impl_->stateMutex);
        std::lock_guard<std::mutex> sendLock(impl_->sendMutex);

        const bool wasConnected =
            impl_->connected.exchange(false, std::memory_order_acq_rel);

        if (wasConnected && impl_->socket) {
            const DWORD reasonLength = static_cast<DWORD>(
                (std::min<std::size_t>)(reason.size(), 123));

            WinHttpWebSocketClose(
                impl_->socket.Get(),
                status,
                reasonLength > 0
                    ? const_cast<char*>(reason.data())
                    : nullptr,
                reasonLength);
        }

        impl_->socket.Reset();
        impl_->connection.Reset();
        impl_->session.Reset();
    }

    bool WinHttpWebSocketClient::IsConnected() const noexcept
    {
        return impl_->connected.load(std::memory_order_acquire);
    }
}
