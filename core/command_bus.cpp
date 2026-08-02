#include "command_bus.h"

#include <utility>

CommandBus::CommandBus(
    int* wakeFrames,
    int wakeValue) noexcept
    : wakeFrames_(wakeFrames),
      wakeValue_(wakeValue)
{
}

void CommandBus::Push(
    Cmd type,
    std::string arg,
    int i0)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);

        queue_.push_back(
            Command{
                type,
                std::move(arg),
                i0
            });
    }

    if (wakeFrames_ != nullptr) {
        *wakeFrames_ = wakeValue_;
    }
}

bool CommandBus::Pop(Command& out)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (queue_.empty()) {
        return false;
    }

    out = std::move(queue_.front());
    queue_.pop_front();

    return true;
}