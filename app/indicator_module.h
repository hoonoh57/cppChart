#pragma once

#include "feature_registry.h"
#include "../core/indicator_engine.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace trading::app
{
    struct IndicatorMarketSource final
    {
        std::string symbol;
        std::shared_ptr<const std::vector<Bar>> completedBars;
        Bar liveBar;
        bool hasLiveBar = false;
        std::uint64_t revision = 0;
        std::uint64_t completedRevision = 0;
    };

    struct IndicatorSeriesSnapshot final
    {
        indicators::IndicatorSpec spec;
        std::shared_ptr<const std::vector<indicators::IndicatorValue>>
            completedValues;
        indicators::IndicatorValue liveValue;
        bool hasLiveValue = false;
        std::size_t retainedBytes = 0;
    };

    enum class IndicatorModuleState
    {
        Empty,
        Ready,
        Error
    };

    struct IndicatorModuleSnapshot final
    {
        IndicatorModuleState state = IndicatorModuleState::Empty;
        FeatureLevel level = FeatureLevel::Off;
        std::string symbol;
        std::uint64_t sourceRevision = 0;
        std::uint64_t completedRevision = 0;
        std::uint64_t calculationRevision = 0;
        std::vector<IndicatorSeriesSnapshot> series;
        FeatureMetrics metrics;
        std::string error;
    };

    class IndicatorModule final
    {
    public:
        IndicatorModule();
        IndicatorModule(const IndicatorModule&) = delete;
        IndicatorModule& operator=(const IndicatorModule&) = delete;

        bool SetLevel(FeatureLevel level, std::string& error);
        FeatureLevel Level() const noexcept;

        bool Configure(
            const std::vector<indicators::IndicatorSpec>& specs,
            std::string& error);

        bool Update(
            const IndicatorMarketSource& source,
            std::string& error);

        IndicatorModuleSnapshot Snapshot() const;

        static const char* StateName(IndicatorModuleState state) noexcept;

    private:
        struct Runtime final
        {
            indicators::IndicatorSpec spec;
            indicators::IndicatorInstance instance;
            std::shared_ptr<const std::vector<indicators::IndicatorValue>>
                completedValues;
            indicators::IndicatorValue liveValue;
            bool hasLiveValue = false;
        };

        bool BuildRuntimesLocked(std::string& error);
        void ClearCalculatedStateLocked() noexcept;
        std::size_t EstimateRetainedBytesLocked() const noexcept;

        mutable std::mutex mutex_;
        FeatureLevel level_ = FeatureLevel::Off;
        IndicatorModuleState state_ = IndicatorModuleState::Empty;
        indicators::IndicatorRegistry registry_;
        std::vector<indicators::IndicatorSpec> specs_;
        std::vector<Runtime> runtimes_;
        std::string symbol_;
        std::uint64_t sourceRevision_ = 0;
        std::uint64_t completedRevision_ = 0;
        std::uint64_t calculationRevision_ = 0;
        FeatureMetrics metrics_;
        std::string error_;
    };
}
