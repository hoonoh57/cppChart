#pragma once

#include <deque>
#include <mutex>
#include <string>

enum class Cmd
{
    LoadSymbol,
    Backfill,
    PromoteTarget,
    DemoteTarget,
    LiquidateAll,
    LiquidateSelected,
    ArmStrategy,
    DisarmStrategy,
    OpenMultiChart,
    ResetSoft,
    ResetFeed,
    ResetHard
};

struct Command
{
    Cmd type;
    std::string arg;
    int i0 = 0;
};

class CommandBus final
{
public:
    explicit CommandBus(
        int* wakeFrames = nullptr,
        int wakeValue = 60) noexcept;

    CommandBus(const CommandBus&) = delete;
    CommandBus& operator=(const CommandBus&) = delete;

    void Push(
        Cmd type,
        std::string arg = {},
        int i0 = 0);

    bool Pop(Command& out);

private:
    std::mutex mutex_;
    std::deque<Command> queue_;

    int* wakeFrames_ = nullptr;
    int wakeValue_ = 60;
};