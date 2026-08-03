#include "kiwoom_market_data.h"

#include "json_lite.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>

namespace trading
{
    namespace
    {
        constexpr const char* ChartPath = "/api/dostk/chart";
        constexpr const char* StockMinuteApiId = "ka10080";
        constexpr const char* IndexMinuteApiId = "ka20005";

        std::string Trim(const std::string& value)
        {
            std::size_t first = 0;
            while (
                first < value.size() &&
                std::isspace(
                    static_cast<unsigned char>(value[first])) != 0)
            {
                ++first;
            }

            std::size_t last = value.size();
            while (
                last > first &&
                std::isspace(
                    static_cast<unsigned char>(value[last - 1])) != 0)
            {
                --last;
            }

            return value.substr(first, last - first);
        }

        bool ScalarToString(
            const json_lite::Value* value,
            std::string& out)
        {
            if (value == nullptr) return false;
            if (value->IsString()) {
                out = value->AsString();
                return true;
            }
            if (value->IsNumber()) {
                std::ostringstream stream;
                stream.precision(15);
                stream << value->AsNumber();
                out = stream.str();
                return true;
            }
            return false;
        }

        bool TryParseInteger64(
            const json_lite::Value* value,
            std::int64_t& out)
        {
            std::string text;
            if (!ScalarToString(value, text)) return false;

            text = Trim(text);
            if (text.empty()) return false;

            std::string normalized;
            normalized.reserve(text.size());
            for (char ch : text) {
                if (ch != ',') normalized.push_back(ch);
            }

            try {
                std::size_t consumed = 0;
                const long long parsed =
                    std::stoll(normalized, &consumed, 10);
                if (consumed != normalized.size()) return false;
                out = static_cast<std::int64_t>(parsed);
                return true;
            }
            catch (...) {
                return false;
            }
        }

        bool TryReadAbsolutePrice(
            const json_lite::Value* value,
            PriceWon& out)
        {
            std::int64_t parsed = 0;
            if (!TryParseInteger64(value, parsed)) return false;
            if (parsed == (std::numeric_limits<std::int64_t>::min)()) {
                return false;
            }
            const std::int64_t absolute = parsed < 0 ? -parsed : parsed;
            if (
                absolute <= 0 ||
                absolute > (std::numeric_limits<PriceWon>::max)())
            {
                return false;
            }
            out = static_cast<PriceWon>(absolute);
            return true;
        }

        bool TryReadAbsoluteVolume(
            const json_lite::Value* value,
            Volume& out)
        {
            std::int64_t parsed = 0;
            if (!TryParseInteger64(value, parsed)) return false;
            if (parsed == (std::numeric_limits<std::int64_t>::min)()) {
                return false;
            }
            out = parsed < 0 ? -parsed : parsed;
            return true;
        }

        bool IsLeapYear(int year) noexcept
        {
            return
                (year % 4 == 0 && year % 100 != 0) ||
                year % 400 == 0;
        }

        int DaysInMonth(int year, int month) noexcept
        {
            static constexpr int days[] = {
                31, 28, 31, 30, 31, 30,
                31, 31, 30, 31, 30, 31};
            if (month < 1 || month > 12) return 0;
            if (month == 2 && IsLeapYear(year)) return 29;
            return days[month - 1];
        }

        std::int64_t DaysFromCivil(
            int year,
            unsigned month,
            unsigned day) noexcept
        {
            year -= month <= 2;
            const int era =
                (year >= 0 ? year : year - 399) / 400;
            const unsigned yearOfEra =
                static_cast<unsigned>(year - era * 400);
            const unsigned dayOfYear =
                (153 * (month + (month > 2 ? -3 : 9)) + 2) /
                    5 +
                day - 1;
            const unsigned dayOfEra =
                yearOfEra * 365 + yearOfEra / 4 -
                yearOfEra / 100 + dayOfYear;
            return
                static_cast<std::int64_t>(era) * 146097 +
                static_cast<std::int64_t>(dayOfEra) -
                719468;
        }

        bool TryParseKstTimestamp(
            const json_lite::Value* value,
            EpochMillis& out)
        {
            std::string text;
            if (!ScalarToString(value, text)) return false;
            text = Trim(text);

            if (text.size() != 12 && text.size() != 14) return false;
            for (char ch : text) {
                if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
                    return false;
                }
            }

            const auto parsePart = [&text](
                std::size_t offset,
                std::size_t count) -> int {
                return std::stoi(text.substr(offset, count));
            };

            try {
                const int year = parsePart(0, 4);
                const int month = parsePart(4, 2);
                const int day = parsePart(6, 2);
                const int hour = parsePart(8, 2);
                const int minute = parsePart(10, 2);
                const int second =
                    text.size() == 14 ? parsePart(12, 2) : 0;

                if (
                    year < 1970 || year > 9999 ||
                    month < 1 || month > 12 ||
                    day < 1 || day > DaysInMonth(year, month) ||
                    hour < 0 || hour > 23 ||
                    minute < 0 || minute > 59 ||
                    second < 0 || second > 59)
                {
                    return false;
                }

                const std::int64_t days = DaysFromCivil(
                    year,
                    static_cast<unsigned>(month),
                    static_cast<unsigned>(day));
                const std::int64_t localSeconds =
                    days * 86400 +
                    static_cast<std::int64_t>(hour) * 3600 +
                    static_cast<std::int64_t>(minute) * 60 +
                    second;
                const std::int64_t utcSeconds =
                    localSeconds - 9 * 3600;
                out = utcSeconds * 1000;
                return true;
            }
            catch (...) {
                return false;
            }
        }

        bool TryReadReturnCode(
            const json_lite::Value& root,
            int& out)
        {
            const json_lite::Value* value = root.Find("return_code");
            if (value == nullptr) {
                out = 0;
                return true;
            }

            std::int64_t parsed = 0;
            if (!TryParseInteger64(value, parsed)) return false;
            if (
                parsed < (std::numeric_limits<int>::min)() ||
                parsed > (std::numeric_limits<int>::max)())
            {
                return false;
            }
            out = static_cast<int>(parsed);
            return true;
        }

        ProtocolResult ParseProtocolResult(
            const json_lite::Value& root)
        {
            ProtocolResult result;
            int returnCode = 0;
            if (!TryReadReturnCode(root, returnCode)) {
                result.error = "return_code has an invalid type";
                return result;
            }

            result.returnCode = returnCode;
            const json_lite::Value* message = root.Find("return_msg");
            if (message != nullptr && message->IsString()) {
                result.returnMessage = message->AsString();
            }

            result.ok = returnCode == 0;
            if (!result.ok && result.returnMessage.empty()) {
                result.returnMessage =
                    "Kiwoom market-data API returned an error";
            }
            return result;
        }

        RestRequest BuildMinuteBarsRequest(
            MinuteBarInstrument instrument,
            const std::string& code,
            int minuteUnit,
            const std::string& bearerToken,
            bool adjustedPrice,
            const Continuation& continuation,
            std::string& error)
        {
            RestRequest request;
            if (code.empty()) {
                error = instrument == MinuteBarInstrument::Stock
                    ? "stock code is required"
                    : "index code is required";
                return request;
            }
            if (!IsSupportedMinuteUnit(minuteUnit)) {
                error = "unsupported minute unit";
                return request;
            }
            if (bearerToken.empty()) {
                error = "bearer token is required";
                return request;
            }

            request.method = "POST";
            request.path = ChartPath;
            request.apiId =
                instrument == MinuteBarInstrument::Stock
                    ? StockMinuteApiId
                    : IndexMinuteApiId;
            request.headers["authorization"] =
                "Bearer " + bearerToken;
            request.headers["api-id"] = request.apiId;
            request.headers["content-type"] =
                "application/json;charset=UTF-8";

            if (!continuation.continueYn.empty()) {
                request.headers["cont-yn"] =
                    continuation.continueYn;
            }
            if (!continuation.nextKey.empty()) {
                request.headers["next-key"] =
                    continuation.nextKey;
            }

            std::ostringstream body;
            body << '{';
            if (instrument == MinuteBarInstrument::Stock) {
                body
                    << "\"stk_cd\":"
                    << json_lite::EscapeString(code)
                    << ',';
            }
            else {
                body
                    << "\"inds_cd\":"
                    << json_lite::EscapeString(code)
                    << ',';
            }
            body
                << "\"tic_scope\":"
                << json_lite::EscapeString(
                    std::to_string(minuteUnit));
            if (instrument == MinuteBarInstrument::Stock) {
                body
                    << ",\"upd_stkpc_tp\":"
                    << json_lite::EscapeString(
                        adjustedPrice ? "1" : "0");
            }
            body << '}';
            request.body = body.str();

            error.clear();
            return request;
        }

        bool BarsEqual(
            const Bar& left,
            const Bar& right) noexcept
        {
            return
                left.open == right.open &&
                left.high == right.high &&
                left.low == right.low &&
                left.close == right.close &&
                left.volume == right.volume &&
                left.closeTimestampMs == right.closeTimestampMs;
        }

        MinuteBarsPage ParseMinuteBarsResponse(
            MinuteBarInstrument instrument,
            const std::string& code,
            int minuteUnit,
            const std::string& arrayKey,
            const std::string& json)
        {
            MinuteBarsPage response;
            response.instrument = instrument;
            response.code = code;
            response.minuteUnit = minuteUnit;

            const json_lite::ParseResult parsed =
                json_lite::Parse(json);
            if (!parsed.ok) {
                std::ostringstream message;
                message
                    << "JSON parse error at byte "
                    << parsed.errorOffset
                    << ": "
                    << parsed.error;
                response.result.error = message.str();
                return response;
            }
            if (!parsed.value.IsObject()) {
                response.result.error =
                    "market-data JSON root must be an object";
                return response;
            }

            response.result = ParseProtocolResult(parsed.value);
            if (!response.result.ok) return response;

            const json_lite::Value* rows =
                parsed.value.Find(arrayKey);
            if (rows == nullptr) {
                response.result.ok = false;

                if (
                    instrument == MinuteBarInstrument::Stock &&
                    parsed.value.Find("stk_tic_chart_qry") != nullptr)
                {
                    response.result.error =
                        "received stock tick-chart response from api-id ka10079; "
                        "stock minute bars require api-id ka10080";
                    return response;
                }

                if (
                    instrument == MinuteBarInstrument::Index &&
                    (
                        parsed.value.Find("inds_tic_pole_qry") != nullptr ||
                        parsed.value.Find("inds_tic_chart_qry") != nullptr))
                {
                    response.result.error =
                        "received index tick-chart response from api-id ka20004; "
                        "index minute bars require api-id ka20005";
                    return response;
                }

                std::ostringstream message;
                message
                    << "required response array is missing: "
                    << arrayKey
                    << "; response keys=";

                bool firstKey = true;
                for (const auto& entry : parsed.value.AsObject()) {
                    if (!firstKey) message << ',';
                    message << entry.first;
                    firstKey = false;
                }

                response.result.error = message.str();
                return response;
            }
            if (!rows->IsArray()) {
                response.result.ok = false;
                response.result.error =
                    "response field is not an array: " + arrayKey;
                return response;
            }
            if (rows->AsArray().empty()) {
                response.result.ok = false;
                response.result.error =
                    "minute-bar response contains no rows";
                return response;
            }

            response.bars.reserve(rows->AsArray().size());
            for (
                std::size_t index = 0;
                index < rows->AsArray().size();
                ++index)
            {
                const json_lite::Value& row =
                    rows->AsArray()[index];
                if (!row.IsObject()) {
                    response.result.ok = false;
                    response.result.error =
                        "minute-bar row is not an object at index " +
                        std::to_string(index);
                    response.bars.clear();
                    return response;
                }

                Bar bar;
                if (!TryReadAbsolutePrice(row.Find("open_pric"), bar.open)) {
                    response.result.ok = false;
                    response.result.error =
                        "invalid open_pric at row " +
                        std::to_string(index);
                }
                else if (!TryReadAbsolutePrice(row.Find("high_pric"), bar.high)) {
                    response.result.ok = false;
                    response.result.error =
                        "invalid high_pric at row " +
                        std::to_string(index);
                }
                else if (!TryReadAbsolutePrice(row.Find("low_pric"), bar.low)) {
                    response.result.ok = false;
                    response.result.error =
                        "invalid low_pric at row " +
                        std::to_string(index);
                }
                else if (!TryReadAbsolutePrice(row.Find("cur_prc"), bar.close)) {
                    response.result.ok = false;
                    response.result.error =
                        "invalid cur_prc at row " +
                        std::to_string(index);
                }
                else if (!TryReadAbsoluteVolume(row.Find("trde_qty"), bar.volume)) {
                    response.result.ok = false;
                    response.result.error =
                        "invalid trde_qty at row " +
                        std::to_string(index);
                }
                else if (!TryParseKstTimestamp(
                             row.Find("cntr_tm"),
                             bar.closeTimestampMs))
                {
                    response.result.ok = false;
                    response.result.error =
                        "invalid cntr_tm at row " +
                        std::to_string(index);
                }

                if (!response.result.ok) {
                    response.bars.clear();
                    return response;
                }

                if (
                    bar.high < (std::max)(bar.open, bar.close) ||
                    bar.low > (std::min)(bar.open, bar.close) ||
                    bar.high < bar.low)
                {
                    response.result.ok = false;
                    response.result.error =
                        "OHLC invariant violation at row " +
                        std::to_string(index);
                    response.bars.clear();
                    return response;
                }

                response.bars.push_back(bar);
            }

            std::sort(
                response.bars.begin(),
                response.bars.end(),
                [](const Bar& left, const Bar& right) {
                    return
                        left.closeTimestampMs <
                        right.closeTimestampMs;
                });

            std::vector<Bar> unique;
            unique.reserve(response.bars.size());
            for (const Bar& bar : response.bars) {
                if (
                    !unique.empty() &&
                    unique.back().closeTimestampMs ==
                        bar.closeTimestampMs)
                {
                    if (!BarsEqual(unique.back(), bar)) {
                        response.result.ok = false;
                        response.result.error =
                            "conflicting duplicate minute-bar timestamp";
                        response.bars.clear();
                        return response;
                    }
                    continue;
                }
                unique.push_back(bar);
            }
            response.bars = std::move(unique);
            response.result.ok = true;
            response.result.error.clear();
            return response;
        }
    }

    bool IsSupportedMinuteUnit(int minuteUnit) noexcept
    {
        switch (minuteUnit) {
        case 1:
        case 3:
        case 5:
        case 10:
        case 15:
        case 30:
        case 45:
        case 60:
            return true;
        default:
            return false;
        }
    }

    RestRequest BuildStockMinuteBarsRestRequest(
        const std::string& stockCode,
        int minuteUnit,
        const std::string& bearerToken,
        bool adjustedPrice,
        const Continuation& continuation,
        std::string& error)
    {
        return BuildMinuteBarsRequest(
            MinuteBarInstrument::Stock,
            stockCode,
            minuteUnit,
            bearerToken,
            adjustedPrice,
            continuation,
            error);
    }

    RestRequest BuildIndexMinuteBarsRestRequest(
        const std::string& indexCode,
        int minuteUnit,
        const std::string& bearerToken,
        const Continuation& continuation,
        std::string& error)
    {
        return BuildMinuteBarsRequest(
            MinuteBarInstrument::Index,
            indexCode,
            minuteUnit,
            bearerToken,
            false,
            continuation,
            error);
    }

    MinuteBarsPage ParseStockMinuteBarsResponse(
        const std::string& stockCode,
        int minuteUnit,
        const std::string& json)
    {
        return ParseMinuteBarsResponse(
            MinuteBarInstrument::Stock,
            stockCode,
            minuteUnit,
            "stk_min_pole_chart_qry",
            json);
    }

    MinuteBarsPage ParseIndexMinuteBarsResponse(
        const std::string& indexCode,
        int minuteUnit,
        const std::string& json)
    {
        return ParseMinuteBarsResponse(
            MinuteBarInstrument::Index,
            indexCode,
            minuteUnit,
            "inds_min_pole_qry",
            json);
    }
}
