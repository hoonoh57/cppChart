#include "../core/json_lite.h"
#include "../core/kiwoom_protocol.h"
#include "../core/order_coordinator.h"
#include "../core/trading_state.h"

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

    trading::PositionSnapshot Position(
        const std::string& code,
        const std::string& name,
        trading::Quantity quantity,
        trading::PriceWon averagePrice,
        bool selected = false)
    {
        trading::PositionSnapshot position;
        position.code = code;
        position.name = name;
        position.quantity = quantity;
        position.costBasisWon =
            static_cast<trading::MoneyWon>(quantity) *
            averagePrice;
        position.currentPriceWon = averagePrice;
        position.selected = selected;
        return position;
    }

    void TestSubmissionGateAndRestRequest()
    {
        trading::TradingState state;
        trading::OrderCoordinator coordinator(state);

        trading::OrderIntent intent;
        intent.code = "005930";
        intent.name = "삼성전자";
        intent.side = trading::StockOrderSide::Buy;
        intent.type = trading::StockOrderType::Market;
        intent.quantity = 5;

        trading::CreateOrderResult blocked =
            coordinator.CreateOrder(intent);

        Check(!blocked.ok,
              "orders must be blocked before the session is ready");

        coordinator.SetSubmissionAllowed(true);
        trading::CreateOrderResult created =
            coordinator.CreateOrder(intent);

        Check(created.ok, "ready coordinator must create an order intent");
        Check(created.order.clientIntentId == "I-0000000001",
              "first client intent id mismatch");
        Check(created.order.lifecycle ==
                  trading::OrderLifecycle::PendingRest,
              "new order must be pending REST submission");

        trading::RestRequest request;
        std::string error;
        Check(coordinator.BuildRestRequest(
                  created.order.clientIntentId,
                  "access-token",
                  request,
                  error),
              "REST request generation failed");
        Check(request.apiId == "kt10000",
              "buy request api-id mismatch");
        Check(request.headers.at("authorization") ==
                  "Bearer access-token",
              "buy request authorization mismatch");

        const json_lite::ParseResult body =
            json_lite::Parse(request.body);
        Check(body.ok, "order request body must be valid JSON");
        Check(body.value.Find("stk_cd")->AsString() == "005930",
              "order request symbol mismatch");
        Check(body.value.Find("ord_qty")->AsString() == "5",
              "order request quantity mismatch");

        coordinator.BeginReconciliation();
        Check(!coordinator.SubmissionAllowed(),
              "reconciliation must block order submission");
        Check(!coordinator.CreateOrder(intent).ok,
              "new order must be rejected during reconciliation");

        coordinator.CompleteReconciliation(true);
        Check(coordinator.SubmissionAllowed(),
              "successful reconciliation must reopen the order gate");
    }

    void TestAcceptedPartialAndDuplicateFill()
    {
        trading::TradingState state;
        trading::OrderCoordinator coordinator(state);
        coordinator.SetSubmissionAllowed(true);

        trading::OrderIntent intent;
        intent.code = "000660";
        intent.name = "SK하이닉스";
        intent.side = trading::StockOrderSide::Buy;
        intent.quantity = 5;

        const trading::CreateOrderResult created =
            coordinator.CreateOrder(intent);

        std::string error;
        Check(coordinator.MarkRestAccepted(
                  created.order.clientIntentId,
                  "B100",
                  error),
              "REST acceptance binding failed");

        trading::BrokerFillEvent fill;
        fill.brokerOrderNumber = "B100";
        fill.executionId = "E1";
        fill.code = "000660";
        fill.name = "SK하이닉스";
        fill.side = trading::StockOrderSide::Buy;
        fill.cumulativeQuantity = 2;
        fill.fillPriceWon = 180000;

        trading::BrokerFillApplyResult applied =
            coordinator.ApplyBrokerFill(fill);

        Check(applied.error.empty(), "first partial fill failed");
        Check(applied.positionResult.status ==
                  trading::ApplyFillStatus::Applied,
              "first partial fill must apply");
        Check(applied.positionResult.appliedQuantity == 2,
              "first partial fill quantity mismatch");
        Check(applied.clientIntentId == created.order.clientIntentId,
              "fill must resolve to the client intent");

        applied = coordinator.ApplyBrokerFill(fill);
        Check(applied.positionResult.status ==
                  trading::ApplyFillStatus::Duplicate,
              "duplicate execution must not apply twice");

        fill.executionId = "E2";
        fill.cumulativeQuantity = 5;
        fill.fillPriceWon = 181000;
        applied = coordinator.ApplyBrokerFill(fill);

        Check(applied.positionResult.appliedQuantity == 3,
              "2 to 5 cumulative fill must apply only three shares");

        const std::vector<trading::OrderRecord> orders =
            coordinator.SnapshotOrders();

        Check(orders.size() == 1,
              "accepted order snapshot count mismatch");
        Check(orders[0].cumulativeFilledQuantity == 5,
              "filled order cumulative quantity mismatch");
        Check(orders[0].lifecycle == trading::OrderLifecycle::Filled,
              "fully filled order lifecycle mismatch");

        const std::vector<trading::PositionSnapshot> positions =
            state.SnapshotPositions();
        Check(positions.size() == 1 && positions[0].quantity == 5,
              "position quantity after cumulative fills mismatch");
        Check(positions[0].costBasisWon == 903000,
              "position exact cost basis mismatch");
    }

    void TestFillBeforeRestAcknowledgement()
    {
        trading::TradingState state;
        trading::OrderCoordinator coordinator(state);
        coordinator.SetSubmissionAllowed(true);

        trading::OrderIntent intent;
        intent.code = "005930";
        intent.name = "삼성전자";
        intent.side = trading::StockOrderSide::Buy;
        intent.quantity = 5;

        const trading::CreateOrderResult created =
            coordinator.CreateOrder(intent);

        trading::BrokerFillEvent fill;
        fill.brokerOrderNumber = "FAST-1";
        fill.executionId = "FAST-E1";
        fill.code = "005930";
        fill.name = "삼성전자";
        fill.side = trading::StockOrderSide::Buy;
        fill.cumulativeQuantity = 3;
        fill.fillPriceWon = 70100;

        const trading::BrokerFillApplyResult early =
            coordinator.ApplyBrokerFill(fill);

        Check(early.error.empty(),
              "fill-before-ack must not be rejected");
        Check(early.orphanBrokerEvent,
              "fill-before-ack must be retained as an orphan event");
        Check(state.SnapshotPositions().front().quantity == 3,
              "fill-before-ack must update the position immediately");

        std::string error;
        Check(coordinator.MarkRestAccepted(
                  created.order.clientIntentId,
                  "FAST-1",
                  error),
              "late REST acknowledgement must bind the orphan fill");

        const std::vector<trading::OrderRecord> orders =
            coordinator.SnapshotOrders();

        Check(orders.size() == 1,
              "orphan record must merge into the client order");
        Check(orders[0].clientIntentId == created.order.clientIntentId,
              "merged orphan client id mismatch");
        Check(orders[0].cumulativeFilledQuantity == 3,
              "merged orphan cumulative quantity mismatch");
        Check(orders[0].lifecycle ==
                  trading::OrderLifecycle::PartiallyFilled,
              "merged orphan lifecycle mismatch");
    }

    void TestIdempotentLiquidationReservation()
    {
        trading::TradingState state;
        std::string error;

        Check(state.ReconcilePosition(
                  Position("005930", "삼성전자", 10, 70000, true),
                  error),
              "initial position reconciliation failed");

        trading::OrderCoordinator coordinator(state);
        coordinator.SetSubmissionAllowed(true);

        std::vector<trading::CreateOrderResult> first =
            coordinator.CreateLiquidationOrders(true);

        Check(first.size() == 1 && first[0].ok,
              "selected liquidation must create one sell intent");
        Check(first[0].order.requestedQuantity == 10,
              "selected liquidation quantity mismatch");
        Check(coordinator.ReservedSellQuantity("005930") == 10,
              "full liquidation quantity must be reserved");

        const std::vector<trading::CreateOrderResult> repeated =
            coordinator.CreateLiquidationOrders(true);

        Check(repeated.empty(),
              "repeated liquidation must not duplicate a reserved sell");

        Check(coordinator.MarkRestAccepted(
                  first[0].order.clientIntentId,
                  "S100",
                  error),
              "liquidation REST acceptance failed");

        trading::BrokerFillEvent fill;
        fill.brokerOrderNumber = "S100";
        fill.executionId = "S-E1";
        fill.code = "005930";
        fill.name = "삼성전자";
        fill.side = trading::StockOrderSide::Sell;
        fill.cumulativeQuantity = 4;
        fill.fillPriceWon = 71000;

        const trading::BrokerFillApplyResult partial =
            coordinator.ApplyBrokerFill(fill);

        Check(partial.positionResult.appliedQuantity == 4,
              "liquidation partial fill quantity mismatch");
        Check(coordinator.ReservedSellQuantity("005930") == 6,
              "remaining liquidation reservation mismatch");
        Check(state.SnapshotPositions().front().quantity == 6,
              "remaining position quantity mismatch");
        Check(coordinator.CreateLiquidationOrders(true).empty(),
              "remaining position already reserved must not be re-ordered");

        Check(coordinator.MarkRestRejected(
                  first[0].order.clientIntentId,
                  "remaining quantity cancelled",
                  error),
              "partially filled order rejection handling failed");
        Check(coordinator.ReservedSellQuantity("005930") == 0,
              "rejected outstanding quantity must be released");

        const std::vector<trading::CreateOrderResult> retry =
            coordinator.CreateLiquidationOrders(true);

        Check(retry.size() == 1 && retry[0].ok,
              "released remainder must be available for retry");
        Check(retry[0].order.requestedQuantity == 6,
              "retry liquidation must use only the remaining position");
    }

    void TestRestRejectionReleasesSellReservation()
    {
        trading::TradingState state;
        std::string error;
        Check(state.ReconcilePosition(
                  Position("000660", "SK하이닉스", 7, 180000),
                  error),
              "position reconciliation failed");

        trading::OrderCoordinator coordinator(state);
        coordinator.SetSubmissionAllowed(true);

        trading::OrderIntent sell;
        sell.code = "000660";
        sell.name = "SK하이닉스";
        sell.side = trading::StockOrderSide::Sell;
        sell.quantity = 7;

        const trading::CreateOrderResult created =
            coordinator.CreateOrder(sell);

        Check(created.ok, "sell intent creation failed");
        Check(coordinator.ReservedSellQuantity("000660") == 7,
              "sell intent reservation mismatch");

        Check(coordinator.MarkRestRejected(
                  created.order.clientIntentId,
                  "broker rejected order",
                  error),
              "REST rejection handling failed");
        Check(coordinator.ReservedSellQuantity("000660") == 0,
              "REST rejection must release sell reservation");
    }
}

int main()
{
    TestSubmissionGateAndRestRequest();
    TestAcceptedPartialAndDuplicateFill();
    TestFillBeforeRestAcknowledgement();
    TestIdempotentLiquidationReservation();
    TestRestRejectionReleasesSellReservation();

    std::puts("[PASS] order_coordinator_tests");
    return 0;
}
