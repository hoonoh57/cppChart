#include "../core/kiwoom_gateway_core.h"

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

    trading::RealTimeEnvelope MakeFillEnvelope(
        const std::string& brokerOrderNumber,
        const std::string& executionId,
        const std::string& code,
        int orderQuantity,
        int unfilledQuantity,
        int fillQuantity,
        int fillPrice)
    {
        trading::RealTimeEnvelope envelope;
        envelope.result.ok = true;
        envelope.transactionName = "REAL";

        trading::RealTimeRecord record;
        record.type = "00";
        record.values["9203"] = brokerOrderNumber;
        record.values["909"] = executionId;
        record.values["9001"] = code;
        record.values["302"] = "삼성전자";
        record.values["907"] = "2";
        record.values["900"] = std::to_string(orderQuantity);
        record.values["902"] = std::to_string(unfilledQuantity);
        record.values["910"] = std::to_string(fillPrice);
        record.values["911"] = std::to_string(fillQuantity);
        record.values["908"] = "091500";
        record.values["913"] = "체결";
        envelope.records.push_back(record);
        return envelope;
    }

    void TestAcceptedOrderFillAndDuplicate()
    {
        trading::TradingState state;
        trading::OrderCoordinator coordinator(state);
        trading::KiwoomGatewayCore gateway(state, coordinator);

        coordinator.SetSubmissionAllowed(true);

        trading::OrderIntent intent;
        intent.code = "005930";
        intent.name = "삼성전자";
        intent.side = trading::StockOrderSide::Buy;
        intent.quantity = 10;

        const trading::CreateOrderResult created =
            coordinator.CreateOrder(intent);
        Check(created.ok, "buy intent must be created");

        std::string error;
        Check(
            coordinator.MarkRestAccepted(
                created.order.clientIntentId,
                "0000123",
                error),
            "REST accepted order must bind broker number");

        const trading::RealTimeEnvelope envelope =
            MakeFillEnvelope(
                "0000123",
                "E-1",
                "A005930",
                10,
                4,
                2,
                71000);

        trading::GatewayEventReport report =
            gateway.ApplyRealTimeEnvelope(envelope);

        Check(report.Ok(), "accepted fill must apply without errors");
        Check(report.fillEventsApplied == 1,
              "first fill must be applied");

        std::vector<trading::PositionSnapshot> positions =
            state.SnapshotPositions();
        Check(positions.size() == 1 && positions[0].quantity == 6,
              "cumulative fill must create six-share position");
        Check(positions[0].costBasisWon == 426000,
              "fill notional must be exact integer won");

        report = gateway.ApplyRealTimeEnvelope(envelope);
        Check(report.Ok(), "duplicate fill must not be an error");
        Check(report.fillEventsDuplicate == 1,
              "duplicate execution must be classified");
        Check(state.SnapshotPositions()[0].quantity == 6,
              "duplicate fill must not change quantity");
    }

    void TestFillBeforeRestAcknowledgement()
    {
        trading::TradingState state;
        trading::OrderCoordinator coordinator(state);
        trading::KiwoomGatewayCore gateway(state, coordinator);

        coordinator.SetSubmissionAllowed(true);

        trading::OrderIntent intent;
        intent.code = "005930";
        intent.name = "삼성전자";
        intent.side = trading::StockOrderSide::Buy;
        intent.quantity = 5;

        const trading::CreateOrderResult created =
            coordinator.CreateOrder(intent);
        Check(created.ok, "pending intent must be created");

        const trading::GatewayEventReport report =
            gateway.ApplyRealTimeEnvelope(
                MakeFillEnvelope(
                    "0000999",
                    "EARLY-1",
                    "005930",
                    5,
                    2,
                    3,
                    70000));

        Check(report.Ok() && report.fillEventsApplied == 1,
              "fill-before-ack must apply once as an orphan broker event");
        Check(state.SnapshotPositions()[0].quantity == 3,
              "early fill quantity mismatch");

        std::string error;
        Check(
            coordinator.MarkRestAccepted(
                created.order.clientIntentId,
                "0000999",
                error),
            "late REST acknowledgement must bind orphan event");

        const std::vector<trading::OrderRecord> orders =
            coordinator.SnapshotOrders();
        Check(orders.size() == 1,
              "orphan order must merge into the client intent");
        Check(orders[0].cumulativeFilledQuantity == 3,
              "merged orphan cumulative fill mismatch");
    }

    void TestBalanceEventAndAuthoritativeSnapshot()
    {
        trading::TradingState state;
        trading::OrderCoordinator coordinator(state);
        trading::KiwoomGatewayCore gateway(state, coordinator);

        trading::PositionSnapshot initial;
        initial.code = "005930";
        initial.name = "삼성전자";
        initial.quantity = 6;
        initial.costBasisWon = 420000;
        initial.currentPriceWon = 71000;

        std::string error;
        Check(state.ReconcilePosition(initial, error),
              "initial position setup failed");
        Check(state.SetSelected("005930", true),
              "initial selection setup failed");

        trading::RealTimeEnvelope balanceEnvelope;
        balanceEnvelope.result.ok = true;
        trading::RealTimeRecord balance;
        balance.type = "04";
        balance.values["9001"] = "A005930";
        balance.values["302"] = "삼성전자";
        balance.values["930"] = "8";
        balance.values["933"] = "8";
        balance.values["931"] = "70500";
        balance.values["932"] = "564000";
        balance.values["10"] = "+71500";
        balanceEnvelope.records.push_back(balance);

        const trading::GatewayEventReport report =
            gateway.ApplyRealTimeEnvelope(balanceEnvelope);
        Check(report.Ok() && report.balanceUpdatesApplied == 1,
              "04 balance event must reconcile the position");

        std::vector<trading::PositionSnapshot> positions =
            state.SnapshotPositions();
        Check(positions.size() == 1 && positions[0].quantity == 8,
              "balance event quantity mismatch");
        Check(positions[0].costBasisWon == 564000,
              "balance event cost basis mismatch");

        trading::PositionSnapshot replacement;
        replacement.code = "000660";
        replacement.name = "SK하이닉스";
        replacement.quantity = 2;
        replacement.costBasisWon = 360000;
        replacement.currentPriceWon = 185000;

        Check(
            gateway.ApplyAuthoritativePositions({ replacement }, error),
            "authoritative broker snapshot must apply");

        positions = state.SnapshotPositions();
        Check(positions.size() == 1 && positions[0].code == "000660",
              "authoritative snapshot must remove broker-absent positions");
    }

    void TestInvalidEnvelopeIsContained()
    {
        trading::TradingState state;
        trading::OrderCoordinator coordinator(state);
        trading::KiwoomGatewayCore gateway(state, coordinator);

        trading::RealTimeEnvelope envelope;
        envelope.result.ok = false;
        envelope.result.error = "fixture transport error";

        const trading::GatewayEventReport report =
            gateway.ApplyRealTimeEnvelope(envelope);

        Check(!report.Ok() && report.errors.size() == 1,
              "failed envelope must not mutate state");
        Check(state.SnapshotPositions().empty(),
              "failed envelope must leave positions unchanged");
    }
}

int main()
{
    TestAcceptedOrderFillAndDuplicate();
    TestFillBeforeRestAcknowledgement();
    TestBalanceEventAndAuthoritativeSnapshot();
    TestInvalidEnvelopeIsContained();

    std::puts("[PASS] kiwoom_gateway_core_tests");
    return 0;
}
