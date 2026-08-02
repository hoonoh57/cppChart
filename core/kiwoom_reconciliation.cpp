#include "kiwoom_reconciliation.h"

#include "json_lite.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace trading
{
    namespace
    {
        constexpr const char* AccountPath = "/api/dostk/acnt";

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

        bool ParseRoot(
            const std::string& json,
            json_lite::Value& root,
            std::string& error)
        {
            const json_lite::ParseResult parsed = json_lite::Parse(json);
            if (!parsed.ok) {
                std::ostringstream message;
                message
                    << "JSON parse error at byte "
                    << parsed.errorOffset
                    << ": "
                    << parsed.error;
                error = message.str();
                return false;
            }

            if (!parsed.value.IsObject()) {
                error = "JSON root must be an object";
                return false;
            }

            root = parsed.value;
            error.clear();
            return true;
        }

        std::string ScalarText(const json_lite::Value* value)
        {
            if (value == nullptr) return {};
            if (value->IsString()) return Trim(value->AsString());
            if (value->IsNumber()) {
                std::ostringstream out;
                out << std::setprecision(15) << value->AsNumber();
                return out.str();
            }
            return {};
        }

        bool TryParseIntegerText(
            const std::string& source,
            std::int64_t& value,
            bool absoluteValue = true)
        {
            std::string text = Trim(source);
            text.erase(std::remove(text.begin(), text.end(), ','), text.end());
            if (text.empty()) return false;

            try {
                std::size_t consumed = 0;
                const long double parsed = std::stold(text, &consumed);
                if (consumed != text.size() || !std::isfinite(parsed)) return false;

                long double normalized = absoluteValue ? std::fabs(parsed) : parsed;
                if (
                    normalized < static_cast<long double>((std::numeric_limits<std::int64_t>::min)()) ||
                    normalized > static_cast<long double>((std::numeric_limits<std::int64_t>::max)()))
                {
                    return false;
                }

                value = static_cast<std::int64_t>(std::llround(normalized));
                return true;
            }
            catch (...) {
                return false;
            }
        }

        bool TryReadInt32(
            const json_lite::Value& object,
            const char* key,
            std::int32_t& value,
            bool absoluteValue = true)
        {
            std::int64_t parsed = 0;
            if (!TryParseIntegerText(ScalarText(object.Find(key)), parsed, absoluteValue)) {
                return false;
            }
            if (
                parsed < static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::min)()) ||
                parsed > static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::max)()))
            {
                return false;
            }
            value = static_cast<std::int32_t>(parsed);
            return true;
        }

        bool TryReadInt64(
            const json_lite::Value& object,
            const char* key,
            std::int64_t& value,
            bool absoluteValue = true)
        {
            return TryParseIntegerText(
                ScalarText(object.Find(key)),
                value,
                absoluteValue);
        }

        ProtocolResult ParseResult(const json_lite::Value& root)
        {
            ProtocolResult result;
            result.returnCode = 0;

            const json_lite::Value* code = root.Find("return_code");
            if (code != nullptr) {
                std::int64_t parsed = 0;
                if (!TryParseIntegerText(ScalarText(code), parsed, false)) {
                    result.error = "return_code is invalid";
                    return result;
                }
                if (
                    parsed < static_cast<std::int64_t>((std::numeric_limits<int>::min)()) ||
                    parsed > static_cast<std::int64_t>((std::numeric_limits<int>::max)()))
                {
                    result.error = "return_code is out of range";
                    return result;
                }
                result.returnCode = static_cast<int>(parsed);
            }

            result.returnMessage = ScalarText(root.Find("return_msg"));
            result.ok = result.returnCode == 0;
            if (!result.ok && result.returnMessage.empty()) {
                result.returnMessage = "Kiwoom API returned an error";
            }
            return result;
        }

        bool TryParseSide(
            const std::string& source,
            StockOrderSide& side)
        {
            std::string text = Trim(source);
            std::transform(
                text.begin(),
                text.end(),
                text.begin(),
                [](unsigned char ch) {
                    return static_cast<char>(std::toupper(ch));
                });

            if (
                text == "2" || text == "B" || text == "BUY" ||
                text.find("매수") != std::string::npos)
            {
                side = StockOrderSide::Buy;
                return true;
            }
            if (
                text == "1" || text == "S" || text == "SELL" ||
                text.find("매도") != std::string::npos)
            {
                side = StockOrderSide::Sell;
                return true;
            }
            return false;
        }

        const char* SideCode(ReconciliationSide side)
        {
            switch (side) {
            case ReconciliationSide::Sell: return "1";
            case ReconciliationSide::Buy: return "2";
            default: return "0";
            }
        }

        RestRequest BuildAccountRequest(
            const std::string& apiId,
            const std::string& bearerToken,
            const std::string& body,
            const Continuation& continuation)
        {
            RestRequest request;
            request.method = "POST";
            request.path = AccountPath;
            request.apiId = apiId;
            request.body = body;
            request.headers["authorization"] = "Bearer " + bearerToken;
            request.headers["api-id"] = apiId;
            request.headers["content-type"] = "application/json;charset=UTF-8";

            if (!continuation.continueYn.empty()) {
                request.headers["cont-yn"] = continuation.continueYn;
            }
            if (!continuation.nextKey.empty()) {
                request.headers["next-key"] = continuation.nextKey;
            }
            return request;
        }

        const json_lite::Value::Array* FindArray(
            const json_lite::Value& root,
            const char* key,
            ProtocolResult& result)
        {
            const json_lite::Value* value = root.Find(key);
            if (value == nullptr) return nullptr;
            if (!value->IsArray()) {
                result.ok = false;
                result.error = std::string(key) + " must be an array";
                return nullptr;
            }
            return &value->AsArray();
        }
    }

    RestRequest BuildOpenOrdersRestRequest(
        const std::string& bearerToken,
        const std::string& stockCode,
        ReconciliationSide side,
        const Continuation& continuation)
    {
        std::ostringstream body;
        body
            << '{'
            << "\"all_stk_tp\":\"" << (stockCode.empty() ? '0' : '1') << "\","
            << "\"trde_tp\":\"" << SideCode(side) << "\","
            << "\"stk_cd\":" << json_lite::EscapeString(stockCode) << ','
            << "\"stex_tp\":\"1\""
            << '}';

        return BuildAccountRequest(
            "ka10075",
            bearerToken,
            body.str(),
            continuation);
    }

    RestRequest BuildExecutionsRestRequest(
        const std::string& bearerToken,
        const std::string& stockCode,
        ReconciliationSide side,
        const std::string& beforeOrderNumber,
        const Continuation& continuation)
    {
        std::ostringstream body;
        body
            << '{'
            << "\"stk_cd\":" << json_lite::EscapeString(stockCode) << ','
            << "\"qry_tp\":\"" << (stockCode.empty() ? '0' : '1') << "\","
            << "\"sell_tp\":\"" << SideCode(side) << "\","
            << "\"ord_no\":" << json_lite::EscapeString(beforeOrderNumber) << ','
            << "\"stex_tp\":\"1\""
            << '}';

        return BuildAccountRequest(
            "ka10076",
            bearerToken,
            body.str(),
            continuation);
    }

    RestRequest BuildAccountBalanceRestRequest(
        const std::string& bearerToken,
        const Continuation& continuation)
    {
        return BuildAccountRequest(
            "kt00018",
            bearerToken,
            "{\"qry_tp\":\"1\",\"dmst_stex_tp\":\"KRX\"}",
            continuation);
    }

    OpenOrdersResponse ParseOpenOrdersResponse(
        const std::string& json)
    {
        OpenOrdersResponse response;
        json_lite::Value root;
        if (!ParseRoot(json, root, response.result.error)) return response;

        response.result = ParseResult(root);
        if (!response.result.ok) return response;

        const json_lite::Value::Array* rows =
            FindArray(root, "oso", response.result);
        if (!response.result.ok || rows == nullptr) return response;

        for (const json_lite::Value& row : *rows) {
            if (!row.IsObject()) {
                response.result.ok = false;
                response.result.error = "oso row must be an object";
                response.orders.clear();
                return response;
            }

            OpenOrderSnapshot item;
            item.brokerOrderNumber = ScalarText(row.Find("ord_no"));
            item.originalOrderNumber = ScalarText(row.Find("orig_ord_no"));
            item.code = NormalizeCode(ScalarText(row.Find("stk_cd")));
            item.name = ScalarText(row.Find("stk_nm"));
            item.orderStatus = ScalarText(row.Find("ord_stt"));
            item.orderTime = ScalarText(row.Find("tm"));

            if (!TryParseSide(ScalarText(row.Find("io_tp_nm")), item.side)) {
                response.result.ok = false;
                response.result.error = "open order side is invalid";
                response.orders.clear();
                return response;
            }

            if (
                item.brokerOrderNumber.empty() ||
                item.code.empty() ||
                !TryReadInt32(row, "ord_qty", item.orderedQuantity) ||
                !TryReadInt32(row, "oso_qty", item.unfilledQuantity))
            {
                response.result.ok = false;
                response.result.error = "open order row is missing required fields";
                response.orders.clear();
                return response;
            }

            TryReadInt32(row, "ord_pric", item.orderPriceWon);
            response.orders.push_back(std::move(item));
        }

        return response;
    }

    ExecutionsResponse ParseExecutionsResponse(
        const std::string& json)
    {
        ExecutionsResponse response;
        json_lite::Value root;
        if (!ParseRoot(json, root, response.result.error)) return response;

        response.result = ParseResult(root);
        if (!response.result.ok) return response;

        const json_lite::Value::Array* rows =
            FindArray(root, "cntr", response.result);
        if (!response.result.ok || rows == nullptr) return response;

        for (const json_lite::Value& row : *rows) {
            if (!row.IsObject()) {
                response.result.ok = false;
                response.result.error = "cntr row must be an object";
                response.executions.clear();
                return response;
            }

            ExecutionSnapshot item;
            item.brokerOrderNumber = ScalarText(row.Find("ord_no"));
            item.originalOrderNumber = ScalarText(row.Find("orig_ord_no"));
            item.code = NormalizeCode(ScalarText(row.Find("stk_cd")));
            item.name = ScalarText(row.Find("stk_nm"));
            item.orderStatus = ScalarText(row.Find("ord_stt"));
            item.orderTime = ScalarText(row.Find("ord_tm"));

            if (!TryParseSide(ScalarText(row.Find("io_tp_nm")), item.side)) {
                response.result.ok = false;
                response.result.error = "execution side is invalid";
                response.executions.clear();
                return response;
            }

            if (
                item.brokerOrderNumber.empty() ||
                item.code.empty() ||
                !TryReadInt32(row, "ord_qty", item.orderedQuantity))
            {
                response.result.ok = false;
                response.result.error = "execution row is missing required fields";
                response.executions.clear();
                return response;
            }

            if (!TryReadInt32(row, "oso_qty", item.unfilledQuantity)) {
                item.unfilledQuantity = 0;
            }

            if (item.unfilledQuantity > item.orderedQuantity) {
                response.result.ok = false;
                response.result.error = "execution unfilled quantity exceeds order quantity";
                response.executions.clear();
                return response;
            }

            item.cumulativeFilledQuantity =
                item.orderedQuantity - item.unfilledQuantity;

            std::int32_t reportedFill = 0;
            if (
                TryReadInt32(row, "cntr_qty", reportedFill) &&
                item.cumulativeFilledQuantity == 0)
            {
                item.cumulativeFilledQuantity = reportedFill;
            }

            TryReadInt32(row, "ord_pric", item.orderPriceWon);
            TryReadInt32(row, "cntr_pric", item.fillPriceWon);
            response.executions.push_back(std::move(item));
        }

        return response;
    }

    AccountBalanceResponse ParseAccountBalanceResponse(
        const std::string& json)
    {
        AccountBalanceResponse response;
        json_lite::Value root;
        if (!ParseRoot(json, root, response.result.error)) return response;

        response.result = ParseResult(root);
        if (!response.result.ok) return response;

        TryReadInt64(root, "tot_pur_amt", response.totalPurchaseWon);
        TryReadInt64(root, "tot_evlt_amt", response.totalEvaluationWon);
        TryReadInt64(
            root,
            "tot_evlt_pl",
            response.totalEvaluationPnlWon,
            false);
        TryReadInt64(root, "prsm_dpst_aset_amt", response.estimatedAssetWon);

        const json_lite::Value::Array* rows =
            FindArray(root, "acnt_evlt_remn_indv_tot", response.result);
        if (!response.result.ok || rows == nullptr) return response;

        for (const json_lite::Value& row : *rows) {
            if (!row.IsObject()) {
                response.result.ok = false;
                response.result.error = "balance row must be an object";
                response.positions.clear();
                return response;
            }

            PositionSnapshot position;
            position.code = NormalizeCode(ScalarText(row.Find("stk_cd")));
            position.name = ScalarText(row.Find("stk_nm"));

            if (
                position.code.empty() ||
                !TryReadInt32(row, "rmnd_qty", position.quantity))
            {
                response.result.ok = false;
                response.result.error = "balance row is missing required fields";
                response.positions.clear();
                return response;
            }

            if (position.quantity == 0) continue;

            TryReadInt32(row, "cur_prc", position.currentPriceWon);
            TryReadInt64(row, "pur_amt", position.costBasisWon);

            if (position.costBasisWon == 0) {
                PriceWon averagePrice = 0;
                if (!TryReadInt32(row, "pur_pric", averagePrice)) {
                    response.result.ok = false;
                    response.result.error = "balance purchase amount and price are missing";
                    response.positions.clear();
                    return response;
                }

                if (!TryCalculateNotional(
                        averagePrice,
                        position.quantity,
                        position.costBasisWon))
                {
                    response.result.ok = false;
                    response.result.error = "balance cost basis overflow";
                    response.positions.clear();
                    return response;
                }
            }

            if (position.currentPriceWon <= 0) {
                position.currentPriceWon = static_cast<PriceWon>(
                    position.costBasisWon /
                    static_cast<MoneyWon>(position.quantity));
            }

            response.positions.push_back(std::move(position));
        }

        return response;
    }
}
