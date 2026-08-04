#include "../core/kiwoom_symbol_catalog.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::exit(1);
    }

    void Check(bool condition, const char* message)
    {
        if (!condition) Fail(message);
    }
}

int main()
{
    using namespace trading;

    std::string error;
    Continuation continuation;
    const RestRequest request = BuildSymbolCatalogRestRequest(
        "0", "token", continuation, error);
    Check(error.empty(), "catalog request build failed");
    Check(request.apiId == "ka10099", "catalog api id mismatch");
    Check(request.path == "/api/dostk/stkinfo", "catalog path mismatch");
    Check(request.body.find("\"mrkt_tp\":\"0\"") != std::string::npos,
          "catalog market body mismatch");

    continuation.hasMore = true;
    continuation.continueYn = "Y";
    continuation.nextKey = "NEXT";
    const RestRequest continued = BuildSymbolCatalogRestRequest(
        "10", "token", continuation, error);
    Check(continued.headers.at("cont-yn") == "Y",
          "catalog continuation header missing");
    Check(continued.headers.at("next-key") == "NEXT",
          "catalog next-key header missing");

    const std::string json =
        "{\"return_code\":0,\"return_msg\":\"정상\","
        "\"list\":["
        "{\"stk_cd\":\"005930\",\"stk_nm\":\"삼성전자\"},"
        "{\"stk_cd\":\"000660\",\"stk_nm\":\"SK하이닉스\"}]}";
    const SymbolCatalogPage page = ParseSymbolCatalogResponse("KOSPI", json);
    Check(page.result.ok, "catalog response must parse");
    Check(page.entries.size() == 2U, "catalog entry count mismatch");
    Check(page.entries[0].market == "KOSPI", "catalog market label mismatch");

    std::vector<SymbolCatalogEntry> entries = page.entries;
    entries.push_back({ "035420", "NAVER", "KOSPI" });
    std::vector<SymbolCatalogEntry> byCode =
        SearchSymbolCatalog(entries, "000", 10U);
    Check(byCode.size() == 1U && byCode.front().code == "000660",
          "code prefix search mismatch");
    std::vector<SymbolCatalogEntry> byName =
        SearchSymbolCatalog(entries, "하이", 10U);
    Check(byName.size() == 1U && byName.front().code == "000660",
          "Korean name substring search mismatch");
    std::vector<SymbolCatalogEntry> exact =
        SearchSymbolCatalog(entries, "삼성전자", 10U);
    Check(exact.size() == 1U && exact.front().code == "005930",
          "exact name search mismatch");

    const SymbolCatalogPage rejected = ParseSymbolCatalogResponse(
        "KOSDAQ",
        "{\"return_code\":-1,\"return_msg\":\"실패\"}");
    Check(!rejected.result.ok && !rejected.result.error.empty(),
          "catalog failure must fail closed");

    std::puts("[PASS] kiwoom_symbol_catalog_tests");
    return 0;
}
