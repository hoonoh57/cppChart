#include "../core/command_bus.h"
#include "../core/fault_policy.h"
#include "../core/json_lite.h"
#include "../core/market_types.h"
#include "../core/parameter_store.h"
#include "../core/trading_state.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
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

    void CheckNear(
        double actual,
        double expected,
        double tolerance,
        const char* message)
    {
        if (std::fabs(actual - expected) > tolerance) {
            std::fprintf(
                stderr,
                "[FAIL] %s actual=%.12f expected=%.12f\n",
                message,
                actual,
                expected);
            std::exit(1);
        }
    }

    void TestCommandBus()
    {
        std::atomic<int> wakeFrames{ 0 };
        CommandBus bus(&wakeFrames, 77);

        bus.Push(Cmd::LoadSymbol, "005930", 3);
        bus.Push(Cmd::LiquidateAll);

        Check(wakeFrames.load() == 77, "CommandBus must atomically wake the UI");

        Command first{};
        Command second{};

        Check(bus.Pop(first), "first command must exist");
        Check(first.type == Cmd::LoadSymbol, "command order must be FIFO");
        Check(first.arg == "005930", "command argument must be preserved");
        Check(first.i0 == 3, "command integer argument must be preserved");

        Check(bus.Pop(second), "second command must exist");
        Check(second.type == Cmd::LiquidateAll, "second command type mismatch");
        Check(!bus.Pop(second), "empty queue must return false");
    }

    void TestFaultPolicy()
    {
        std::atomic<bool> observeMode{ false };
        std::atomic<int> wakeFrames{ 0 };
        std::atomic<int> logCount{ 0 };

        FaultPolicy policy(
            &observeMode,
            &wakeFrames,
            [&](const char*, const char*) {
                logCount.fetch_add(1, std::memory_order_relaxed);
            },
            91);

        policy.Raise(Fault::PositionMismatch, "unit-test");

        Check(observeMode.load(), "PositionMismatch must enter observe mode");
        Check(wakeFrames.load() == 91, "FaultPolicy must atomically wake the UI");

        const FaultStat mismatch =
            policy.GetStat(Fault::PositionMismatch);

        Check(mismatch.total == 1, "fault total must increment");
        Check(mismatch.last == Action::Observe,
              "PositionMismatch action must be Observe");
        Check(logCount.load() == 1, "fault logger must receive a message");

        constexpr int kThreadCount = 8;
        constexpr int kRaisesPerThread = 1000;
        std::vector<std::thread> workers;

        for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex) {
            workers.emplace_back([&policy, kRaisesPerThread] {
                for (int index = 0; index < kRaisesPerThread; ++index) {
                    policy.Raise(Fault::WsDisconnected, "concurrent-test");
                }
            });
        }

        for (std::thread& worker : workers) worker.join();

        const FaultStat wsStat =
            policy.GetStat(Fault::WsDisconnected);

        Check(
            wsStat.total == kThreadCount * kRaisesPerThread,
            "concurrent fault increments must not be lost");
        Check(
            wsStat.last == Action::HardRestart,
            "WsDisconnected must escalate to HardRestart");
    }

    void TestMarketTypes()
    {
        trading::Bar bar;
        bar.open = 70100;
        bar.high = 70500;
        bar.low = 69900;
        bar.close = 70300;
        bar.volume = 1200345;
        bar.closeTimestampMs = 1785678900123LL;
        bar.tickCount = 418;

        Check(bar.closeTimestampMs == 1785678900123LL,
              "bar close timestamp must be preserved");
        Check(bar.tickCount == 418,
              "bar tick count must be preserved");

        trading::MoneyWon notional = 0;
        Check(
            trading::TryCalculateNotional(123456, 789, notional),
            "valid notional must calculate");
        Check(
            notional == 97406784LL,
            "price and quantity multiplication must be exact");

        trading::PositionSnapshot position;
        position.quantity = 5;
        position.costBasisWon = 502;
        position.currentPriceWon = 103;

        CheckNear(position.AveragePriceWon(), 100.4, 1e-12,
                  "average price must derive from exact cost basis");
        Check(position.EvaluationWon() == 515,
              "evaluation must use integer won arithmetic");
        Check(position.UnrealizedPnlWon() == 13,
              "unrealized PnL must be exact");
    }

    void TestJsonLite()
    {
        const json_lite::ParseResult parsed = json_lite::Parse(
            "{\"ok\":true,\"count\":3,\"name\":\"한글\",\"items\":[1,2,3]}");

        Check(parsed.ok, "valid JSON must parse");
        Check(parsed.value.Find("ok")->AsBoolean(),
              "boolean JSON value mismatch");
        Check(parsed.value.Find("count")->AsInt() == 3,
              "number JSON value mismatch");
        Check(parsed.value.Find("name")->AsString() == "한글",
              "UTF-8 JSON string mismatch");
        Check(parsed.value.Find("items")->AsArray().size() == 3,
              "JSON array size mismatch");

        const json_lite::ParseResult invalid =
            json_lite::Parse("{\"broken\":]");
        Check(!invalid.ok, "invalid JSON must fail");
    }

    void TestParameterStore()
    {
        int visibleBars = 220;
        float minimumBeta = 1.0f;
        bool showVolume = true;
        float upColor[4] = { 0.9f, 0.2f, 0.2f, 1.0f };

        trading::ParameterStore store;
        Check(store.RegisterInteger(
                  "chart", "chart.visible_bars", "Visible bars",
                  visibleBars, 30, 2000),
              "integer parameter registration failed");
        Check(store.RegisterFloat(
                  "scanner", "scanner.minimum_beta", "Minimum beta",
                  minimumBeta, 0.0f, 3.0f),
              "float parameter registration failed");
        Check(store.RegisterBoolean(
                  "chart", "chart.show_volume", "Show volume",
                  showVolume),
              "boolean parameter registration failed");
        Check(store.RegisterColor4(
                  "chart", "chart.up_color", "Up color",
                  upColor),
              "color parameter registration failed");
        Check(!store.RegisterInteger(
                  "chart", "chart.visible_bars", "Duplicate",
                  visibleBars, 30, 2000),
              "duplicate parameter key must be rejected");

        const std::string saved = store.SaveJson();

        visibleBars = 50;
        minimumBeta = 2.5f;
        showVolume = false;
        upColor[0] = 0.0f;

        std::string error;
        Check(store.LoadJson(saved, error),
              "saved parameter JSON must load");
        Check(visibleBars == 220,
              "integer parameter roundtrip mismatch");
        CheckNear(minimumBeta, 1.0, 1e-6,
                  "float parameter roundtrip mismatch");
        Check(showVolume,
              "boolean parameter roundtrip mismatch");
        CheckNear(upColor[0], 0.9, 1e-6,
                  "color parameter roundtrip mismatch");

        const std::string invalid =
            "{\"schema\":1,\"parameters\":{" 
            "\"chart.visible_bars\":300," 
            "\"scanner.minimum_beta\":99}}";

        visibleBars = 220;
        minimumBeta = 1.0f;
        Check(!store.LoadJson(invalid, error),
              "out-of-range parameter document must fail");
        Check(visibleBars == 220 && minimumBeta == 1.0f,
              "failed parameter load must be transactional");
    }

    void TestTradingState()
    {
        trading::TradingState state;

        trading::CumulativeFill buy;
        buy.orderId = "B-1";
        buy.executionId = "E-1";
        buy.code = "005930";
        buy.name = "삼성전자";
        buy.side = trading::OrderSide::Buy;
        buy.cumulativeQuantity = 5;
        buy.fillPriceWon = 1000;

        trading::ApplyFillResult result =
            state.ApplyCumulativeFill(buy);

        Check(result.status == trading::ApplyFillStatus::Applied,
              "first buy fill must apply");
        Check(result.appliedQuantity == 5,
              "first cumulative fill delta mismatch");

        result = state.ApplyCumulativeFill(buy);
        Check(result.status == trading::ApplyFillStatus::Duplicate,
              "duplicate execution must not apply twice");

        buy.executionId = "E-2";
        buy.cumulativeQuantity = 8;
        buy.fillPriceWon = 1010;
        result = state.ApplyCumulativeFill(buy);

        Check(result.status == trading::ApplyFillStatus::Applied,
              "higher cumulative fill must apply");
        Check(result.appliedQuantity == 3,
              "5 to 8 cumulative fill must apply only 3 shares");

        std::vector<trading::PositionSnapshot> positions =
            state.SnapshotPositions();

        Check(positions.size() == 1,
              "buy fills must create one position");
        Check(positions[0].quantity == 8,
              "position quantity after partial fills mismatch");
        Check(positions[0].costBasisWon == 8030,
              "position cost basis after partial fills mismatch");
        CheckNear(positions[0].AveragePriceWon(), 1003.75, 1e-12,
                  "weighted average must be exact from cost basis");

        buy.executionId = "E-3";
        buy.cumulativeQuantity = 7;
        result = state.ApplyCumulativeFill(buy);
        Check(result.status == trading::ApplyFillStatus::Stale,
              "lower cumulative fill must be ignored as stale");

        trading::CumulativeFill sell;
        sell.orderId = "S-1";
        sell.executionId = "S-E-1";
        sell.code = "005930";
        sell.name = "삼성전자";
        sell.side = trading::OrderSide::Sell;
        sell.cumulativeQuantity = 3;
        sell.fillPriceWon = 1100;

        result = state.ApplyCumulativeFill(sell);
        Check(result.status == trading::ApplyFillStatus::Applied,
              "first sell fill must apply");
        Check(result.realizedPnlWon == 289,
              "first partial sell realized PnL mismatch");

        sell.executionId = "S-E-2";
        sell.cumulativeQuantity = 5;
        result = state.ApplyCumulativeFill(sell);
        Check(result.appliedQuantity == 2,
              "sell cumulative delta mismatch");
        Check(result.realizedPnlWon == 193,
              "second partial sell realized PnL mismatch");

        sell.executionId = "S-E-3";
        sell.cumulativeQuantity = 8;
        result = state.ApplyCumulativeFill(sell);
        Check(result.appliedQuantity == 3,
              "final sell cumulative delta mismatch");
        Check(result.realizedPnlWon == 288,
              "final sell realized PnL mismatch");
        Check(state.RealizedPnlWon() == 770,
              "total realized PnL must preserve exact cost basis");
        Check(state.SnapshotPositions().empty(),
              "fully sold position must be removed");

        trading::PositionSnapshot first;
        first.code = "000660";
        first.name = "SK하이닉스";
        first.quantity = 10;
        first.costBasisWon = 1800000;
        first.currentPriceWon = 185000;

        trading::PositionSnapshot second;
        second.code = "005930";
        second.name = "삼성전자";
        second.quantity = 20;
        second.costBasisWon = 1400000;
        second.currentPriceWon = 71000;

        std::string error;
        Check(state.ReconcilePosition(first, error),
              "first broker position reconciliation failed");
        Check(state.ReconcilePosition(second, error),
              "second broker position reconciliation failed");
        Check(state.SetSelected("005930", true),
              "position selection failed");

        const std::vector<trading::LiquidationOrder> selected =
            state.BuildLiquidationPlan(true);

        Check(selected.size() == 1,
              "selected liquidation plan must contain one symbol");
        Check(selected[0].code == "005930" && selected[0].quantity == 20,
              "selected liquidation order mismatch");

        const std::vector<trading::LiquidationOrder> all =
            state.BuildLiquidationPlan(false);
        Check(all.size() == 2,
              "full liquidation plan must contain all positions");
    }
}

int main()
{
    TestCommandBus();
    TestFaultPolicy();
    TestMarketTypes();
    TestJsonLite();
    TestParameterStore();
    TestTradingState();

    std::puts("[PASS] core_tests");
    return 0;
}
