#include "chart_workspace_module.h"

#include "../render/market_chart_builder.h"

#include <utility>

namespace trading::app
{
    namespace
    {
        bool IsVisibleLevel(FeatureLevel level) noexcept
        {
            return
                level == FeatureLevel::Visible ||
                level == FeatureLevel::Active;
        }

        std::size_t DynamicStringBytes(const std::string& value) noexcept
        {
            return value.empty() ? 0 : value.capacity();
        }
    }

    ChartWorkspaceModule::ChartWorkspaceModule()
        : completedVolume_(
            std::make_shared<const std::vector<render::HistogramPoint>>())
    {
    }

    bool ChartWorkspaceModule::SetLevel(
        FeatureLevel level,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;

        if (level == FeatureLevel::Off) {
            document_.reset();
            completedVolume_ =
                std::make_shared<const std::vector<render::HistogramPoint>>();
            state_ = ChartWorkspaceState::Empty;
            sourceRevision_ = 0;
            completedRevision_ = 0;
            documentRevision_ = 0;
            sourceBarCount_ = 0;
            error_.clear();
        }

        error.clear();
        return true;
    }

    FeatureLevel ChartWorkspaceModule::Level() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return level_;
    }

    bool ChartWorkspaceModule::UpdateMarketChart(
        const std::string& workspaceId,
        const std::string& title,
        const std::string& seriesId,
        const ChartMarketSource& source,
        std::string& error)
    {
        if (workspaceId.empty()) {
            error = "chart workspace id is empty";
            return false;
        }
        if (seriesId.empty()) {
            error = "chart series id is empty";
            return false;
        }
        if (source.barCount == 0 || !source.hasLiveBar) {
            error = "chart source bars are empty";
            return false;
        }
        if (!source.completedBars) {
            error = "chart completed-bar history is missing";
            return false;
        }

        std::uint64_t nextDocumentRevision = 0;
        std::shared_ptr<const std::vector<render::HistogramPoint>>
            completedVolume;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!IsVisibleLevel(level_)) {
                error =
                    level_ == FeatureLevel::Off
                        ? "chart workspace is Off"
                        : "chart workspace is Standby";
                return false;
            }
            if (
                state_ == ChartWorkspaceState::Ready &&
                sourceRevision_ == source.revision &&
                document_ != nullptr)
            {
                error.clear();
                return true;
            }

            nextDocumentRevision = documentRevision_ + 1;
            if (
                completedRevision_ == source.completedRevision &&
                completedVolume_)
            {
                completedVolume = completedVolume_;
            }
        }

        if (!completedVolume) {
            completedVolume = BuildCompletedVolume(source.completedBars);
        }

        render::MarketChartSource renderSource;
        renderSource.completedBars = source.completedBars;
        renderSource.liveBar = source.liveBar;
        renderSource.hasLiveBar = source.hasLiveBar;
        renderSource.completedVolume = completedVolume;

        render::RenderDocument candidate =
            render::BuildMarketChartDocument(
                workspaceId,
                title,
                seriesId,
                renderSource,
                nextDocumentRevision);

        std::string validationError;
        if (!render::ValidateRenderDocument(candidate, validationError)) {
            SetError("render document validation failed: " + validationError);
            error = validationError;
            return false;
        }

        std::shared_ptr<const render::RenderDocument> immutable =
            std::make_shared<const render::RenderDocument>(
                std::move(candidate));

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!IsVisibleLevel(level_)) {
                error = "chart workspace level changed while building";
                return false;
            }
            if (
                state_ == ChartWorkspaceState::Ready &&
                sourceRevision_ == source.revision &&
                document_ != nullptr)
            {
                error.clear();
                return true;
            }

            sourceRevision_ = source.revision;
            completedRevision_ = source.completedRevision;
            documentRevision_ = immutable->revision;
            sourceBarCount_ = source.barCount;
            completedVolume_ = std::move(completedVolume);
            document_ = std::move(immutable);
            error_.clear();
            state_ = ChartWorkspaceState::Ready;
        }

        error.clear();
        return true;
    }

    void ChartWorkspaceModule::SetError(const std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = error;
        state_ = ChartWorkspaceState::Error;
    }

    ChartWorkspaceSnapshot ChartWorkspaceModule::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ChartWorkspaceSnapshot result;
        result.state = state_;
        result.level = level_;
        result.sourceRevision = sourceRevision_;
        result.completedRevision = completedRevision_;
        result.documentRevision = documentRevision_;
        result.sourceBarCount = sourceBarCount_;
        result.error = error_;
        result.document = document_;
        if (document_ != nullptr) {
            result.paneCount = document_->panes.size();
            result.seriesCount = CountSeries(*document_);
            result.retainedBytes = EstimateRetainedBytes(*document_);
        }
        return result;
    }

    bool ChartWorkspaceModule::NeedsUpdate(
        std::uint64_t sourceRevision) const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return
            IsVisibleLevel(level_) &&
            (
                document_ == nullptr ||
                state_ != ChartWorkspaceState::Ready ||
                sourceRevision_ != sourceRevision);
    }

    const char* ChartWorkspaceModule::StateName(
        ChartWorkspaceState state) noexcept
    {
        switch (state) {
        case ChartWorkspaceState::Ready: return "Ready";
        case ChartWorkspaceState::Error: return "Error";
        default: return "Empty";
        }
    }

    std::size_t ChartWorkspaceModule::CountSeries(
        const render::RenderDocument& document) noexcept
    {
        std::size_t result = 0;
        for (const render::Pane& pane : document.panes) {
            result += pane.candles.size();
            result += pane.lines.size();
            result += pane.histograms.size();
            result += pane.markers.size();
            result += pane.referenceLines.size();
            result += pane.annotations.size();
        }
        return result;
    }

    std::size_t ChartWorkspaceModule::EstimateRetainedBytes(
        const render::RenderDocument& document) noexcept
    {
        std::size_t result =
            DynamicStringBytes(document.workspaceId) +
            DynamicStringBytes(document.title) +
            document.panes.capacity() * sizeof(render::Pane);

        for (const render::Pane& pane : document.panes) {
            result += DynamicStringBytes(pane.id);
            result += DynamicStringBytes(pane.title);
            result += pane.candles.capacity() * sizeof(render::CandleSeries);
            result += pane.lines.capacity() * sizeof(render::LineSeries);
            result += pane.histograms.capacity() * sizeof(render::HistogramSeries);
            result += pane.markers.capacity() * sizeof(render::MarkerSeries);
            result += pane.referenceLines.capacity() * sizeof(render::ReferenceLine);
            result += pane.annotations.capacity() * sizeof(render::TextAnnotation);

            for (const render::CandleSeries& series : pane.candles) {
                result += DynamicStringBytes(series.id);
                result += DynamicStringBytes(series.label);
                result += series.bars.RetainedBytes();
            }
            for (const render::LineSeries& series : pane.lines) {
                result += DynamicStringBytes(series.id);
                result += DynamicStringBytes(series.label);
                result += series.points.capacity() * sizeof(render::LinePoint);
            }
            for (const render::HistogramSeries& series : pane.histograms) {
                result += DynamicStringBytes(series.id);
                result += DynamicStringBytes(series.label);
                result += series.points.RetainedBytes();
            }
            for (const render::MarkerSeries& series : pane.markers) {
                result += DynamicStringBytes(series.id);
                result += DynamicStringBytes(series.label);
                result += series.points.capacity() * sizeof(render::MarkerPoint);
            }
        }
        return result;
    }

    std::shared_ptr<const std::vector<render::HistogramPoint>>
    ChartWorkspaceModule::BuildCompletedVolume(
        const std::shared_ptr<const std::vector<Bar>>& completedBars)
    {
        auto points =
            std::make_shared<std::vector<render::HistogramPoint>>();
        if (completedBars) {
            points->reserve(completedBars->size());
            for (const Bar& bar : *completedBars) {
                render::HistogramPoint point;
                point.timestampMs = bar.closeTimestampMs;
                point.value = static_cast<double>(bar.volume);
                point.positive = bar.close >= bar.open;
                points->push_back(point);
            }
        }
        return points;
    }
}
