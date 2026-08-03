#pragma once

#include "../core/market_types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace trading::render
{
    template <typename T>
    class SharedTailSeries final
    {
    public:
        class ConstIterator final
        {
        public:
            ConstIterator(
                const SharedTailSeries* owner,
                std::size_t index) noexcept
                : owner_(owner), index_(index)
            {
            }

            const T& operator*() const noexcept
            {
                return owner_->At(index_);
            }

            const T* operator->() const noexcept
            {
                return &owner_->At(index_);
            }

            ConstIterator& operator++() noexcept
            {
                ++index_;
                return *this;
            }

            bool operator!=(const ConstIterator& other) const noexcept
            {
                return owner_ != other.owner_ || index_ != other.index_;
            }

        private:
            const SharedTailSeries* owner_ = nullptr;
            std::size_t index_ = 0;
        };

        SharedTailSeries() = default;

        SharedTailSeries& operator=(const std::vector<T>& values)
        {
            sharedPrefix_.reset();
            owned_ = values;
            hasLiveTail_ = false;
            return *this;
        }

        SharedTailSeries& operator=(std::vector<T>&& values) noexcept
        {
            sharedPrefix_.reset();
            owned_ = std::move(values);
            hasLiveTail_ = false;
            return *this;
        }

        void SetShared(
            std::shared_ptr<const std::vector<T>> completed,
            const T* liveTail = nullptr)
        {
            sharedPrefix_ = std::move(completed);
            owned_.clear();
            owned_.shrink_to_fit();
            hasLiveTail_ = liveTail != nullptr;
            if (liveTail != nullptr) liveTail_ = *liveTail;
        }

        void reserve(std::size_t capacity)
        {
            EnsureOwned();
            owned_.reserve(capacity);
        }

        void push_back(const T& value)
        {
            EnsureOwned();
            owned_.push_back(value);
        }

        void push_back(T&& value)
        {
            EnsureOwned();
            owned_.push_back(std::move(value));
        }

        void clear()
        {
            sharedPrefix_.reset();
            owned_.clear();
            hasLiveTail_ = false;
        }

        std::size_t size() const noexcept
        {
            return SharedSize() + owned_.size() + (hasLiveTail_ ? 1U : 0U);
        }

        bool empty() const noexcept
        {
            return size() == 0;
        }

        std::size_t capacity() const noexcept
        {
            return
                (sharedPrefix_ ? sharedPrefix_->capacity() : 0U) +
                owned_.capacity() +
                (hasLiveTail_ ? 1U : 0U);
        }

        std::size_t RetainedBytes() const noexcept
        {
            return capacity() * sizeof(T);
        }

        const T& front() const noexcept
        {
            return At(0);
        }

        const T& back() const noexcept
        {
            return At(size() - 1U);
        }

        ConstIterator begin() const noexcept
        {
            return ConstIterator(this, 0);
        }

        ConstIterator end() const noexcept
        {
            return ConstIterator(this, size());
        }

        std::shared_ptr<const std::vector<T>> SharedPrefix() const noexcept
        {
            return sharedPrefix_;
        }

        bool HasLiveTail() const noexcept
        {
            return hasLiveTail_;
        }

        const T& LiveTail() const noexcept
        {
            return liveTail_;
        }

    private:
        std::size_t SharedSize() const noexcept
        {
            return sharedPrefix_ ? sharedPrefix_->size() : 0U;
        }

        const T& At(std::size_t index) const noexcept
        {
            const std::size_t sharedSize = SharedSize();
            if (index < sharedSize) {
                return (*sharedPrefix_)[index];
            }

            index -= sharedSize;
            if (index < owned_.size()) {
                return owned_[index];
            }

            return liveTail_;
        }

        void EnsureOwned()
        {
            if (!sharedPrefix_ && !hasLiveTail_) return;

            std::vector<T> materialized;
            materialized.reserve(size());
            for (const T& value : *this) {
                materialized.push_back(value);
            }

            sharedPrefix_.reset();
            owned_ = std::move(materialized);
            hasLiveTail_ = false;
        }

        std::shared_ptr<const std::vector<T>> sharedPrefix_;
        std::vector<T> owned_;
        T liveTail_{};
        bool hasLiveTail_ = false;
    };

    struct ColorRgba final
    {
        std::uint8_t red = 255;
        std::uint8_t green = 255;
        std::uint8_t blue = 255;
        std::uint8_t alpha = 255;
    };

    struct LinePoint final
    {
        EpochMillis timestampMs = 0;
        double value = 0.0;
    };

    struct HistogramPoint final
    {
        EpochMillis timestampMs = 0;
        double value = 0.0;
        bool positive = true;
    };

    enum class MarkerShape
    {
        Circle,
        TriangleUp,
        TriangleDown,
        Diamond,
        Square
    };

    struct MarkerPoint final
    {
        EpochMillis timestampMs = 0;
        double value = 0.0;
        MarkerShape shape = MarkerShape::Circle;
        std::string label;
        ColorRgba color;
    };

    struct CandleSeries final
    {
        std::string id;
        std::string label;
        SharedTailSeries<Bar> bars;
        ColorRgba upColor{ 235, 72, 72, 255 };
        ColorRgba downColor{ 70, 130, 240, 255 };
        bool visible = true;
    };

    struct LineSeries final
    {
        std::string id;
        std::string label;
        std::vector<LinePoint> points;
        ColorRgba color;
        float width = 1.0f;
        bool visible = true;
    };

    struct HistogramSeries final
    {
        std::string id;
        std::string label;
        SharedTailSeries<HistogramPoint> points;
        ColorRgba positiveColor{ 235, 72, 72, 255 };
        ColorRgba negativeColor{ 70, 130, 240, 255 };
        bool visible = true;
    };

    struct MarkerSeries final
    {
        std::string id;
        std::string label;
        std::vector<MarkerPoint> points;
        bool visible = true;
    };

    struct ReferenceLine final
    {
        std::string id;
        std::string label;
        double value = 0.0;
        ColorRgba color;
        float width = 1.0f;
        bool visible = true;
    };

    struct TextAnnotation final
    {
        std::string id;
        EpochMillis timestampMs = 0;
        double value = 0.0;
        std::string text;
        ColorRgba color;
        bool visible = true;
    };

    enum class PaneValueScale
    {
        Auto,
        Fixed,
        Symmetric
    };

    struct Pane final
    {
        std::string id;
        std::string title;
        float heightWeight = 1.0f;
        PaneValueScale valueScale = PaneValueScale::Auto;
        double fixedMinimum = 0.0;
        double fixedMaximum = 0.0;
        std::vector<CandleSeries> candles;
        std::vector<LineSeries> lines;
        std::vector<HistogramSeries> histograms;
        std::vector<MarkerSeries> markers;
        std::vector<ReferenceLine> referenceLines;
        std::vector<TextAnnotation> annotations;
    };

    struct InteractionState final
    {
        EpochMillis visibleStartMs = 0;
        EpochMillis visibleEndMs = 0;
        EpochMillis crosshairTimestampMs = 0;
        double crosshairValue = 0.0;
        bool autoScroll = true;
        bool crosshairVisible = false;
    };

    struct RenderDocument final
    {
        std::uint64_t revision = 0;
        std::uint64_t structureRevision = 0;
        std::string workspaceId;
        std::string title;
        std::vector<Pane> panes;
        InteractionState interaction;
    };

    bool ValidateRenderDocument(
        const RenderDocument& document,
        std::string& error);
}
