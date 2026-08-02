#include "../core/safe_liquidation.h"

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

    trading::OpenOrderSnapshot MakeSellOrder(
        const std::string& brokerOrderNumber,
        int orderedQuantity,
        int unfilledQuantity)
    {
        trading::OpenOrderSnapshot order;
        order.brokerOrderNumber = brokerOrderNumber;
        order.code = "005930";
        order.name = "삼성전자";
        order.side = trading::StockOrderSide::Sell;
        order.orderedQuantity = orderedQuantity;
        order.unfilledQuantity = unfilledQuantity;
        return order;
    }

    void TestKnownAndUnknownReservations()
    {
        trading::TradingState state;
        trading::PositionSnapshot position;
        position.code = "005930";
        position.name = "삼성전자";
        position.quantity = 10;
        position.costBasisWon = 700000;
        position.currentPriceWon = 71000;

        std::string error;
        Check(state.ReconcilePosition(position, error),
              "position setup failed");

        trading::OrderCoordinator coordinator(state);
        coordinator.SetSubmissionAllowed(true);

        trading::OrderIntent intent;
        intent.code = "005930";
        intent.name = "삼성전자";
        intent.side = trading::StockOrderSide::Sell;
        intent.quantity = 3;

        const trading::CreateOrderResult created =
            coordinator.CreateOrder(intent);
        Check(created.ok, "coordinator sell intent must be created");
        Check(
            coordinator.MarkRestAccepted(
                created.order.clientIntentId,
                "KNOWN-1",
                error),
            "coordinator sell intent must bind broker order");

        trading::BrokerOpenOrderRegistry registry;
        Check(
            registry.Replace(
                {
                    MakeSellOrder("KNOWN-1", 3, 3),
                    MakeSellOrder("MANUAL-1", 2, 2)
                },
                error),
            "broker open orders must load");

        const std::vector<trading::LiquidationOrder> plan =
            trading::BuildBrokerAwareLiquidationPlan(
                state,
                coordinator,
                registry,
                false);

        Check(plan.size() == 1,
              "liquidation plan must contain the held symbol");
        Check(plan[0].quantity == 5,
              "known order must not be double-reserved and manual order must reserve two");
    }

    void TestFullReservationSuppressesRetry()
    {
        trading::TradingState state;
        trading::PositionSnapshot position;
        position.code = "005930";
        position.name = "삼성전자";
        position.quantity = 4;
        position.costBasisWon = 280000;
        position.currentPriceWon = 71000;

        std::string error;
        Check(state.ReconcilePosition(position, error),
              "position setup failed");

        trading::OrderCoordinator coordinator(state);
        trading::BrokerOpenOrderRegistry registry;
        Check(
            registry.Replace(
                { MakeSellOrder("MANUAL-FULL", 4, 4) },
                error),
            "full manual sell reservation must load");

        const std::vector<trading::LiquidationOrder> plan =
            trading::BuildBrokerAwareLiquidationPlan(
                state,
                coordinator,
                registry,
                false);

        Check(plan.empty(),
              "already pending full liquidation must not be submitted again");
    }

    void TestRegistryReplacementIsTransactional()
    {
        trading::BrokerOpenOrderRegistry registry;
        std::string error;

        Check(
            registry.Replace(
                { MakeSellOrder("VALID-1", 2, 1) },
                error),
            "valid registry page must load");

        trading::OpenOrderSnapshot invalid =
            MakeSellOrder("INVALID-1", 2, 3);
        Check(!registry.Replace({ invalid }, error),
              "invalid registry page must fail");

        const std::vector<trading::OpenOrderSnapshot> snapshot =
            registry.Snapshot();
        Check(snapshot.size() == 1 &&
              snapshot[0].brokerOrderNumber == "VALID-1",
              "failed replacement must preserve the prior registry");
    }
}

int main()
{
    TestKnownAndUnknownReservations();
    TestFullReservationSuppressesRetry();
    TestRegistryReplacementIsTransactional();

    std::puts("[PASS] safe_liquidation_tests");
    return 0;
}
