#include "kiwoom_symbol_catalog.h"

#include "json_lite.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace trading
{
    namespace
    {
        std::string LowerAscii(std::string value)
        {
            std::transform(
                value.begin(), value.end(), value.begin(),
                [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
            return value;
        }

        const json_lite::Value* FindFirst(
            const json_lite::Value& object,
            const char* primary,
            const char* legacy) noexcept
        {
            const json_lite::Value* value = object.Find(primary);
            if (value == nullptr && legacy != nullptr) {
                value = object.Find(legacy);
            }
            return value;
        }

        const json_lite::Value::Array* FindEntryArray(
            const json_lite::Value& root) noexcept
        {
            if (!root.IsObject()) return nullptr;

            const json_lite::Value* list = root.Find("list");
            if (list != nullptr && list->IsArray()) {
                return &list->AsArray();
            }

            for (const auto& pair : root.AsObject()) {
                if (!pair.second.IsArray()) continue;
                for (const json_lite::Value& item : pair.second.AsArray()) {
                    if (!item.IsObject()) continue;
                    const bool currentSchema =
                        item.Find("code") != nullptr &&
                        item.Find("name") != nullptr;
                    const bool legacySchema =
                        item.Find("stk_cd") != nullptr &&
                        item.Find("stk_nm") != nullptr;
                    if (currentSchema || legacySchema) {
                        return &pair.second.AsArray();
                    }
                }
            }
            return nullptr;
        }
    }

    RestRequest BuildSymbolCatalogRestRequest(
        const std::string& marketType,
        const std::string& bearerToken,
        const Continuation& continuation,
        std::string& error)
    {
        RestRequest request;
        if (marketType.empty()) {
            error = "symbol catalog market type is required";
            return request;
        }
        if (bearerToken.empty()) {
            error = "symbol catalog bearer token is required";
            return request;
        }

        request.method = "POST";
        request.path = "/api/dostk/stkinfo";
        request.apiId = "ka10099";
        request.headers.emplace("authorization", "Bearer " + bearerToken);
        request.headers.emplace("api-id", request.apiId);
        request.headers.emplace("content-type", "application/json;charset=UTF-8");
        request.headers.emplace("cont-yn", continuation.continueYn == "Y" ? "Y" : "N");
        request.headers.emplace("next-key", continuation.nextKey);
        request.body =
            "{\"mrkt_tp\":" + json_lite::EscapeString(marketType) + "}";
        error.clear();
        return request;
    }

    SymbolCatalogPage ParseSymbolCatalogResponse(
        const std::string& market,
        const std::string& json)
    {
        SymbolCatalogPage page;
        const json_lite::ParseResult parsed = json_lite::Parse(json);
        if (!parsed.ok || !parsed.value.IsObject()) {
            page.result.error = parsed.error.empty()
                ? "symbol catalog response is not an object"
                : parsed.error;
            return page;
        }

        const json_lite::Value* returnCode = parsed.value.Find("return_code");
        if (returnCode == nullptr) returnCode = parsed.value.Find("returnCode");
        page.result.returnCode = returnCode != nullptr
            ? returnCode->AsInt(-1)
            : -1;
        const json_lite::Value* returnMessage = parsed.value.Find("return_msg");
        if (returnMessage == nullptr) returnMessage = parsed.value.Find("returnMsg");
        page.result.returnMessage = returnMessage != nullptr
            ? returnMessage->StringOr()
            : std::string{};
        if (page.result.returnCode != 0) {
            page.result.error = page.result.returnMessage.empty()
                ? "symbol catalog request failed"
                : page.result.returnMessage;
            return page;
        }

        const json_lite::Value::Array* entries = FindEntryArray(parsed.value);
        if (entries == nullptr) {
            page.result.error = "symbol catalog list is missing";
            return page;
        }

        std::set<std::string> seen;
        for (const json_lite::Value& item : *entries) {
            if (!item.IsObject()) continue;
            const json_lite::Value* codeValue =
                FindFirst(item, "code", "stk_cd");
            const json_lite::Value* nameValue =
                FindFirst(item, "name", "stk_nm");
            if (codeValue == nullptr || nameValue == nullptr) continue;

            SymbolCatalogEntry entry;
            entry.code = codeValue->StringOr();
            entry.name = nameValue->StringOr();
            entry.market = market;
            if (entry.code.empty() || entry.name.empty()) continue;
            if (!seen.insert(entry.code).second) continue;
            page.entries.push_back(std::move(entry));
        }
        page.result.ok = true;
        return page;
    }

    std::vector<SymbolCatalogEntry> SearchSymbolCatalog(
        const std::vector<SymbolCatalogEntry>& entries,
        const std::string& query,
        std::size_t limit)
    {
        std::vector<SymbolCatalogEntry> result;
        if (query.empty() || limit == 0U) return result;
        const std::string normalized = LowerAscii(query);

        struct Match final
        {
            int rank = 0;
            const SymbolCatalogEntry* entry = nullptr;
        };
        std::vector<Match> matches;
        for (const SymbolCatalogEntry& entry : entries) {
            const std::string code = LowerAscii(entry.code);
            const std::string name = LowerAscii(entry.name);
            int rank = 100;
            if (code == normalized || name == normalized) rank = 0;
            else if (code.rfind(normalized, 0U) == 0U) rank = 1;
            else if (name.rfind(normalized, 0U) == 0U) rank = 2;
            else if (code.find(normalized) != std::string::npos) rank = 3;
            else if (name.find(normalized) != std::string::npos) rank = 4;
            if (rank < 100) matches.push_back({ rank, &entry });
        }
        std::stable_sort(
            matches.begin(), matches.end(),
            [](const Match& left, const Match& right) {
                if (left.rank != right.rank) return left.rank < right.rank;
                return left.entry->code < right.entry->code;
            });
        for (const Match& match : matches) {
            if (result.size() >= limit) break;
            result.push_back(*match.entry);
        }
        return result;
    }
}
