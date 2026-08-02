#include "command_bus.h"

#include <utility>

CommandBus::CommandBus(
    int* wakeFrames,
    int wakeValue) noexcept
    : legacyWakeFrames_(wakeFrames),
      wakeValue_(wakeValue)
{
}

CommandBus::CommandBus(
    std::atomic<int>* wakeFrames,
    int wakeValue) noexcept
    : atomicWakeFrames_(wakeFrames),
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

    Wake();
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

void CommandBus::Wake() noexcept
{
    if (atomicWakeFrames_ != nullptr) {
        int current = atomicWakeFrames_->load(std::memory_order_relaxed);

        while (
            current < wakeValue_ &&
            !atomicWakeFrames_->compare_exchange_weak(
                current,
                wakeValue_,
                std::memory_order_relaxed,
                std::memory_order_relaxed))
        {
        }
        return;
    }

    if (legacyWakeFrames_ != nullptr) {
        *legacyWakeFrames_ = wakeValue_;
    }
}
