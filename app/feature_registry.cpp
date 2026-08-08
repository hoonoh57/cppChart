#include "feature_registry.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace trading::app
{
    bool FeatureRegistry::Register(
        const std::string& id,
        const std::string& displayName,
        FeatureLevel initialLevel,
        std::vector<std::string> dependencies,
        std::string& error)
    {
        if (!IsValidId(id)) {
            error = "feature id is invalid";
            return false;
        }
        if (displayName.empty()) {
            error = "feature display name is empty";
            return false;
        }

        std::sort(dependencies.begin(), dependencies.end());
        dependencies.erase(
            std::unique(dependencies.begin(), dependencies.end()),
            dependencies.end());

        if (std::find(dependencies.begin(), dependencies.end(), id) !=
            dependencies.end())
        {
            error = "feature cannot depend on itself";
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (features_.find(id) != features_.end()) {
            error = "feature is already registered";
            return false;
        }

        for (const std::string& dependency : dependencies) {
            if (features_.find(dependency) == features_.end()) {
                error = "feature dependency is not registered: " + dependency;
                return false;
            }
        }

        FeatureSnapshot feature;
        feature.id = id;
        feature.displayName = displayName;
        feature.dependencies = std::move(dependencies);
        feature.level = initialLevel;

        if (!DependenciesAllowLocked(feature, initialLevel, error)) {
            return false;
        }

        features_.emplace(id, std::move(feature));
        error.clear();
        return true;
    }

    bool FeatureRegistry::SetLevel(
        const std::string& id,
        FeatureLevel level,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = features_.find(id);
        if (found == features_.end()) {
            error = "feature is not registered: " + id;
            return false;
        }

        if (!DependenciesAllowLocked(found->second, level, error)) {
            return false;
        }

        if (level == FeatureLevel::Off) {
            for (const auto& entry : features_) {
                if (entry.first == id || entry.second.level == FeatureLevel::Off) {
                    continue;
                }
                if (std::find(
                        entry.second.dependencies.begin(),
                        entry.second.dependencies.end(),
                        id) != entry.second.dependencies.end())
                {
                    error =
                        "feature is required by enabled feature: " +
                        entry.first;
                    return false;
                }
            }
        }

        found->second.level = level;
        error.clear();
        return true;
    }

    bool FeatureRegistry::SetHealth(
        const std::string& id,
        bool ready,
        const std::string& lastError,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = features_.find(id);
        if (found == features_.end()) {
            error = "feature is not registered: " + id;
            return false;
        }

        found->second.ready = ready;
        found->second.lastError = lastError;
        error.clear();
        return true;
    }

    bool FeatureRegistry::RecordWork(
        const std::string& id,
        std::uint64_t elapsedMicros,
        std::size_t queueDepth,
        std::size_t retainedBytes,
        std::size_t symbolCount,
        std::size_t renderSeriesCount,
        std::uint64_t mergedEvents,
        std::uint64_t droppedEvents,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = features_.find(id);
        if (found == features_.end()) {
            error = "feature is not registered: " + id;
            return false;
        }

        FeatureMetrics& metrics = found->second.metrics;
        ++metrics.eventCount;
        metrics.mergedEventCount += mergedEvents;
        metrics.droppedEventCount += droppedEvents;
        metrics.lastProcessingMicros = elapsedMicros;
        metrics.maxProcessingMicros =
            (std::max)(metrics.maxProcessingMicros, elapsedMicros);
        metrics.queueDepth = queueDepth;
        metrics.retainedBytes = retainedBytes;
        metrics.symbolCount = symbolCount;
        metrics.renderSeriesCount = renderSeriesCount;
        error.clear();
        return true;
    }

    bool FeatureRegistry::Get(
        const std::string& id,
        FeatureSnapshot& snapshot) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = features_.find(id);
        if (found == features_.end()) return false;
        snapshot = found->second;
        return true;
    }

    std::vector<FeatureSnapshot> FeatureRegistry::SnapshotAll() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<FeatureSnapshot> result;
        result.reserve(features_.size());
        for (const auto& entry : features_) {
            result.push_back(entry.second);
        }
        return result;
    }

    const char* FeatureRegistry::LevelName(FeatureLevel level) noexcept
    {
        switch (level) {
        case FeatureLevel::Off: return "Off";
        case FeatureLevel::Standby: return "Standby";
        case FeatureLevel::Visible: return "Visible";
        case FeatureLevel::Active: return "Active";
        default: return "Unknown";
        }
    }

    bool FeatureRegistry::IsValidId(const std::string& id) noexcept
    {
        if (id.empty()) return false;
        for (char ch : id) {
            const unsigned char value = static_cast<unsigned char>(ch);
            if (
                std::isalnum(value) == 0 &&
                ch != '.' &&
                ch != '-' &&
                ch != '_')
            {
                return false;
            }
        }
        return true;
    }

    bool FeatureRegistry::DependenciesAllowLocked(
        const FeatureSnapshot& feature,
        FeatureLevel requested,
        std::string& error) const
    {
        if (requested == FeatureLevel::Off) {
            error.clear();
            return true;
        }

        for (const std::string& dependency : feature.dependencies) {
            const auto found = features_.find(dependency);
            if (found == features_.end()) {
                error = "feature dependency is not registered: " + dependency;
                return false;
            }
            if (found->second.level == FeatureLevel::Off) {
                error = "feature dependency is Off: " + dependency;
                return false;
            }
        }

        error.clear();
        return true;
    }
}
