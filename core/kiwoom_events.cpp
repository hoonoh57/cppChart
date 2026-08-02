#include "kiwoom_events.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <sstream>

namespace trading
{
    namespace
    {
        std::string Trim(std::string value)
        {
            const auto notSpace = [](unsigned char ch) {
                return std::isspace(ch) == 0;
            };

            const auto first = std::find_if(value.begin(), value.end(), notSpace);
            if (first == value.end()) return {};

            const auto last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
            return std::string(first, last);
        }

        const std::string* FindValue(
            const RealTimeRecord& record,
            std::initializer_list<const char*> keys)
        {
            for (const char* key : keys) {
                const auto found = record.values.find(key);
                if (found != record.values.end()) return &found->second;
            }
            return nullptr;
        }

        bool TryParseSignedInteger(
            const std::string& source,
            std::int64_t& value)
        {
            std::string text = Trim(source);
            text.erase(
                std::remove(text.begin(), text.end(), ','),
                text.end());

            if (text.empty()) return false;

            try {
                std::size_t consumed = 0;
                const long long parsed = std::stoll(text, &consumed, 10);
                if (consumed != text.size()) return false;
                value = static_cast<std::int64_t>(parsed);
                return true;
            }
            catch (...) {
                return false;
            }
        }

        bool TryParseNonNegativeInt32(
            const std::string* source,
            std::int32_t& value,
            bool absoluteValue = true)
        {
            if (source == nullptr) return false;

            std::int64_t parsed = 0;
            if (!TryParseSignedInteger(*source, parsed)) return false;
            if (absoluteValue && parsed < 0) parsed = -parsed;

            if (
                parsed < 0 ||
                parsed > static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::max)()))
            {
                return false;
            }

            value = static_cast<std::int32_t>(parsed);
            return true;
        }

        bool TryParseNonNegativeInt64(
            const std::string* source,
            std::int64_t& value,
            bool absoluteValue = true)
        {
            if (source == nullptr) return false;

            if (!TryParseSignedInteger(*source, value)) return false;
            if (absoluteValue && value < 0) value = -value;
            return value >= 0;
        }

        std::string NormalizeCode(const std::string& source)
        {
            std::string code = Trim(source);
            if (
                code.size() == 7 &&
                (code.front() == 'A' || code.front() == 'a'))
            {
                code.erase(code.begin());
            }
            return code;
        }

        bool TryParseSide(
            const std::string* source,
            StockOrderSide& side)
        {
            if (source == nullptr) return false;
            std::string text = Trim(*source);

            std::transform(
                text.begin(),
                text.end(),
                text.begin(),
                [](unsigned char ch) {
                    return static_cast<char>(std::toupper(ch));
                });

            if (
                text == "2" ||
                text == "B" ||
                text == "BUY" ||
                text.find("매수") != std::string::npos)
            {
                side = StockOrderSide::Buy;
                return true;
            }

            if (
                text == "1" ||
                text == "S" ||
                text == "SELL" ||
                text.find("매도") != std::string::npos)
            {
                side = StockOrderSide::Sell;
                return true;
            }

            return false;
        }

        bool TryParseTimeOfDay(
            const std::string* source,
            EpochMillis sessionDateStartMs,
            EpochMillis& timestampMs)
        {
            if (source == nullptr) {
                timestampMs = sessionDateStartMs;
                return false;
            }

            std::string digits;
            for (unsigned char ch : *source) {
                if (std::isdigit(ch) != 0) digits.push_back(static_cast<char>(ch));
            }

            if (digits.size() != 6 && digits.size() != 9) {
                timestampMs = sessionDateStartMs;
                return false;
            }

            const int hour = std::stoi(digits.substr(0, 2));
            const int minute = std::stoi(digits.substr(2, 2));
            const int second = std::stoi(digits.substr(4, 2));
            const int millisecond =
                digits.size() == 9
                    ? std::stoi(digits.substr(6, 3))
                    : 0;

            if (
                hour < 0 || hour > 23 ||
                minute < 0 || minute > 59 ||
                second < 0 || second > 59)
            {
                timestampMs = sessionDateStartMs;
                return false;
            }

            timestampMs =
                sessionDateStartMs +
                static_cast<EpochMillis>(hour) * 60 * 60 * 1000 +
                static_cast<EpochMillis>(minute) * 60 * 1000 +
                static_cast<EpochMillis>(second) * 1000 +
                millisecond;
            return true;
        }

        std::string RequiredText(
            const RealTimeRecord& record,
            std::initializer_list<const char*> keys)
        {
            const std::string* value = FindValue(record, keys);
            return value == nullptr ? std::string() : Trim(*value);
        }
    }

    OrderExecutionDecodeResult DecodeKiwoomOrderExecution(
        const RealTimeRecord& record,
        EpochMillis sessionDateStartMs)
    {
        OrderExecutionDecodeResult result;
        if (record.type != "00") return result;

        result.status = EventDecodeStatus::Invalid;
        KiwoomOrderExecution& event = result.event;

        event.brokerOrderNumber = RequiredText(
            record, { "9203", "ord_no", "order_no" });
        event.originalOrderNumber = RequiredText(
            record, { "904", "orig_ord_no", "original_order_no" });
        event.executionId = RequiredText(
            record, { "909", "cntr_no", "execution_id" });
        event.code = NormalizeCode(RequiredText(
            record, { "9001", "stk_cd", "code" }));
        event.name = RequiredText(
            record, { "302", "stk_nm", "name" });
        if (event.name.empty()) event.name = record.name;
        event.orderStatus = RequiredText(
            record, { "913", "ord_stt", "order_status" });

        const std::string* sideValue = FindValue(
            record, { "907", "side", "sel_buy_tp", "order_side" });
        if (!TryParseSide(sideValue, event.side)) {
            sideValue = FindValue(record, { "905", "io_tp_nm" });
            if (!TryParseSide(sideValue, event.side)) {
                result.error = "order side is missing or invalid";
                return result;
            }
        }

        if (event.brokerOrderNumber.empty()) {
            result.error = "broker order number is missing";
            return result;
        }
        if (event.code.empty()) {
            result.error = "stock code is missing";
            return result;
        }

        if (!TryParseNonNegativeInt32(
                FindValue(record, { "900", "ord_qty", "order_qty" }),
                event.orderedQuantity))
        {
            result.error = "ordered quantity is missing or invalid";
            return result;
        }

        if (!TryParseNonNegativeInt32(
                FindValue(record, { "902", "oso_qty", "unfilled_qty" }),
                event.unfilledQuantity))
        {
            event.unfilledQuantity = event.orderedQuantity;
        }

        if (
            event.unfilledQuantity < 0 ||
            event.unfilledQuantity > event.orderedQuantity)
        {
            result.error = "unfilled quantity exceeds ordered quantity";
            return result;
        }

        const std::string* explicitCumulative = FindValue(
            record, { "cum_fill_qty", "cumulative_fill_qty" });

        if (explicitCumulative != nullptr) {
            if (!TryParseNonNegativeInt32(
                    explicitCumulative,
                    event.cumulativeQuantity))
            {
                result.error = "cumulative fill quantity is invalid";
                return result;
            }
        }
        else {
            event.cumulativeQuantity =
                event.orderedQuantity - event.unfilledQuantity;
        }

        if (
            event.cumulativeQuantity < 0 ||
            event.cumulativeQuantity > event.orderedQuantity)
        {
            result.error = "cumulative fill quantity exceeds ordered quantity";
            return result;
        }

        TryParseNonNegativeInt32(
            FindValue(record, { "911", "cntr_qty", "fill_qty" }),
            event.lastFillQuantity);

        TryParseNonNegativeInt32(
            FindValue(record, { "910", "cntr_pric", "fill_price" }),
            event.fillPriceWon);

        TryParseTimeOfDay(
            FindValue(record, { "908", "ord_cntr_tm", "execution_time", "tm" }),
            sessionDateStartMs,
            event.executionTimestampMs);

        if (
            event.cumulativeQuantity == 0 ||
            event.lastFillQuantity == 0 ||
            event.fillPriceWon <= 0)
        {
            result.status = EventDecodeStatus::NoExecution;
            result.error.clear();
            return result;
        }

        if (event.executionId.empty()) {
            std::ostringstream generated;
            generated
                << event.brokerOrderNumber
                << ':'
                << event.cumulativeQuantity;
            event.executionId = generated.str();
        }

        result.status = EventDecodeStatus::Decoded;
        result.error.clear();
        return result;
    }

    BalanceDecodeResult DecodeKiwoomBalanceUpdate(
        const RealTimeRecord& record)
    {
        BalanceDecodeResult result;
        if (record.type != "04") return result;

        result.status = EventDecodeStatus::Invalid;
        KiwoomBalanceUpdate& event = result.event;

        event.code = NormalizeCode(RequiredText(
            record, { "9001", "stk_cd", "code" }));
        event.name = RequiredText(
            record, { "302", "stk_nm", "name" });
        if (event.name.empty()) event.name = record.name;

        if (event.code.empty()) {
            result.error = "stock code is missing";
            return result;
        }

        if (!TryParseNonNegativeInt32(
                FindValue(record, { "930", "rmnd_qty", "quantity" }),
                event.quantity))
        {
            result.error = "held quantity is missing or invalid";
            return result;
        }

        if (!TryParseNonNegativeInt32(
                FindValue(record, { "933", "trde_able_qty", "available_qty" }),
                event.availableQuantity))
        {
            event.availableQuantity = event.quantity;
        }

        if (event.availableQuantity > event.quantity) {
            result.error = "available quantity exceeds held quantity";
            return result;
        }

        TryParseNonNegativeInt32(
            FindValue(record, { "931", "pur_pric", "avg_prc", "average_price" }),
            event.averagePriceWon);
        TryParseNonNegativeInt32(
            FindValue(record, { "10", "cur_prc", "current_price" }),
            event.currentPriceWon);
        TryParseNonNegativeInt64(
            FindValue(record, { "932", "pur_amt", "cost_basis" }),
            event.costBasisWon);

        if (event.quantity == 0) {
            event.averagePriceWon = 0;
            event.currentPriceWon = 0;
            event.costBasisWon = 0;
            result.status = EventDecodeStatus::Decoded;
            result.error.clear();
            return result;
        }

        if (event.costBasisWon == 0) {
            if (event.averagePriceWon <= 0) {
                result.error = "average price or cost basis is required";
                return result;
            }

            MoneyWon calculated = 0;
            if (!TryCalculateNotional(
                    event.averagePriceWon,
                    event.quantity,
                    calculated))
            {
                result.error = "cost basis overflow";
                return result;
            }
            event.costBasisWon = calculated;
        }

        if (event.averagePriceWon <= 0) {
            event.averagePriceWon = static_cast<PriceWon>(
                event.costBasisWon /
                static_cast<MoneyWon>(event.quantity));
        }

        if (event.currentPriceWon <= 0) {
            event.currentPriceWon = event.averagePriceWon;
        }

        result.status = EventDecodeStatus::Decoded;
        result.error.clear();
        return result;
    }

    PositionSnapshot ToPositionSnapshot(
        const KiwoomBalanceUpdate& update)
    {
        PositionSnapshot position;
        position.code = update.code;
        position.name = update.name;
        position.quantity = update.quantity;
        position.costBasisWon = update.costBasisWon;
        position.currentPriceWon = update.currentPriceWon;
        return position;
    }
}
