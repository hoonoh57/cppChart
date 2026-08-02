#include "../core/command_bus.h"
#include "../core/fault_policy.h"

#include <atomic>
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
        if (!condition) {
            Fail(message);
        }
    }

    void TestCommandBus()
    {
        int wakeFrames = 0;
        CommandBus bus(&wakeFrames, 77);

        bus.Push(Cmd::LoadSymbol, "005930", 3);
        bus.Push(Cmd::LiquidateAll);

        Check(wakeFrames == 77, "CommandBus must wake the UI");

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
        int wakeFrames = 0;
        std::vector<std::string> messages;

        FaultPolicy policy(
            &observeMode,
            &wakeFrames,
            [&](const char* category, const char* message) {
                messages.emplace_back(
                    std::string(category) + ":" + message);
            },
            91);

        policy.Raise(Fault::PositionMismatch, "unit-test");

        Check(observeMode.load(), "PositionMismatch must enter observe mode");
        Check(wakeFrames == 91, "FaultPolicy must wake the UI");
        Check(policy.GetStat(Fault::PositionMismatch).total == 1,
              "fault total must increment");
        Check(policy.GetStat(Fault::PositionMismatch).last == Action::Observe,
              "PositionMismatch action must be Observe");
        Check(!messages.empty(), "fault logger must receive a message");

        for (int i = 0; i < 5; ++i) {
            policy.Raise(Fault::WsDisconnected, "unit-test");
        }

        const FaultStat& wsStat = policy.GetStat(Fault::WsDisconnected);
        Check(wsStat.total == 5, "WsDisconnected total must be five");
        Check(wsStat.recent == 5, "WsDisconnected recent count must be five");
        Check(wsStat.last == Action::HardRestart,
              "WsDisconnected must escalate to HardRestart at threshold");
    }
}

int main()
{
    TestCommandBus();
    TestFaultPolicy();

    std::puts("[PASS] core_tests");
    return 0;
}
