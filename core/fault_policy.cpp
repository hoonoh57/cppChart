#include "fault_policy.h"

#include <chrono>
#include <cstdio>
#include <utility>

namespace
{
    constexpr std::size_t kFaultCount =
        static_cast<std::size_t>(Fault::COUNT);

    constexpr std::array<const char*, kFaultCount> kFaultNames = {
        "DeviceLost",
        "RenderStall",
        "WsDisconnected",
        "WsStale",
        "HttpRateLimited",
        "TokenExpired",
        "BarGap",
        "DataCorrupt",
        "OrderRejected",
        "PositionMismatch"
    };

    constexpr std::array<const char*, 5> kActionNames = {
        "Ignore",
        "SoftReset",
        "FeedReset",
        "HardRestart",
        "Observe"
    };

    constexpr std::array<FaultRule, kFaultCount> kRules = {{
        { Action::SoftReset,  60,  3, Action::HardRestart },
        { Action::SoftReset,  60,  5, Action::HardRestart },
        { Action::FeedReset, 300,  5, Action::HardRestart },
        { Action::FeedReset, 300,  3, Action::HardRestart },
        { Action::Ignore,     60, 20, Action::FeedReset   },
        { Action::FeedReset, 600,  3, Action::Observe     },
        { Action::FeedReset, 300, 10, Action::HardRestart },
        { Action::FeedReset,  60,  3, Action::HardRestart },
        { Action::Ignore,     60,  3, Action::Observe     },
        { Action::Observe,     0,  0, Action::Observe     }
    }};
}

FaultPolicy::FaultPolicy(
    std::atomic<bool>* observeMode,
    int* wakeFrames,
    LogCallback logger,
    int wakeValue)
    : observeMode_(observeMode),
      wakeFrames_(wakeFrames),
      logger_(std::move(logger)),
      wakeValue_(wakeValue)
{
}

void FaultPolicy::Raise(
    Fault fault,
    const char* context)
{
    const std::size_t index =
        static_cast<std::size_t>(fault);

    const FaultRule& rule = kRules[index];
    FaultStat& stat = stats_[index];

    const double now = NowSeconds();

    if (
        rule.windowSeconds > 0 &&
        now - stat.windowStart > rule.windowSeconds)
    {
        stat.windowStart = now;
        stat.recent = 0;
    }

    ++stat.total;
    ++stat.recent;

    const Action action =
        rule.threshold > 0 &&
        stat.recent >= rule.threshold
            ? rule.escalated
            : rule.first;

    stat.last = action;

    if (
        action == Action::Observe &&
        observeMode_ != nullptr)
    {
        observeMode_->store(true);
    }

    if (logger_) {
        char message[320];

        std::snprintf(
            message,
            sizeof(message),
            "%s (%s) x%d -> %s",
            FaultName(fault),
            context != nullptr ? context : "",
            stat.recent,
            ActionName(action));

        logger_("FAULT", message);
    }

    if (wakeFrames_ != nullptr) {
        *wakeFrames_ = wakeValue_;
    }
}

const FaultRule& FaultPolicy::GetRule(
    Fault fault) const noexcept
{
    return kRules[
        static_cast<std::size_t>(fault)];
}

const FaultStat& FaultPolicy::GetStat(
    Fault fault) const noexcept
{
    return stats_[
        static_cast<std::size_t>(fault)];
}

const char* FaultPolicy::FaultName(
    Fault fault) noexcept
{
    return kFaultNames[
        static_cast<std::size_t>(fault)];
}

const char* FaultPolicy::ActionName(
    Action action) noexcept
{
    return kActionNames[
        static_cast<std::size_t>(action)];
}

double FaultPolicy::NowSeconds() noexcept
{
    using Clock = std::chrono::steady_clock;

    static const Clock::time_point start =
        Clock::now();

    return std::chrono::duration<double>(
        Clock::now() - start).count();
}