#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace trading::app
{
    enum class FeatureLevel
    {
        Off = 0,
        Standby = 1,
        Visible = 2,
        Active = 3
    };

    struct FeatureMetrics final
    {
        std::uint64_t eventCount = 0;
        std::uint64_t mergedEventCount = 0;
        std::uint64_t droppedEventCount = 0;
        std::uint64_t lastProcessingMicros = 0;
        std::uint64_t maxProcessingMicros = 0;
        std::size_t queueDepth = 0;
        std::size_t retainedBytes = 0;
        std::size_t symbolCount = 0;
        std::size_t renderSeriesCount = 0;
    };

    struct FeatureSnapshot final
    {
        std::string id;
        std::string displayName;
        std::vector<std::string> dependencies;
        FeatureLevel level = FeatureLevel::Off;
        bool ready = false;
        std::string lastError;
        FeatureMetrics metrics;
    };

    class FeatureRegistry final
    {
    public:
        FeatureRegistry() = default;
        FeatureRegistry(const FeatureRegistry&) = delete;
        FeatureRegistry& operator=(const FeatureRegistry&) = delete;

        bool Register(
            const std::string& id,
            const std::string& displayName,
            FeatureLevel initialLevel,
            std::vector<std::string> dependencies,
            std::string& error);

        bool SetLevel(
            const std::string& id,
            FeatureLevel level,
            std::string& error);

        bool SetHealth(
            const std::string& id,
            bool ready,
            const std::string& lastError,
            std::string& error);

        bool RecordWork(
            const std::string& id,
            std::uint64_t elapsedMicros,
            std::size_t queueDepth,
            std::size_t retainedBytes,
            std::size_t symbolCount,
            std::size_t renderSeriesCount,
            std::uint64_t mergedEvents,
            std::uint64_t droppedEvents,
            std::string& error);

        bool Get(
            const std::string& id,
            FeatureSnapshot& snapshot) const;

        std::vector<FeatureSnapshot> SnapshotAll() const;

        static const char* LevelName(FeatureLevel level) noexcept;

    private:
        static bool IsValidId(const std::string& id) noexcept;
        bool DependenciesAllowLocked(
            const FeatureSnapshot& feature,
            FeatureLevel requested,
            std::string& error) const;

        mutable std::mutex mutex_;
        std::map<std::string, FeatureSnapshot> features_;
    };
}
