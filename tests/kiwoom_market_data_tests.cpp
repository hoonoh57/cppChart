#include "../core/json_lite.h"
#include "../core/kiwoom_market_data.h"

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

    void TestStockRequest()
    {
        std::string error;
        const trading::RestRequest request =
            trading::BuildStockMinuteBarsRestRequest(
                "005930",
                3,
                "token",
                true,
                {},
                error);

        Check(error.empty(), "stock minute request must build");
        Check(request.path == "/api/dostk/chart", "chart path mismatch");
        Check(request.apiId == "ka10080", "stock minute api-id mismatch");
        Check(request.headers.at("authorization") == "Bearer token",
              "authorization header mismatch");
        Check(request.body.find("\"stk_cd\":\"005930\"") != std::string::npos,
              "stock code body mismatch");
        Check(request.body.find("\"tic_scope\":\"3\"") != std::string::npos,
              "minute unit body mismatch");
        Check(request.body.find("\"upd_stkpc_tp\":\"1\"") != std::string::npos,
              "adjusted price body mismatch");

        trading::Continuation continuation;
        continuation.continueYn = "Y";
        continuation.nextKey = "next-1";
        const trading::RestRequest continued =
            trading::BuildStockMinuteBarsRestRequest(
                "005930", 1, "token", true, continuation, error);
        Check(continued.headers.at("cont-yn") == "Y",
              "continuation header mismatch");
        Check(continued.headers.at("next-key") == "next-1",
              "next-key header mismatch");
    }

    void TestStrictStockParsing()
    {
        const std::string json =
            "{"
            "\"return_code\":0,"
            "\"return_msg\":\"정상\","
            "\"stk_min_pole_chart_qry\":["
            "{\"cur_prc\":\"-70100\",\"trde_qty\":\"1,200\","
            "\"cntr_tm\":\"20260803090300\",\"open_pric\":\"70000\","
            "\"high_pric\":\"70200\",\"low_pric\":\"69900\"},"
            "{\"cur_prc\":\"+70000\",\"trde_qty\":\"900\","
            "\"cntr_tm\":\"20260803090000\",\"open_pric\":\"69800\","
            "\"high_pric\":\"70100\",\"low_pric\":\"69700\"}"
            "]}"
            ;

        const trading::MinuteBarsPage page =
            trading::ParseStockMinuteBarsResponse("005930", 3, json);
        Check(page.result.ok, "valid stock minute response must parse");
        Check(page.bars.size() == 2, "stock bar count mismatch");
        Check(page.bars[0].close == 70000, "signed close must normalize");
        Check(page.bars[1].volume == 1200, "comma volume must parse");
        Check(page.bars[1].closeTimestampMs - page.bars[0].closeTimestampMs == 180000,
              "KST timestamp ordering mismatch");
    }

    void TestStockTradeAggregation()
    {
        trading::RealTimeRecord record;
        record.type = "0B";
        record.item = "A000660";
        record.values["20"] = "123701";
        record.values["10"] = "+1584000";
        record.values["15"] = "-3";
        record.values["13"] = "552";

        const trading::StockTradeDecodeResult decoded =
            trading::DecodeStockTradeRecord(record);
        Check(decoded.result.ok, "valid 0B stock trade must decode");
        Check(decoded.tick.code == "000660", "0B code normalization mismatch");
        Check(decoded.tick.priceWon == 1584000, "0B price mismatch");
        Check(decoded.tick.tradeVolume == 3, "0B volume mismatch");
        Check(decoded.tick.tradeTimeHhmmss == 123701, "0B time mismatch");

        constexpr trading::EpochMillis SessionStart = 1785682800000LL;
        std::vector<trading::Bar> bars;
        trading::Bar current;
        current.open = 1582000;
        current.high = 1583000;
        current.low = 1581000;
        current.close = 1583000;
        current.volume = 549;
        current.closeTimestampMs = SessionStart + 12LL * 3600000LL + 37LL * 60000LL;
        bars.push_back(current);

        std::string error;
        Check(
            trading::MergeStockTradeIntoMinuteBars(
                bars, 1, SessionStart, decoded.tick, error),
            "0B trade must merge into current minute bar");
        Check(bars.size() == 1, "same-minute 0B trade must not append a bar");
        Check(bars.back().close == 1584000, "0B close update mismatch");
        Check(bars.back().high == 1584000, "0B high update mismatch");
        Check(bars.back().volume == 552, "0B volume accumulation mismatch");
        Check(bars.back().tickCount == 1, "0B tick count mismatch");

        trading::StockTradeTick next = decoded.tick;
        next.tradeTimeHhmmss = 123800;
        next.priceWon = 1585000;
        next.tradeVolume = 4;
        Check(
            trading::MergeStockTradeIntoMinuteBars(
                bars, 1, SessionStart, next, error),
            "next-minute 0B trade must append a bar");
        Check(bars.size() == 2, "next-minute 0B trade bar count mismatch");
        Check(bars.back().open == 1585000, "new 0B bar open mismatch");
        Check(bars.back().volume == 4, "new 0B bar volume mismatch");
    }

    void TestIndexAndFailures()
    {
        std::string error;
        const trading::RestRequest request =
            trading::BuildIndexMinuteBarsRestRequest(
                "001", 1, "token", {}, error);
        Check(error.empty(), "index request must build");
        Check(request.apiId == "ka20005", "index minute api-id mismatch");
        Check(request.body.find("\"inds_cd\":\"001\"") != std::string::npos,
              "index code body mismatch");

        const trading::MinuteBarsPage indexPage =
            trading::ParseIndexMinuteBarsResponse(
                "001",
                1,
                "{\"return_code\":0,\"inds_min_pole_qry\":["
                "{\"cur_prc\":\"3,200.00\",\"trde_qty\":\"10\","
                "\"cntr_tm\":\"20260803090100\",\"open_pric\":\"3,199.00\","
                "\"high_pric\":\"3,201.00\",\"low_pric\":\"3,198.00\"}]}"
            );
        Check(indexPage.result.ok, "valid index minute response must parse");
        Check(indexPage.bars.size() == 1U,
              "index minute bar count mismatch");
        Check(indexPage.bars.front().close == 320000,
              "decimal index close must normalize to x100 integer");

        const trading::MinuteBarsPage missing =
            trading::ParseStockMinuteBarsResponse(
                "005930", 1, "{\"return_code\":0}");
        Check(!missing.result.ok, "missing array must fail");
        Check(missing.result.error.find("stk_min_pole_chart_qry") != std::string::npos,
              "missing array error must name the field");

        const trading::MinuteBarsPage wrongApi =
            trading::ParseStockMinuteBarsResponse(
                "005930",
                1,
                "{\"return_code\":0,\"stk_tic_chart_qry\":[]}");
        Check(!wrongApi.result.ok,
              "tick-chart response must not be accepted as minute bars");
        Check(wrongApi.result.error.find("ka10080") != std::string::npos,
              "wrong API-ID error must identify ka10080");

        const trading::MinuteBarsPage broken =
            trading::ParseStockMinuteBarsResponse(
                "005930",
                1,
                "{\"return_code\":0,\"stk_min_pole_chart_qry\":["
                "{\"cur_prc\":\"100\",\"trde_qty\":\"1\","
                "\"cntr_tm\":\"20260803090100\",\"open_pric\":\"100\","
                "\"high_pric\":\"90\",\"low_pric\":\"80\"}]}"
            );
        Check(!broken.result.ok, "OHLC invariant violation must fail");

        const trading::RestRequest invalid =
            trading::BuildStockMinuteBarsRestRequest(
                "005930", 2, "token", true, {}, error);
        Check(!error.empty(), "unsupported minute unit must fail");
        Check(invalid.path.empty(), "invalid request must remain empty");
    }
}

int main()
{
    TestStockRequest();
    TestStrictStockParsing();
    TestStockTradeAggregation();
    TestIndexAndFailures();
    std::puts("[PASS] kiwoom_market_data_tests");
    return 0;
}
