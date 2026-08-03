#pragma once

#include "../render/render_document.h"
#include "feature_registry.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace trading::app
{
    enum class ChartWorkspaceState
    {
        Empty,
        Ready,
        Error
    };

    struct ChartWorkspaceSnapshot final
    {
        ChartWorkspaceState state = ChartWorkspaceState::Empty;
        FeatureLevel level = FeatureLevel::Off;
        std::uint64_t sourceRevision = 0;
        std::uint64_t documentRevision = 0;
        std::size_t visibleBarLimit = 0;
        std::size_t paneCount = 0;
        std::size_t seriesCount = 0;
        std::size_t retainedBytes = 0;
        std::string error;
        std::shared_ptr<const render::RenderDocument> document;
    };

    class ChartWorkspaceModule final
    {
    public:
        ChartWorkspaceModule();
        ChartWorkspaceModule(const ChartWorkspaceModule&) = delete;
        ChartWorkspaceModule& operator=(const ChartWorkspaceModule&) = delete;

        bool SetLevel(
            FeatureLevel level,
            std::string& error);

        FeatureLevel Level() const noexcept;

        bool UpdateMarketChart(
            const std::string& workspaceId,
            const std::string& title,
            const std::string& seriesId,
            const std::vector<Bar>& visibleBars,
            std::uint64_t sourceRevision,
            std::size_t visibleBarLimit,
            std::string& error);

        void SetError(const std::string& error);

        ChartWorkspaceSnapshot Snapshot() const;

        bool NeedsUpdate(
            std::uint64_t sourceRevision,
            std::size_t visibleBarLimit) const noexcept;

        static const char* StateName(
            ChartWorkspaceState state) noexcept;

    private:
        static std::size_t CountSeries(
            const render::RenderDocument& document) noexcept;

        static std::size_t EstimateRetainedBytes(
            const render::RenderDocument& document) noexcept;

        mutable std::mutex mutex_;
        FeatureLevel level_ = FeatureLevel::Visible;
        ChartWorkspaceState state_ = ChartWorkspaceState::Empty;
        std::uint64_t sourceRevision_ = 0;
        std::uint64_t documentRevision_ = 0;
        std::size_t visibleBarLimit_ = 0;
        std::string error_;
        std::shared_ptr<const render::RenderDocument> document_;
    };
}
