#include "stock_pool_minute_client.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <string>

#include "../core/json_lite.h"

namespace trading::stock_pool::platform
{
    namespace
    {
        struct HttpResult final
        {
            bool ok = false;
            unsigned long statusCode = 0UL;
            std::string body;
            std::string error;
        };

        class WinHttpHandle final
        {
        public:
            WinHttpHandle() = default;
            explicit WinHttpHandle(HINTERNET value) : value_(value) {}
            ~WinHttpHandle()
            {
                if (value_ != nullptr) WinHttpCloseHandle(value_);
            }

            WinHttpHandle(const WinHttpHandle&) = delete;
            WinHttpHandle& operator=(const WinHttpHandle&) = delete;

            HINTERNET get() const noexcept { return value_; }

        private:
            HINTERNET value_ = nullptr;
        };

        std::string Trim(std::string value)
        {
            const auto isSpace = [](unsigned char character) {
                return character == ' ' || character == '\t' ||
                    character == '\r' || character == '\n';
            };
            while (!value.empty() &&
                   isSpace(static_cast<unsigned char>(value.front())))
            {
                value.erase(value.begin());
            }
            while (!value.empty() &&
                   isSpace(static_cast<unsigned char>(value.back())))
            {
                value.pop_back();
            }
            if (value.size() >= 2U &&
                ((value.front() == '"' && value.back() == '"') ||
                 (value.front() == '\'' && value.back() == '\'')))
            {
                value = value.substr(1U, value.size() - 2U);
            }
            return value;
        }

        std::map<std::string, std::string> ReadEnvironment(
            const std::string& path)
        {
            std::map<std::string, std::string> values;
            std::ifstream input(path, std::ios::binary);
            if (!input) return values;

            std::string line;
            bool first = true;
            while (std::getline(input, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (first && line.size() >= 3U &&
                    static_cast<unsigned char>(line[0]) == 0xEFU &&
                    static_cast<unsigned char>(line[1]) == 0xBBU &&
                    static_cast<unsigned char>(line[2]) == 0xBFU)
                {
                    line.erase(0U, 3U);
                }
                first = false;
                line = Trim(line);
                if (line.empty() || line.front() == '#') continue;
                const std::size_t equals = line.find('=');
                if (equals == std::string::npos) continue;
                const std::string key = Trim(line.substr(0U, equals));
                const std::string value = Trim(line.substr(equals + 1U));
                if (!key.empty()) values[key] = value;
            }
            return values;
        }

        std::string ProcessEnvironment(const char* key)
        {
            if (key == nullptr || key[0] == '\0') return {};
            const DWORD required = GetEnvironmentVariableA(key, nullptr, 0U);
            if (required == 0U) return {};
            std::string value(static_cast<std::size_t>(required), '\0');
            const DWORD written = GetEnvironmentVariableA(
                key,
                value.data(),
                required);
            if (written == 0U || written >= required) return {};
            value.resize(static_cast<std::size_t>(written));
            return value;
        }

        std::string ResolveBaseUrl(const std::string& environmentFilePath)
        {
            const std::string processValue =
                ProcessEnvironment("STOCK_POOL_SERVER32_BASE_URL");
            if (!processValue.empty()) return Trim(processValue);

            const auto values = ReadEnvironment(environmentFilePath);
            const auto found = values.find("STOCK_POOL_SERVER32_BASE_URL");
            if (found != values.end() && !found->second.empty()) {
                return Trim(found->second);
            }
            return "http://127.0.0.1:8082";
        }

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

        std::string WindowsError(const char* operation)
        {
            return std::string(operation == nullptr ? "WinHTTP" : operation) +
                " failed: Windows error " + std::to_string(GetLastError());
        }

        bool CrackBaseUrl(
            const std::string& baseUrl,
            std::wstring& host,
            INTERNET_PORT& port,
            std::wstring& basePath,
            bool& secure,
            std::string& error)
        {
            const std::wstring wide = Utf8ToWide(baseUrl);
            if (wide.empty()) {
                error = "server32 URL UTF-8 변환 실패: " + baseUrl;
                return false;
            }

            URL_COMPONENTS components{};
            components.dwStructSize = sizeof(components);
            components.dwHostNameLength = static_cast<DWORD>(-1);
            components.dwUrlPathLength = static_cast<DWORD>(-1);
            components.dwExtraInfoLength = static_cast<DWORD>(-1);
            if (!WinHttpCrackUrl(
                    wide.c_str(),
                    static_cast<DWORD>(wide.size()),
                    0U,
                    &components))
            {
                error = WindowsError("WinHttpCrackUrl");
                return false;
            }

            if (components.lpszHostName == nullptr ||
                components.dwHostNameLength == 0U)
            {
                error = "server32 URL host가 없습니다: " + baseUrl;
                return false;
            }

            host.assign(
                components.lpszHostName,
                components.dwHostNameLength);
            port = components.nPort;
            secure = components.nScheme == INTERNET_SCHEME_HTTPS;
            if (components.lpszUrlPath != nullptr &&
                components.dwUrlPathLength > 0U)
            {
                basePath.assign(
                    components.lpszUrlPath,
                    components.dwUrlPathLength);
            }
            if (basePath.empty()) basePath = L"/";
            while (basePath.size() > 1U && basePath.back() == L'/') {
                basePath.pop_back();
            }
            return true;
        }

        std::wstring BuildRequestPath(
            const std::wstring& basePath,
            const std::wstring& endpoint)
        {
            std::wstring result = basePath;
            if (result.empty()) result = L"/";
            if (result.back() == L'/') result.pop_back();
            if (endpoint.empty()) return result.empty() ? L"/" : result;
            if (endpoint.front() != L'/') result.push_back(L'/');
            result.append(endpoint);
            return result;
        }

        std::string PercentEncode(const std::string& value)
        {
            std::ostringstream output;
            output.imbue(std::locale::classic());
            output << std::uppercase << std::hex;
            for (unsigned char character : value) {
                const bool unreserved =
                    (character >= 'A' && character <= 'Z') ||
                    (character >= 'a' && character <= 'z') ||
                    (character >= '0' && character <= '9') ||
                    character == '-' || character == '_' ||
                    character == '.' || character == '~';
                if (unreserved) {
                    output << static_cast<char>(character);
                }
                else {
                    output << '%' << std::setw(2) << std::setfill('0')
                           << static_cast<int>(character);
                }
            }
            return output.str();
        }

        HttpResult GetJson(
            const std::string& baseUrl,
            const std::string& endpoint)
        {
            HttpResult result;
            std::wstring host;
            std::wstring basePath;
            INTERNET_PORT port = 0;
            bool secure = false;
            if (!CrackBaseUrl(
                    baseUrl,
                    host,
                    port,
                    basePath,
                    secure,
                    result.error))
            {
                return result;
            }

            const std::wstring endpointWide = Utf8ToWide(endpoint);
            if (endpointWide.empty()) {
                result.error = "server32 endpoint UTF-8 변환 실패";
                return result;
            }

            WinHttpHandle session(WinHttpOpen(
                L"cppChart-stock-pool-minute-client/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0U));
            if (session.get() == nullptr) {
                result.error = WindowsError("WinHttpOpen");
                return result;
            }
            WinHttpSetTimeouts(session.get(), 5000, 5000, 15000, 60000);

            WinHttpHandle connection(WinHttpConnect(
                session.get(),
                host.c_str(),
                port,
                0U));
            if (connection.get() == nullptr) {
                result.error = WindowsError("WinHttpConnect");
                return result;
            }

            const std::wstring path = BuildRequestPath(basePath, endpointWide);
            const DWORD requestFlags = secure ? WINHTTP_FLAG_SECURE : 0U;
            WinHttpHandle request(WinHttpOpenRequest(
                connection.get(),
                L"GET",
                path.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                requestFlags));
            if (request.get() == nullptr) {
                result.error = WindowsError("WinHttpOpenRequest");
                return result;
            }

            constexpr wchar_t headers[] =
                L"Accept: application/json\r\n";
            if (!WinHttpSendRequest(
                    request.get(),
                    headers,
                    static_cast<DWORD>(-1),
                    WINHTTP_NO_REQUEST_DATA,
                    0U,
                    0U,
                    0U))
            {
                result.error = WindowsError("WinHttpSendRequest");
                return result;
            }
            if (!WinHttpReceiveResponse(request.get(), nullptr)) {
                result.error = WindowsError("WinHttpReceiveResponse");
                return result;
            }

            DWORD statusSize = sizeof(result.statusCode);
            if (!WinHttpQueryHeaders(
                    request.get(),
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    &result.statusCode,
                    &statusSize,
                    WINHTTP_NO_HEADER_INDEX))
            {
                result.error = WindowsError("WinHttpQueryHeaders");
                return result;
            }

            for (;;) {
                DWORD available = 0U;
                if (!WinHttpQueryDataAvailable(request.get(), &available)) {
                    result.error = WindowsError("WinHttpQueryDataAvailable");
                    return result;
                }
                if (available == 0U) break;
                const std::size_t offset = result.body.size();
                result.body.resize(offset + available);
                DWORD read = 0U;
                if (!WinHttpReadData(
                        request.get(),
                        result.body.data() + offset,
                        available,
                        &read))
                {
                    result.error = WindowsError("WinHttpReadData");
                    return result;
                }
                result.body.resize(offset + read);
            }

            result.ok = true;
            return result;
        }

        const json_lite::Value* FindAny(
            const json_lite::Value& object,
            const char* first,
            const char* second = nullptr)
        {
            const json_lite::Value* value = object.Find(first);
            if (value == nullptr && second != nullptr) {
                value = object.Find(second);
            }
            return value;
        }

        bool ParseEnvelopeArray(
            const HttpResult& http,
            json_lite::ParseResult& parsed,
            const json_lite::Value*& data,
            std::string& error)
        {
            if (!http.ok) {
                error = http.error;
                return false;
            }
            parsed = json_lite::Parse(http.body);
            if (!parsed.ok || !parsed.value.IsObject()) {
                error = "server32 JSON 응답 파싱 실패: " + parsed.error;
                return false;
            }

            const json_lite::Value* success =
                FindAny(parsed.value, "Success", "success");
            const json_lite::Value* message =
                FindAny(parsed.value, "Message", "message");
            data = FindAny(parsed.value, "Data", "data");
            if (success == nullptr || !success->AsBoolean(false)) {
                error = message == nullptr
                    ? "server32 API가 실패 응답을 반환했습니다."
                    : message->StringOr("server32 API 실패");
                if (http.statusCode != 0UL) {
                    error += " [HTTP " + std::to_string(http.statusCode) + "]";
                }
                return false;
            }
            if (http.statusCode < 200UL || http.statusCode >= 300UL) {
                error = "server32 HTTP status " +
                    std::to_string(http.statusCode);
                return false;
            }
            if (data == nullptr || !data->IsArray()) {
                error = "server32 분봉 API Data 배열이 없습니다.";
                return false;
            }
            return true;
        }

        std::string DigitsOnly(const std::string& value)
        {
            std::string result;
            result.reserve(value.size());
            for (unsigned char character : value) {
                if (character >= '0' && character <= '9') {
                    result.push_back(static_cast<char>(character));
                }
            }
            return result;
        }

        bool ParseDoubleText(const std::string& text, double& value)
        {
            std::string normalized;
            normalized.reserve(text.size());
            for (unsigned char character : text) {
                if (character == ',' || character == ' ' ||
                    character == '\t' || character == '\r' ||
                    character == '\n')
                {
                    continue;
                }
                normalized.push_back(static_cast<char>(character));
            }
            if (normalized.empty()) return false;
            char* end = nullptr;
            value = std::strtod(normalized.c_str(), &end);
            return end != normalized.c_str() && end != nullptr &&
                *end == '\0' && std::isfinite(value);
        }

        bool ReadNumber(
            const json_lite::Value& object,
            const char* key,
            double& value)
        {
            const json_lite::Value* field = object.Find(key);
            if (field == nullptr) return false;
            if (field->IsNumber()) {
                value = field->AsNumber(0.0);
                return std::isfinite(value);
            }
            if (field->IsString()) {
                return ParseDoubleText(field->AsString(), value);
            }
            return false;
        }

        bool ParsePackedTimestamp(
            const std::string& timestamp,
            EpochMillis& packed)
        {
            if (timestamp.size() != 14U) return false;
            for (unsigned char character : timestamp) {
                if (character < '0' || character > '9') return false;
            }
            char* end = nullptr;
            const long long value = std::strtoll(timestamp.c_str(), &end, 10);
            if (end == timestamp.c_str() || end == nullptr || *end != '\0') {
                return false;
            }
            packed = static_cast<EpochMillis>(value) * 1000LL;
            return true;
        }
    }

    MinuteSeriesFetchResult FetchMinuteSeriesViaServer32(
        const std::string& code,
        const std::string& tradingDate,
        const std::string& captureTime,
        const std::string& environmentFilePath)
    {
        MinuteSeriesFetchResult result;
        const std::string normalizedCode = Trim(code);
        const std::string date = DigitsOnly(tradingDate);
        std::string time = DigitsOnly(captureTime);
        if (normalizedCode.empty()) {
            result.error = "분봉 조회 종목코드가 비어 있습니다.";
            return result;
        }
        if (date.size() != 8U) {
            result.error = "분봉 조회 일자는 YYYY-MM-DD 또는 YYYYMMDD 형식이어야 합니다.";
            return result;
        }
        if (time.size() == 4U) time += "00";
        if (time.size() != 6U) {
            result.error = "포착시각은 HH:mm 또는 HHmmss 형식이어야 합니다.";
            return result;
        }

        const std::string startTimestamp = date + time;
        const std::string endTimestamp = date + "153000";
        if (startTimestamp > endTimestamp) {
            result.error = "포착시각이 장 마감시각 15:30 이후입니다.";
            return result;
        }

        const std::string baseUrl = ResolveBaseUrl(environmentFilePath);
        const std::string endpoint =
            "/api/market/candles/minute?code=" +
            PercentEncode(normalizedCode) +
            "&tick=1&stopTime=" + PercentEncode(startTimestamp);
        const HttpResult http = GetJson(baseUrl, endpoint);
        json_lite::ParseResult parsed;
        const json_lite::Value* data = nullptr;
        if (!ParseEnvelopeArray(http, parsed, data, result.error)) {
            result.error = baseUrl + " | " + normalizedCode + " | " +
                result.error;
            return result;
        }

        result.responseRowCount = data->AsArray().size();
        std::map<EpochMillis, Bar> ordered;
        for (const json_lite::Value& row : data->AsArray()) {
            if (!row.IsObject()) continue;
            const json_lite::Value* timestampField = row.Find("체결시간");
            if (timestampField == nullptr || !timestampField->IsString()) {
                continue;
            }
            const std::string timestamp = timestampField->AsString();
            if (timestamp.size() != 14U ||
                timestamp.compare(0U, 8U, date) != 0 ||
                timestamp < startTimestamp || timestamp > endTimestamp)
            {
                continue;
            }

            double open = 0.0;
            double high = 0.0;
            double low = 0.0;
            double close = 0.0;
            double volume = 0.0;
            if (!ReadNumber(row, "시가", open) ||
                !ReadNumber(row, "고가", high) ||
                !ReadNumber(row, "저가", low) ||
                !ReadNumber(row, "현재가", close) ||
                !ReadNumber(row, "거래량", volume))
            {
                continue;
            }

            open = std::abs(open);
            high = std::abs(high);
            low = std::abs(low);
            close = std::abs(close);
            volume = std::abs(volume);
            if (open <= 0.0 || high <= 0.0 || low <= 0.0 || close <= 0.0) {
                continue;
            }

            EpochMillis packed = 0;
            if (!ParsePackedTimestamp(timestamp, packed)) continue;

            Bar bar;
            bar.closeTimestampMs = packed;
            bar.open = open;
            bar.high = (std::max)({high, open, close});
            bar.low = (std::min)({low, open, close});
            bar.close = close;
            bar.cumulativeTurnover = volume;
            bar.tradeIntensity = 100.0;
            ordered[packed] = bar;
        }

        double cumulativeTurnover = 0.0;
        result.bars.reserve(ordered.size());
        for (auto& pair : ordered) {
            Bar bar = pair.second;
            const double volume = bar.cumulativeTurnover;
            const double typicalPrice =
                (bar.open + bar.high + bar.low + bar.close) * 0.25;
            cumulativeTurnover += typicalPrice * volume;
            bar.cumulativeTurnover = cumulativeTurnover;
            result.bars.push_back(bar);
        }

        result.acceptedRowCount = result.bars.size();
        result.source = baseUrl + "/api/market/candles/minute";
        if (result.bars.empty()) {
            result.error =
                normalizedCode + " | 선택 일자·포착시각 범위의 실제 1분봉이 없습니다.";
            return result;
        }
        result.ok = true;
        return result;
    }
}
