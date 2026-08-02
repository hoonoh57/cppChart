#include "kiwoom_protocol.h"

#include "json_lite.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace trading
{
    namespace
    {
        bool TryReadInteger(
            const json_lite::Value* value,
            int& out) noexcept
        {
            if (value == nullptr) return false;

            if (value->IsNumber()) {
                const double numeric = value->AsNumber();
                if (
                    !std::isfinite(numeric) ||
                    std::floor(numeric) != numeric ||
                    numeric < static_cast<double>((std::numeric_limits<int>::min)()) ||
                    numeric > static_cast<double>((std::numeric_limits<int>::max)()))
                {
                    return false;
                }

                out = static_cast<int>(numeric);
                return true;
            }

            if (value->IsString()) {
                try {
                    std::size_t consumed = 0;
                    const int parsed = std::stoi(value->AsString(), &consumed, 10);
                    if (consumed != value->AsString().size()) return false;
                    out = parsed;
                    return true;
                }
                catch (...) {
                    return false;
                }
            }

            return false;
        }

        ProtocolResult ParseProtocolResult(
            const json_lite::Value& root)
        {
            ProtocolResult result;

            int returnCode = 0;
            const json_lite::Value* code = root.Find("return_code");

            if (code != nullptr && !TryReadInteger(code, returnCode)) {
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
                result.returnMessage = "Kiwoom API returned an error";
            }

            return result;
        }

        std::string ScalarToString(const json_lite::Value& value)
        {
            if (value.IsString()) return value.AsString();
            if (value.IsBoolean()) return value.AsBoolean() ? "true" : "false";
            if (value.IsNumber()) {
                std::ostringstream out;
                out << std::setprecision(15) << value.AsNumber();
                return out.str();
            }
            if (value.IsNull()) return {};
            return {};
        }

        bool ParseRootObject(
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
    }

    std::string BuildTokenRequestBody(
        const std::string& appKey,
        const std::string& secretKey)
    {
        std::ostringstream out;
        out
            << '{'
            << "\"grant_type\":\"client_credentials\","
            << "\"appkey\":" << json_lite::EscapeString(appKey) << ','
            << "\"secretkey\":" << json_lite::EscapeString(secretKey)
            << '}';
        return out.str();
    }

    TokenResponse ParseTokenResponse(
        const std::string& json)
    {
        TokenResponse response;
        json_lite::Value root;

        if (!ParseRootObject(json, root, response.result.error)) {
            return response;
        }

        response.result = ParseProtocolResult(root);
        if (!response.result.ok) return response;

        const json_lite::Value* token = root.Find("token");
        const json_lite::Value* tokenType = root.Find("token_type");
        const json_lite::Value* expiresAt = root.Find("expires_dt");

        if (token == nullptr || !token->IsString() || token->AsString().empty()) {
            response.result.ok = false;
            response.result.error = "token is missing";
            return response;
        }

        response.token = token->AsString();
        response.tokenType =
            tokenType != nullptr && tokenType->IsString()
                ? tokenType->AsString()
                : "bearer";
        response.expiresAt =
            expiresAt != nullptr && expiresAt->IsString()
                ? expiresAt->AsString()
                : std::string();
        return response;
    }

    std::string BuildWebSocketLoginMessage(
        const std::string& accessToken)
    {
        std::ostringstream out;
        out
            << '{'
            << "\"trnm\":\"LOGIN\","
            << "\"token\":" << json_lite::EscapeString(accessToken)
            << '}';
        return out.str();
    }

    std::string BuildWebSocketRegistrationMessage(
        const std::string& groupNumber,
        bool keepExisting,
        const std::vector<std::string>& items,
        const std::vector<std::string>& realTimeTypes)
    {
        std::ostringstream out;
        out
            << '{'
            << "\"trnm\":\"REG\","
            << "\"grp_no\":" << json_lite::EscapeString(groupNumber) << ','
            << "\"refresh\":\"" << (keepExisting ? '1' : '0') << "\","
            << "\"data\":[{\"item\":[";

        for (std::size_t index = 0; index < items.size(); ++index) {
            if (index > 0) out << ',';
            out << json_lite::EscapeString(items[index]);
        }

        out << "],\"type\":[";

        for (std::size_t index = 0; index < realTimeTypes.size(); ++index) {
            if (index > 0) out << ',';
            out << json_lite::EscapeString(realTimeTypes[index]);
        }

        out << "]}]}";
        return out.str();
    }

    bool IsWebSocketPingMessage(
        const std::string& json)
    {
        json_lite::Value root;
        std::string error;
        if (!ParseRootObject(json, root, error)) return false;

        const json_lite::Value* transactionName = root.Find("trnm");
        return
            transactionName != nullptr &&
            transactionName->IsString() &&
            transactionName->AsString() == "PING";
    }

    RestRequest BuildStockOrderRestRequest(
        const StockOrderRequest& request,
        const std::string& bearerToken,
        std::string& error)
    {
        RestRequest result;

        if (request.code.empty()) {
            error = "stock code is required";
            return result;
        }
        if (!IsValidQuantity(request.quantity)) {
            error = "order quantity must be positive";
            return result;
        }
        if (bearerToken.empty()) {
            error = "bearer token is required";
            return result;
        }
        if (
            request.type == StockOrderType::Limit &&
            !IsValidPrice(request.limitPriceWon))
        {
            error = "limit order price must be positive";
            return result;
        }

        result.method = "POST";
        result.path = KiwoomEndpoints::StockOrderPath;
        result.apiId =
            request.side == StockOrderSide::Buy
                ? "kt10000"
                : "kt10001";
        result.headers["authorization"] = "Bearer " + bearerToken;
        result.headers["api-id"] = result.apiId;
        result.headers["content-type"] =
            "application/json;charset=UTF-8";

        std::ostringstream body;
        body
            << '{'
            << "\"dmst_stex_tp\":\"KRX\","
            << "\"stk_cd\":" << json_lite::EscapeString(request.code) << ','
            << "\"ord_qty\":"
            << json_lite::EscapeString(std::to_string(request.quantity)) << ','
            << "\"ord_uv\":";

        if (request.type == StockOrderType::Limit) {
            body << json_lite::EscapeString(
                std::to_string(request.limitPriceWon));
        }
        else {
            body << "\"\"";
        }

        body
            << ','
            << "\"trde_tp\":\""
            << (request.type == StockOrderType::Market ? '3' : '0')
            << "\","
            << "\"cond_uv\":\"\""
            << '}';

        result.body = body.str();
        error.clear();
        return result;
    }

    OrderResponse ParseOrderResponse(
        const std::string& json)
    {
        OrderResponse response;
        json_lite::Value root;

        if (!ParseRootObject(json, root, response.result.error)) {
            return response;
        }

        response.result = ParseProtocolResult(root);
        if (!response.result.ok) return response;

        const json_lite::Value* orderNumber = root.Find("ord_no");
        if (
            orderNumber == nullptr ||
            !orderNumber->IsString() ||
            orderNumber->AsString().empty())
        {
            response.result.ok = false;
            response.result.error = "ord_no is missing";
            return response;
        }

        response.orderNumber = orderNumber->AsString();

        const json_lite::Value* exchange = root.Find("dmst_stex_tp");
        if (exchange != nullptr && exchange->IsString()) {
            response.exchange = exchange->AsString();
        }

        return response;
    }

    RealTimeEnvelope ParseRealTimeEnvelope(
        const std::string& json)
    {
        RealTimeEnvelope envelope;
        json_lite::Value root;

        if (!ParseRootObject(json, root, envelope.result.error)) {
            return envelope;
        }

        envelope.result = ParseProtocolResult(root);

        const json_lite::Value* transactionName = root.Find("trnm");
        if (transactionName != nullptr && transactionName->IsString()) {
            envelope.transactionName = transactionName->AsString();
        }

        if (!envelope.result.ok) return envelope;

        const json_lite::Value* data = root.Find("data");
        if (data == nullptr) return envelope;
        if (!data->IsArray()) {
            envelope.result.ok = false;
            envelope.result.error = "real-time data must be an array";
            return envelope;
        }

        for (const json_lite::Value& entry : data->AsArray()) {
            if (!entry.IsObject()) {
                envelope.result.ok = false;
                envelope.result.error = "real-time record must be an object";
                envelope.records.clear();
                return envelope;
            }

            RealTimeRecord record;

            const json_lite::Value* type = entry.Find("type");
            const json_lite::Value* item = entry.Find("item");
            const json_lite::Value* name = entry.Find("name");
            const json_lite::Value* values = entry.Find("values");

            if (type != nullptr && type->IsString()) record.type = type->AsString();
            if (item != nullptr && item->IsString()) record.item = item->AsString();
            if (name != nullptr && name->IsString()) record.name = name->AsString();

            if (values != nullptr) {
                if (!values->IsObject()) {
                    envelope.result.ok = false;
                    envelope.result.error = "real-time values must be an object";
                    envelope.records.clear();
                    return envelope;
                }

                for (const auto& value : values->AsObject()) {
                    record.values[value.first] = ScalarToString(value.second);
                }
            }

            envelope.records.push_back(std::move(record));
        }

        return envelope;
    }
}
