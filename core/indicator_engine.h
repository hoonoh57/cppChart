#pragma once

#include "market_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace trading::indicators
{
    constexpr std::size_t MaxIndicatorOutputs = 8;

    enum class IndicatorFault
    {
        None,
        InvalidInstance,
        InvalidInput,
        TimestampMovedBackward
    };

    struct IndicatorValue final
    {
        EpochMillis timestampMs = 0;
        std::array<double, MaxIndicatorOutputs> values{};
        std::uint8_t outputCount = 0;
        std::uint8_t readyMask = 0;
        bool replaced = false;
        IndicatorFault fault = IndicatorFault::None;

        bool IsReady(std::size_t index) const noexcept
        {
            return
                index < outputCount &&
                index < MaxIndicatorOutputs &&
                (readyMask & static_cast<std::uint8_t>(1U << index)) != 0;
        }

        double Value(std::size_t index) const noexcept
        {
            return index < outputCount && index < MaxIndicatorOutputs
                ? values[index]
                : 0.0;
        }

        void SetOutput(
            std::size_t index,
            double value,
            bool ready = true) noexcept
        {
            if (index >= MaxIndicatorOutputs) return;

            values[index] = value;
            const std::size_t requiredCount = index + 1U;
            if (requiredCount > outputCount) {
                outputCount = static_cast<std::uint8_t>(requiredCount);
            }

            const std::uint8_t bit =
                static_cast<std::uint8_t>(1U << index);
            if (ready) readyMask = static_cast<std::uint8_t>(readyMask | bit);
            else readyMask = static_cast<std::uint8_t>(readyMask & ~bit);
        }
    };

    struct IndicatorSpec final
    {
        std::string id;
        std::string type;
        std::map<std::string, double> parameters;
    };

    bool ValidateIndicatorSpec(
        const IndicatorSpec& spec,
        std::string& error);

    bool SerializeIndicatorSpec(
        const IndicatorSpec& spec,
        std::string& json,
        std::string& error);

    bool ParseIndicatorSpec(
        const std::string& json,
        IndicatorSpec& spec,
        std::string& error);

    bool TryGetIntegerParameter(
        const IndicatorSpec& spec,
        const std::string& key,
        int minimum,
        int maximum,
        int& value,
        std::string& error);

    const char* IndicatorFaultMessage(IndicatorFault fault) noexcept;

    class IndicatorInstance final
    {
    public:
        using ResetFunction = void(*)(void*) noexcept;
        using UpdateFunction = IndicatorValue(*)(void*, const Bar&) noexcept;
        using DestroyFunction = void(*)(void*) noexcept;
        using RetainedBytesFunction = std::size_t(*)(const void*) noexcept;

        IndicatorInstance() = default;

        IndicatorInstance(
            std::string type,
            void* state,
            ResetFunction reset,
            UpdateFunction update,
            DestroyFunction destroy,
            RetainedBytesFunction retainedBytes) noexcept;

        ~IndicatorInstance();

        IndicatorInstance(const IndicatorInstance&) = delete;
        IndicatorInstance& operator=(const IndicatorInstance&) = delete;

        IndicatorInstance(IndicatorInstance&& other) noexcept;
        IndicatorInstance& operator=(IndicatorInstance&& other) noexcept;

        bool IsValid() const noexcept;
        const std::string& Type() const noexcept;
        void Reset() noexcept;
        IndicatorValue Update(const Bar& bar) noexcept;
        std::size_t RetainedBytes() const noexcept;

    private:
        void Release() noexcept;

        std::string type_;
        void* state_ = nullptr;
        ResetFunction reset_ = nullptr;
        UpdateFunction update_ = nullptr;
        DestroyFunction destroy_ = nullptr;
        RetainedBytesFunction retainedBytes_ = nullptr;
    };

    using IndicatorFactory = std::function<IndicatorInstance(
        const IndicatorSpec&,
        std::string&)>;

    class IndicatorRegistry final
    {
    public:
        bool Register(
            const std::string& type,
            IndicatorFactory factory);

        bool Contains(const std::string& type) const noexcept;

        IndicatorInstance Create(
            const IndicatorSpec& spec,
            std::string& error) const;

        std::vector<std::string> Types() const;

    private:
        std::map<std::string, IndicatorFactory> factories_;
    };

    bool CalculateBatch(
        IndicatorInstance& instance,
        const std::vector<Bar>& bars,
        std::vector<IndicatorValue>& output,
        std::string& error);
}
