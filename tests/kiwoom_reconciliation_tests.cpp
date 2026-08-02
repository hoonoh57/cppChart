#include "../core/kiwoom_reconciliation.h"

#include <cstdio>
#include <cstdlib>
#include <string>

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

    void CheckContains(
        const std::string& text,
        const std::string& expected,
        const char* message)
    {
        if (text.find(expected) == std::string::npos) Fail(message);
    }

    void TestRequestBuilders()
    {
        trading::Continuation continuation;
        continuation.continueYn = "Y";
        continuation.nextKey = "NEXT-1";

        const trading::RestRequest open =
            trading::BuildOpenOrdersRestRequest(
                "TOKEN",
                "005930",
                trading::ReconciliationSide::Buy,
                continuation);

        Check(open.path == "/api/dostk/acnt",
              "open order endpoint mismatch");
        Check(open.apiId == "ka10075",
              "open order API ID mismatch");
        Check(open.headers.at("authorization") == "Bearer TOKEN",
              "open order authorization mismatch");
        Check(open.headers.at("cont-yn") == "Y",
              "continuation header mismatch");
        Check(open.headers.at("next-key") == "NEXT-1",
              "continuation key mismatch");
        CheckContains(open.body, "\"all_stk_tp\":\"1\"",
                      "single-stock open order scope mismatch");
        CheckContains(open.body, "\"trde_tp\":\"2\"",
                      "buy-side open order filter mismatch");
        CheckContains(open.body, "\"stex_tp\":\"1\"",
                      "mock reconciliation must use KRX");

        const trading::RestRequest executions =
            trading::BuildExecutionsRestRequest(
                "TOKEN",
                {},
                trading::ReconciliationSide::All,
                "0000100");

        Check(executions.apiId == "ka10076",
              "execution API ID mismatch");
        CheckContains(executions.body, "\"qry_tp\":\"0\"",
                      "all-stock execution scope mismatch");
        CheckContains(executions.body, "\"ord_no\":\"0000100\"",
                      "execution cursor order number mismatch");

        const trading::RestRequest balance =
            trading::BuildAccountBalanceRestRequest("TOKEN");

        Check(balance.apiId == "kt00018",
              "balance API ID mismatch");
        CheckContains(balance.body, "\"qry_tp\":\"1\"",
                      "balance aggregate query mismatch");
        CheckContains(balance.body, "\"dmst_stex_tp\":\"KRX\"",
                      "balance exchange mismatch");
    }

    void TestOpenOrdersParser()
    {
        const std::string json =
            "{"
            "\"oso\":[{"
            "\"ord_no\":\"0000069\","
            "\"orig_ord_no\":\"0000000\","
            "\"stk_cd\":\"005930\","
            "\"stk_nm\":\"삼성전자\","
            "\"ord_stt\":\"접수\","
            "\"ord_qty\":\"10\","
            "\"ord_pric\":\"0\","
            "\"oso_qty\":\"4\","
            "\"io_tp_nm\":\"+매수\","
            "\"tm\":\"091501\""
            "}],"
            "\"return_code\":0,"
            "\"return_msg\":\"정상\""
            "}";

        const trading::OpenOrdersResponse parsed =
            trading::ParseOpenOrdersResponse(json);

        Check(parsed.result.ok, "open order response must parse");
        Check(parsed.orders.size() == 1,
              "open order row count mismatch");
        Check(parsed.orders[0].brokerOrderNumber == "0000069",
              "open order number mismatch");
        Check(parsed.orders[0].side == trading::StockOrderSide::Buy,
              "open order side mismatch");
        Check(parsed.orders[0].orderedQuantity == 10,
              "open order quantity mismatch");
        Check(parsed.orders[0].unfilledQuantity == 4,
              "open order unfilled quantity mismatch");
    }

    void TestExecutionsParser()
    {
        const std::string json =
            "{"
            "\"cntr\":[{"
            "\"ord_no\":\"0000069\","
            "\"orig_ord_no\":\"0000000\","
            "\"stk_cd\":\"A005930\","
            "\"stk_nm\":\"삼성전자\","
            "\"io_tp_nm\":\"+매수\","
            "\"ord_pric\":\"0\","
            "\"ord_qty\":\"10\","
            "\"cntr_pric\":\"74100\","
            "\"cntr_qty\":\"6\","
            "\"oso_qty\":\"4\","
            "\"ord_stt\":\"체결\","
            "\"ord_tm\":\"091501\""
            "}],"
            "\"return_code\":\"0\","
            "\"return_msg\":\"정상\""
            "}";

        const trading::ExecutionsResponse parsed =
            trading::ParseExecutionsResponse(json);

        Check(parsed.result.ok, "execution response must parse");
        Check(parsed.executions.size() == 1,
              "execution row count mismatch");
        Check(parsed.executions[0].code == "005930",
              "execution stock code normalization mismatch");
        Check(parsed.executions[0].cumulativeFilledQuantity == 6,
              "execution cumulative fill mismatch");
        Check(parsed.executions[0].fillPriceWon == 74100,
              "execution price mismatch");
    }

    void TestAccountBalanceParser()
    {
        const std::string json =
            "{"
            "\"tot_pur_amt\":\"1,200,000\","
            "\"tot_evlt_amt\":\"1,260,000\","
            "\"tot_evlt_pl\":\"+60,000\","
            "\"prsm_dpst_aset_amt\":\"100,000,000\","
            "\"acnt_evlt_remn_indv_tot\":[{"
            "\"stk_cd\":\"A005930\","
            "\"stk_nm\":\"삼성전자\","
            "\"pur_pric\":\"70000\","
            "\"rmnd_qty\":\"12\","
            "\"trde_able_qty\":\"10\","
            "\"cur_prc\":\"+71000\","
            "\"pur_amt\":\"840000\","
            "\"evlt_amt\":\"852000\""
            "}],"
            "\"return_code\":0,"
            "\"return_msg\":\"정상\""
            "}";

        const trading::AccountBalanceResponse parsed =
            trading::ParseAccountBalanceResponse(json);

        Check(parsed.result.ok, "account balance response must parse");
        Check(parsed.totalPurchaseWon == 1200000,
              "total purchase amount mismatch");
        Check(parsed.totalEvaluationPnlWon == 60000,
              "total evaluation PnL mismatch");
        Check(parsed.positions.size() == 1,
              "balance position count mismatch");
        Check(parsed.positions[0].code == "005930",
              "balance stock code normalization mismatch");
        Check(parsed.positions[0].quantity == 12,
              "balance quantity mismatch");
        Check(parsed.positions[0].costBasisWon == 840000,
              "balance exact cost basis mismatch");
        Check(parsed.positions[0].currentPriceWon == 71000,
              "balance current price mismatch");
    }

    void TestMalformedRowsFailTransactionally()
    {
        const trading::OpenOrdersResponse malformed =
            trading::ParseOpenOrdersResponse(
                "{\"oso\":[{\"ord_no\":\"1\",\"stk_cd\":\"005930\","
                "\"ord_qty\":\"1\",\"oso_qty\":\"1\","
                "\"io_tp_nm\":\"UNKNOWN\"}],\"return_code\":0}");

        Check(!malformed.result.ok,
              "invalid side must fail the full reconciliation page");
        Check(malformed.orders.empty(),
              "failed reconciliation page must not expose partial rows");
    }
}

int main()
{
    TestRequestBuilders();
    TestOpenOrdersParser();
    TestExecutionsParser();
    TestAccountBalanceParser();
    TestMalformedRowsFailTransactionally();

    std::puts("[PASS] kiwoom_reconciliation_tests");
    return 0;
}
