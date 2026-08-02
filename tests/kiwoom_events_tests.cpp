#include "../core/kiwoom_events.h"

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

    void TestOrderExecution()
    {
        trading::RealTimeRecord record;
        record.type = "00";
        record.name = "주문체결";
        record.values["9203"] = "0000123";
        record.values["904"] = "0000000";
        record.values["9001"] = "A005930";
        record.values["302"] = "삼성전자";
        record.values["907"] = "2";
        record.values["900"] = "10";
        record.values["902"] = "4";
        record.values["909"] = "7654321";
        record.values["910"] = "+71000";
        record.values["911"] = "2";
        record.values["908"] = "091501123";
        record.values["913"] = "체결";

        const trading::EpochMillis dayStart = 1000000000LL;
        const trading::OrderExecutionDecodeResult decoded =
            trading::DecodeKiwoomOrderExecution(record, dayStart);

        Check(
            decoded.status == trading::EventDecodeStatus::Decoded,
            "00 event must decode as an execution");
        Check(decoded.event.code == "005930",
              "A-prefixed stock code must normalize");
        Check(decoded.event.side == trading::StockOrderSide::Buy,
              "buy side mapping mismatch");
        Check(decoded.event.orderedQuantity == 10,
              "ordered quantity mismatch");
        Check(decoded.event.unfilledQuantity == 4,
              "unfilled quantity mismatch");
        Check(decoded.event.cumulativeQuantity == 6,
              "cumulative quantity must derive from order minus unfilled");
        Check(decoded.event.lastFillQuantity == 2,
              "last fill quantity mismatch");
        Check(decoded.event.fillPriceWon == 71000,
              "signed price must normalize to positive won");
        Check(
            decoded.event.executionTimestampMs ==
                dayStart +
                9LL * 60 * 60 * 1000 +
                15LL * 60 * 1000 +
                1LL * 1000 +
                123,
            "execution timestamp mismatch");
    }

    void TestGeneratedExecutionIdAndNoFill()
    {
        trading::RealTimeRecord record;
        record.type = "00";
        record.values["ord_no"] = "77";
        record.values["stk_cd"] = "000660";
        record.values["io_tp_nm"] = "-매도";
        record.values["ord_qty"] = "5";
        record.values["oso_qty"] = "2";
        record.values["cntr_pric"] = "180000";
        record.values["cntr_qty"] = "1";

        trading::OrderExecutionDecodeResult decoded =
            trading::DecodeKiwoomOrderExecution(record);

        Check(decoded.status == trading::EventDecodeStatus::Decoded,
              "alias fields must decode");
        Check(decoded.event.side == trading::StockOrderSide::Sell,
              "sell side alias mismatch");
        Check(decoded.event.executionId == "77:3",
              "missing execution number must use deterministic cumulative key");

        record.values["oso_qty"] = "5";
        record.values["cntr_qty"] = "0";
        decoded = trading::DecodeKiwoomOrderExecution(record);
        Check(decoded.status == trading::EventDecodeStatus::NoExecution,
              "accepted but unfilled order update must not become a fill");
    }

    void TestSignedCommaWhitespaceAndLowercaseAliases()
    {
        trading::RealTimeRecord record;
        record.type = "00";
        record.values["order_no"] = " 90001 ";
        record.values["code"] = " a005930 ";
        record.values["name"] = " 삼성전자 ";
        record.values["order_side"] = " buy ";
        record.values["order_qty"] = " 1,000 ";
        record.values["unfilled_qty"] = " 250 ";
        record.values["fill_qty"] = " 250 ";
        record.values["fill_price"] = " -71,500 ";
        record.values["execution_time"] = "09:31:02.456";

        const trading::OrderExecutionDecodeResult decoded =
            trading::DecodeKiwoomOrderExecution(record, 5000);

        Check(decoded.status == trading::EventDecodeStatus::Decoded,
              "signed comma-delimited alias payload must decode");
        Check(decoded.event.code == "005930",
              "lowercase A prefix and whitespace must normalize");
        Check(decoded.event.orderedQuantity == 1000,
              "comma-delimited order quantity mismatch");
        Check(decoded.event.unfilledQuantity == 250,
              "whitespace unfilled quantity mismatch");
        Check(decoded.event.cumulativeQuantity == 750,
              "derived cumulative quantity mismatch");
        Check(decoded.event.fillPriceWon == 71500,
              "signed comma-delimited fill price mismatch");
        Check(decoded.event.executionId == "90001:750",
              "fallback execution key must use normalized order number");
        Check(
            decoded.event.executionTimestampMs ==
                5000 +
                9LL * 60 * 60 * 1000 +
                31LL * 60 * 1000 +
                2LL * 1000 +
                456,
            "punctuated execution time mismatch");
    }

    void TestOrderExecutionRejectsImpossibleQuantities()
    {
        trading::RealTimeRecord record;
        record.type = "00";
        record.values["ord_no"] = "IMPOSSIBLE";
        record.values["stk_cd"] = "005930";
        record.values["io_tp_nm"] = "매수";
        record.values["ord_qty"] = "10";
        record.values["oso_qty"] = "11";
        record.values["cntr_pric"] = "71000";
        record.values["cntr_qty"] = "1";

        const trading::OrderExecutionDecodeResult decoded =
            trading::DecodeKiwoomOrderExecution(record);

        Check(decoded.status == trading::EventDecodeStatus::Invalid,
              "unfilled quantity larger than order must be rejected");
        Check(!decoded.error.empty(),
              "invalid order quantity relationship must explain the error");
    }

    void TestBalanceUpdate()
    {
        trading::RealTimeRecord record;
        record.type = "04";
        record.name = "잔고";
        record.values["9001"] = "A005930";
        record.values["302"] = "삼성전자";
        record.values["930"] = "12";
        record.values["933"] = "10";
        record.values["931"] = "70000";
        record.values["932"] = "840000";
        record.values["10"] = "-71000";

        const trading::BalanceDecodeResult decoded =
            trading::DecodeKiwoomBalanceUpdate(record);

        Check(decoded.status == trading::EventDecodeStatus::Decoded,
              "04 event must decode");
        Check(decoded.event.quantity == 12,
              "balance quantity mismatch");
        Check(decoded.event.availableQuantity == 10,
              "available quantity mismatch");
        Check(decoded.event.costBasisWon == 840000,
              "cost basis mismatch");
        Check(decoded.event.currentPriceWon == 71000,
              "current price sign normalization mismatch");

        const trading::PositionSnapshot position =
            trading::ToPositionSnapshot(decoded.event);
        Check(position.quantity == 12 && position.costBasisWon == 840000,
              "balance to position conversion mismatch");
    }

    void TestBalanceAliasesAndCostBasisFallback()
    {
        trading::RealTimeRecord record;
        record.type = "04";
        record.values["code"] = " a000660 ";
        record.values["name"] = " SK하이닉스 ";
        record.values["quantity"] = " 1,200 ";
        record.values["available_qty"] = " 1,100 ";
        record.values["average_price"] = " -70,000 ";
        record.values["current_price"] = " +71,000 ";

        const trading::BalanceDecodeResult decoded =
            trading::DecodeKiwoomBalanceUpdate(record);

        Check(decoded.status == trading::EventDecodeStatus::Decoded,
              "balance aliases with signed comma values must decode");
        Check(decoded.event.code == "000660",
              "balance lowercase A-prefix normalization mismatch");
        Check(decoded.event.quantity == 1200,
              "balance comma quantity mismatch");
        Check(decoded.event.availableQuantity == 1100,
              "balance comma available quantity mismatch");
        Check(decoded.event.averagePriceWon == 70000,
              "signed average price normalization mismatch");
        Check(decoded.event.currentPriceWon == 71000,
              "signed current price normalization mismatch");
        Check(decoded.event.costBasisWon == 84000000,
              "missing cost basis must derive from exact price times quantity");
    }

    void TestBalanceRejectsAvailableQuantityOverflow()
    {
        trading::RealTimeRecord record;
        record.type = "04";
        record.values["stk_cd"] = "005930";
        record.values["rmnd_qty"] = "10";
        record.values["trde_able_qty"] = "11";
        record.values["pur_pric"] = "70000";

        const trading::BalanceDecodeResult decoded =
            trading::DecodeKiwoomBalanceUpdate(record);

        Check(decoded.status == trading::EventDecodeStatus::Invalid,
              "available quantity above held quantity must be rejected");
        Check(!decoded.error.empty(),
              "invalid available quantity must explain the error");
    }

    void TestZeroBalanceRemovesPosition()
    {
        trading::RealTimeRecord record;
        record.type = "04";
        record.values["stk_cd"] = "000660";
        record.values["rmnd_qty"] = "0";
        record.values["trde_able_qty"] = "0";

        const trading::BalanceDecodeResult decoded =
            trading::DecodeKiwoomBalanceUpdate(record);

        Check(decoded.status == trading::EventDecodeStatus::Decoded,
              "zero balance must be a valid removal event");
        Check(decoded.event.costBasisWon == 0,
              "zero balance must clear cost basis");
    }
}

int main()
{
    TestOrderExecution();
    TestGeneratedExecutionIdAndNoFill();
    TestSignedCommaWhitespaceAndLowercaseAliases();
    TestOrderExecutionRejectsImpossibleQuantities();
    TestBalanceUpdate();
    TestBalanceAliasesAndCostBasisFallback();
    TestBalanceRejectsAvailableQuantityOverflow();
    TestZeroBalanceRemovesPosition();

    std::puts("[PASS] kiwoom_events_tests");
    return 0;
}
