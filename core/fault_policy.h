#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>

enum class Fault
{
    DeviceLost,
    RenderStall,
    WsDisconnected,
    WsStale,
    HttpRateLimited,
    TokenExpired,
    BarGap,
    DataCorrupt,
    OrderRejected,
    PositionMismatch,
    COUNT
};

enum class Action
{
    Ignore,
    SoftReset,
    FeedReset,
    HardRestart,
    Observe
};

struct FaultRule
{
    Action first;
    int windowSeconds;
    int threshold;
    Action escalated;
};

struct FaultStat
{
    int total = 0;
    int recent = 0;
    double windowStart = 0.0;
    Action last = Action::Ignore;
};

class FaultPolicy final
{
public:
    using LogCallback =
        std::function<void(
            const char* category,
            const char* message)>;

    FaultPolicy(
        std::atomic<bool>* observeMode,
        int* wakeFrames,
        LogCallback logger,
        int wakeValue = 60);

    FaultPolicy(const FaultPolicy&) = delete;
    FaultPolicy& operator=(const FaultPolicy&) = delete;

    void Raise(
        Fault fault,
        const char* context);

    const FaultRule& GetRule(
        Fault fault) const noexcept;

    const FaultStat& GetStat(
        Fault fault) const noexcept;

    static const char* FaultName(
        Fault fault) noexcept;

    static const char* ActionName(
        Action action) noexcept;

private:
    static double NowSeconds() noexcept;

    std::array<
        FaultStat,
        static_cast<std::size_t>(Fault::COUNT)> stats_{};

    std::atomic<bool>* observeMode_ = nullptr;
    int* wakeFrames_ = nullptr;
    LogCallback logger_;
    int wakeValue_ = 60;
};