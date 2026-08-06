#include "stock_pool_gateway_client.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <string>
#include <utility>

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
            const wchar_t* endpoint)
        {
            std::wstring result = basePath;
            if (result.empty()) result = L"/";
            if (result.back() == L'/') result.pop_back();
            if (endpoint == nullptr || endpoint[0] == L'\0') {
                return result.empty() ? L"/" : result;
            }
            if (endpoint[0] != L'/') result.push_back(L'/');
            result.append(endpoint);
            return result;
        }

        HttpResult PostJson(
            const std::string& baseUrl,
            const wchar_t* endpoint,
            const std::string& body)
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

            WinHttpHandle session(WinHttpOpen(
                L"cppChart-stock-pool-workbench/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0U));
            if (session.get() == nullptr) {
                result.error = WindowsError("WinHttpOpen");
                return result;
            }
            WinHttpSetTimeouts(session.get(), 5000, 5000, 10000, 15000);

            WinHttpHandle connection(WinHttpConnect(
                session.get(),
                host.c_str(),
                port,
                0U));
            if (connection.get() == nullptr) {
                result.error = WindowsError("WinHttpConnect");
                return result;
            }

            const std::wstring path = BuildRequestPath(basePath, endpoint);
            const DWORD requestFlags = secure ? WINHTTP_FLAG_SECURE : 0U;
            WinHttpHandle request(WinHttpOpenRequest(
                connection.get(),
                L"POST",
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
                L"Content-Type: application/json; charset=utf-8\r\n"
                L"Accept: application/json\r\n";
            LPVOID bodyPointer = body.empty()
                ? WINHTTP_NO_REQUEST_DATA
                : const_cast<char*>(body.data());
            if (!WinHttpSendRequest(
                    request.get(),
                    headers,
                    static_cast<DWORD>(-1),
                    bodyPointer,
                    static_cast<DWORD>(body.size()),
                    static_cast<DWORD>(body.size()),
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

        bool ParseEnvelope(
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
            if (data == nullptr || !data->IsObject()) {
                error = "server32 API Data 객체가 없습니다.";
                return false;
            }
            return true;
        }

        std::string BuildResolveBody(const std::vector<std::string>& names)
        {
            std::ostringstream output;
            output.imbue(std::locale::classic());
            output << "{\"names\":[";
            for (std::size_t index = 0U; index < names.size(); ++index) {
                if (index != 0U) output << ',';
                output << json_lite::EscapeString(names[index]);
            }
            output << "]}";
            return output.str();
        }

        void AppendNumber(
            std::ostringstream& output,
            const char* key,
            double value,
            bool& first)
        {
            if (!first) output << ',';
            first = false;
            output << json_lite::EscapeString(key) << ':'
                   << std::setprecision(15) << value;
        }

        std::string BuildCohortBody(
            const std::string& conditionName,
            const std::string& tradingDate,
            const std::string& captureTime,
            int timeframeMinutes,
            const std::vector<import1516::ImportedRow>& rows)
        {
            std::ostringstream output;
            output.imbue(std::locale::classic());
            output << '{'
                   << json_lite::EscapeString("source_type") << ':'
                   << json_lite::EscapeString("kiwoom_1516_clipboard") << ','
                   << json_lite::EscapeString("condition_name") << ':'
                   << json_lite::EscapeString(conditionName) << ','
                   << json_lite::EscapeString("trading_date") << ':'
                   << json_lite::EscapeString(tradingDate) << ','
                   << json_lite::EscapeString("capture_time") << ':'
                   << json_lite::EscapeString(captureTime) << ','
                   << json_lite::EscapeString("timeframe_minutes") << ':'
                   << timeframeMinutes << ','
                   << json_lite::EscapeString("members") << ":[";

            bool firstMember = true;
            for (const auto& row : rows) {
                if (row.status != import1516::ResolutionStatus::Resolved) {
                    continue;
                }
                if (!firstMember) output << ',';
                firstMember = false;
                output << '{';
                bool firstField = true;
                const auto appendString = [&](const char* key, const std::string& value) {
                    if (!firstField) output << ',';
                    firstField = false;
                    output << json_lite::EscapeString(key) << ':'
                           << json_lite::EscapeString(value);
                };
                appendString("code", row.code);
                appendString("name", row.name);
                appendString("market", row.market);
                AppendNumber(
                    output,
                    "return_1m_label",
                    row.return1mPercent,
                    firstField);
                AppendNumber(
                    output,
                    "return_3m_label",
                    row.return3mPercent,
                    firstField);
                AppendNumber(
                    output,
                    "return_7h_label",
                    row.return7hPercent,
                    firstField);
                AppendNumber(
                    output,
                    "maximum_return_label",
                    row.maximumReturnPercent,
                    firstField);
                if (!firstField) output << ',';
                firstField = false;
                output << json_lite::EscapeString("capture_volume") << ':'
                       << row.captureVolume;
                AppendNumber(
                    output,
                    "other_label",
                    row.otherValue,
                    firstField);
                output << '}';
            }
            output << "]}";
            return output.str();
        }

        GatewayRejectedSymbol ParseRejected(
            const json_lite::Value& value)
        {
            GatewayRejectedSymbol result;
            if (!value.IsObject()) return result;
            const json_lite::Value* inputIndex = value.Find("input_index");
            const json_lite::Value* name = value.Find("name");
            const json_lite::Value* reason = value.Find("reason");
            if (inputIndex != nullptr) {
                const int index = inputIndex->AsInt(0);
                result.inputIndex = index < 0
                    ? 0U
                    : static_cast<std::size_t>(index);
            }
            if (name != nullptr) result.name = name->StringOr();
            if (reason != nullptr) result.reason = reason->StringOr();
            return result;
        }
    }

    SymbolResolveResult ResolveSymbolsViaServer32(
        const std::vector<std::string>& names,
        const std::string& environmentFilePath)
    {
        SymbolResolveResult result;
        if (names.empty()) {
            result.error = "종목명 목록이 비어 있습니다.";
            return result;
        }

        const std::string baseUrl = ResolveBaseUrl(environmentFilePath);
        const HttpResult http = PostJson(
            baseUrl,
            L"/api/stock-pool/symbols/resolve",
            BuildResolveBody(names));
        json_lite::ParseResult parsed;
        const json_lite::Value* data = nullptr;
        if (!ParseEnvelope(http, parsed, data, result.error)) {
            result.error = baseUrl + " | " + result.error;
            return result;
        }

        const json_lite::Value* resolved = data->Find("resolved");
        if (resolved != nullptr && resolved->IsArray()) {
            for (const json_lite::Value& value : resolved->AsArray()) {
                if (!value.IsObject()) continue;
                const json_lite::Value* code = value.Find("code");
                const json_lite::Value* name = value.Find("name");
                const json_lite::Value* market = value.Find("market");
                import1516::SymbolMasterEntry entry;
                if (code != nullptr) entry.code = code->StringOr();
                if (name != nullptr) entry.name = name->StringOr();
                if (market != nullptr) entry.market = market->StringOr();
                if (!entry.code.empty() && !entry.name.empty()) {
                    result.entries.push_back(std::move(entry));
                }
            }
        }

        const json_lite::Value* rejected = data->Find("rejected");
        if (rejected != nullptr && rejected->IsArray()) {
            for (const json_lite::Value& value : rejected->AsArray()) {
                result.rejected.push_back(ParseRejected(value));
            }
        }
        const json_lite::Value* source = data->Find("source");
        result.source = source == nullptr
            ? baseUrl
            : source->StringOr(baseUrl);
        result.ok = true;
        return result;
    }

    CohortSaveResult Save1516CohortViaServer32(
        const std::string& conditionName,
        const std::string& tradingDate,
        const std::string& captureTime,
        int timeframeMinutes,
        const std::vector<import1516::ImportedRow>& rows,
        const std::string& environmentFilePath)
    {
        CohortSaveResult result;
        const std::size_t resolvedCount = static_cast<std::size_t>(
            std::count_if(
                rows.begin(),
                rows.end(),
                [](const import1516::ImportedRow& row) {
                    return row.status ==
                        import1516::ResolutionStatus::Resolved;
                }));
        if (resolvedCount == 0U) {
            result.error = "저장할 코드 확정 종목이 없습니다.";
            return result;
        }

        const std::string baseUrl = ResolveBaseUrl(environmentFilePath);
        const HttpResult http = PostJson(
            baseUrl,
            L"/api/stock-pool/cohorts",
            BuildCohortBody(
                conditionName,
                tradingDate,
                captureTime,
                timeframeMinutes,
                rows));
        json_lite::ParseResult parsed;
        const json_lite::Value* data = nullptr;
        if (!ParseEnvelope(http, parsed, data, result.error)) {
            result.error = baseUrl + " | " + result.error;
            return result;
        }

        const json_lite::Value* cohortId = data->Find("cohort_id");
        const json_lite::Value* memberCount = data->Find("member_count");
        const json_lite::Value* rawImportHash =
            data->Find("raw_import_hash");
        const json_lite::Value* source = data->Find("source");
        result.cohortId = cohortId == nullptr
            ? 0LL
            : static_cast<long long>(cohortId->AsNumber(0.0));
        result.memberCount = memberCount == nullptr
            ? 0U
            : static_cast<std::size_t>(
                (std::max)(0, memberCount->AsInt(0)));
        if (rawImportHash != nullptr) {
            result.rawImportHash = rawImportHash->StringOr();
        }
        result.source = source == nullptr
            ? baseUrl
            : source->StringOr(baseUrl);

        const json_lite::Value* rejected = data->Find("rejected");
        if (rejected != nullptr && rejected->IsArray()) {
            for (const json_lite::Value& value : rejected->AsArray()) {
                result.rejected.push_back(ParseRejected(value));
            }
        }
        if (result.cohortId <= 0LL || result.memberCount == 0U) {
            result.error = "server32가 유효한 cohort_id/member_count를 반환하지 않았습니다.";
            return result;
        }
        result.ok = true;
        return result;
    }
}
