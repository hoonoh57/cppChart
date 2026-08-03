#pragma once

#include "market_types.h"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace trading::indicators
{
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
        double value = 0.0;
        bool ready = false;
        bool replaced = false;
        IndicatorFault fault = IndicatorFault::None;
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
