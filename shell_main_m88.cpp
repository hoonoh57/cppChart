#include "imgui.h"
#include "imgui_internal.h"
#include "app/market_data_module.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    using M88Clock = std::chrono::steady_clock;

    struct M88HistoryController final
    {
        std::mutex mutex;
        std::size_t targetBars = 1800U;
        bool active = false;
        bool requestInFlight = false;
        bool applyVisiblePending = false;
        std::string code;
        int minuteUnit = 1;
        std::size_t receivedBars = 0U;
        M88Clock::time_point lastRequestAt{};
    };

    M88HistoryController g_m88History;

    std::size_t M88HistoryTarget()
    {
        std::lock_guard<std::mutex> lock(g_m88History.mutex);
        return g_m88History.targetBars;
    }

    void M88BeginHistoryRequest(
        const std::string& code,
        int minuteUnit)
    {
        std::lock_guard<std::mutex> lock(g_m88History.mutex);
        g_m88History.active = true;
        g_m88History.requestInFlight = true;
        g_m88History.applyVisiblePending = false;
        g_m88History.code = code;
        g_m88History.minuteUnit = minuteUnit;
        g_m88History.receivedBars = 0U;
        g_m88History.lastRequestAt = M88Clock::now();
    }

    void M88StopHistoryRequest()
    {
        std::lock_guard<std::mutex> lock(g_m88History.mutex);
        g_m88History.active = false;
        g_m88History.requestInFlight = false;
    }

    void M88AfterHistoryPage(
        const trading::app::MarketDataSnapshot& snapshot,
        const trading::Continuation& continuation)
    {
        std::lock_guard<std::mutex> lock(g_m88History.mutex);
        if (snapshot.code != g_m88History.code ||
            snapshot.minuteUnit != g_m88History.minuteUnit)
        {
            return;
        }

        g_m88History.requestInFlight = false;
        g_m88History.receivedBars = snapshot.barCount;
        const bool hasMore =
            (continuation.continueYn == "Y" ||
             continuation.continueYn == "y") &&
            !continuation.nextKey.empty();
        if (snapshot.barCount >= g_m88History.targetBars || !hasMore) {
            g_m88History.active = false;
            g_m88History.applyVisiblePending = true;
        }
    }

    void M88SetHistoryTarget(
        std::size_t target,
        const trading::app::MarketDataSnapshot& snapshot)
    {
        target = (std::max)(100U, (std::min)(20000U, target));
        std::lock_guard<std::mutex> lock(g_m88History.mutex);
        g_m88History.targetBars = target;
        if (snapshot.state != trading::app::MarketDataState::Ready ||
            snapshot.code.empty())
        {
            return;
        }

        const bool hasMore =
            (snapshot.continuation.continueYn == "Y" ||
             snapshot.continuation.continueYn == "y") &&
            !snapshot.continuation.nextKey.empty();
        g_m88History.code = snapshot.code;
        g_m88History.minuteUnit = snapshot.minuteUnit;
        g_m88History.receivedBars = snapshot.barCount;
        if (snapshot.barCount < target && hasMore) {
            g_m88History.active = true;
            g_m88History.requestInFlight = false;
        }
        else {
            g_m88History.active = false;
            g_m88History.applyVisiblePending = true;
        }
    }
}

namespace trading::app
{
    class M88MarketDataModule final
    {
    public:
        bool SetLevel(FeatureLevel level, std::string& error)
        {
            const bool changed = inner_.SetLevel(level, error);
            if (changed && level == FeatureLevel::Off) {
                std::lock_guard<std::mutex> lock(historyMutex_);
                historyBars_.clear();
                requestCode_.clear();
                requestMinuteUnit_ = 1;
                M88StopHistoryRequest();
            }
            return changed;
        }

        FeatureLevel Level() const noexcept
        {
            return inner_.Level();
        }

        bool BeginRequest(
            const std::string& code,
            int minuteUnit,
            std::string& error)
        {
            if (!inner_.BeginRequest(code, minuteUnit, error)) return false;
            {
                std::lock_guard<std::mutex> lock(historyMutex_);
                historyBars_.clear();
                requestCode_ = code;
                requestMinuteUnit_ = minuteUnit;
            }
            M88BeginHistoryRequest(code, minuteUnit);
            return true;
        }

        MarketDataApplyResult ApplyMinuteBars(
            const MinuteBarsPage& page,
            const Continuation& continuation)
        {
            if (!page.result.ok || page.bars.empty()) {
                M88StopHistoryRequest();
                return inner_.ApplyMinuteBars(page, continuation);
            }

            MinuteBarsPage merged = page;
            {
                std::lock_guard<std::mutex> lock(historyMutex_);
                if (!requestCode_.empty() &&
                    (page.code != requestCode_ ||
                     page.minuteUnit != requestMinuteUnit_))
                {
                    return inner_.ApplyMinuteBars(page, continuation);
                }

                for (const Bar& bar : page.bars) {
                    if (bar.closeTimestampMs > 0) {
                        historyBars_.try_emplace(bar.closeTimestampMs, bar);
                    }
                }

                const MarketDataSeriesSnapshot current = inner_.SeriesSnapshot();
                if (current.code == page.code &&
                    current.minuteUnit == page.minuteUnit)
                {
                    if (current.completedBars) {
                        for (const Bar& bar : *current.completedBars) {
                            historyBars_[bar.closeTimestampMs] = bar;
                        }
                    }
                    if (current.hasLiveBar) {
                        historyBars_[current.liveBar.closeTimestampMs] =
                            current.liveBar;
                    }
                }

                const std::size_t target = M88HistoryTarget();
                while (historyBars_.size() > target) {
                    historyBars_.erase(historyBars_.begin());
                }

                merged.bars.clear();
                merged.bars.reserve(historyBars_.size());
                for (const auto& item : historyBars_) {
                    merged.bars.push_back(item.second);
                }
            }

            const MarketDataApplyResult applied =
                inner_.ApplyMinuteBars(merged, continuation);
            if (applied.applied) {
                M88AfterHistoryPage(inner_.Snapshot(), continuation);
            }
            else if (!applied.stale) {
                M88StopHistoryRequest();
            }
            return applied;
        }

        MarketDataApplyResult ApplyStockTradeTick(const StockTradeTick& tick)
        {
            return inner_.ApplyStockTradeTick(tick);
        }

        void SetError(const std::string& error)
        {
            M88StopHistoryRequest();
            inner_.SetError(error);
        }

        void SetStockTradeSubscriptionRequested(bool requested) noexcept
        {
            inner_.SetStockTradeSubscriptionRequested(requested);
        }

        bool TryGetLatestQuote(
            std::string& code,
            PriceWon& priceWon) const
        {
            return inner_.TryGetLatestQuote(code, priceWon);
        }

        MarketDataSnapshot Snapshot() const
        {
            return inner_.Snapshot();
        }

        MarketDataSeriesSnapshot SeriesSnapshot() const
        {
            return inner_.SeriesSnapshot();
        }

        std::vector<Bar> CopyVisibleBars(std::size_t maximumCount) const
        {
            return inner_.CopyVisibleBars(maximumCount);
        }

        static const char* StateName(MarketDataState state) noexcept
        {
            return MarketDataModule::StateName(state);
        }

    private:
        MarketDataModule inner_;
        mutable std::mutex historyMutex_;
        std::map<EpochMillis, Bar> historyBars_;
        std::string requestCode_;
        int requestMinuteUnit_ = 1;
    };
}

namespace ImGui
{
    bool M88Button(
        const char* label,
        const ImVec2& size = ImVec2(0.0f, 0.0f));

    bool M88InputInt(
        const char* label,
        int* value,
        int step = 1,
        int stepFast = 100,
        ImGuiInputTextFlags flags = 0);

    void M88TextDisabled(const char* format, ...);
}

#define MarketDataModule M88MarketDataModule
#define Button M88Button
#define InputInt M88InputInt
#define TextDisabled M88TextDisabled
#include "shell_main_m87.cpp"
#undef TextDisabled
#undef InputInt
#undef Button
#undef MarketDataModule

namespace
{
    bool g_m88Pumping = false;

    bool M88IsMarketPanelDuplicate(const char* label)
    {
        const ImGuiWindow* window = ImGui::GetCurrentWindowRead();
        if (window == nullptr || std::strcmp(window->Name, "실제 시세") != 0) {
            return false;
        }
        static const char* duplicates[] = {
            "1분", "3분", "5분", "10분", "15분", "30분", "60분",
            "재조회"};
        for (const char* duplicate : duplicates) {
            if (std::strcmp(label, duplicate) == 0) return true;
        }
        return false;
    }

    void M88PumpHistoryAutoLoad()
    {
        if (g_m88Pumping) return;
        g_m88Pumping = true;

        const trading::app::MarketDataSnapshot snapshot =
            g_marketDataModule.Snapshot();

        bool applyVisible = false;
        bool requestNext = false;
        trading::Continuation continuation;
        std::string code;
        int minuteUnit = 1;
        std::size_t target = 0U;
        {
            std::lock_guard<std::mutex> lock(g_m88History.mutex);
            target = g_m88History.targetBars;
            if (snapshot.state == trading::app::MarketDataState::Error) {
                g_m88History.active = false;
                g_m88History.requestInFlight = false;
            }

            if (g_m88History.applyVisiblePending &&
                !g_mainRenderSurface.timeAxis.Empty() &&
                g_mainRenderSurface.timeAxis.Size() == snapshot.barCount)
            {
                g_m88History.applyVisiblePending = false;
                applyVisible = true;
            }

            const bool matching =
                snapshot.code == g_m88History.code &&
                snapshot.minuteUnit == g_m88History.minuteUnit;
            const bool hasMore =
                (snapshot.continuation.continueYn == "Y" ||
                 snapshot.continuation.continueYn == "y") &&
                !snapshot.continuation.nextKey.empty();
            const auto elapsed = M88Clock::now() - g_m88History.lastRequestAt;
            if (g_m88History.active && matching &&
                !g_m88History.requestInFlight &&
                snapshot.barCount < target && hasMore &&
                elapsed >= std::chrono::milliseconds(1100))
            {
                g_m88History.requestInFlight = true;
                g_m88History.lastRequestAt = M88Clock::now();
                continuation = snapshot.continuation;
                code = snapshot.code;
                minuteUnit = snapshot.minuteUnit;
                requestNext = true;
            }
        }

        if (applyVisible) {
            ApplyVisibleBarCount(true);
        }

        if (requestNext) {
            std::string error;
            if (!g_runtimeRunner ||
                !g_runtimeRunner->RequestStockMinuteBars(
                    code,
                    minuteUnit,
                    continuation,
                    error))
            {
                M88StopHistoryRequest();
                g_log.Add(
                    "FAULT",
                    "자동 과거데이터 요청 실패: %s",
                    error.c_str());
            }
            else {
                g_log.Add(
                    "DATA",
                    "자동 과거데이터 요청: %s %d분 %zu/%zu봉",
                    code.c_str(),
                    minuteUnit,
                    snapshot.barCount,
                    target);
            }
        }

        g_m88Pumping = false;
    }
}

bool ImGui::M88Button(const char* label, const ImVec2& size)
{
    M88PumpHistoryAutoLoad();

    if (label != nullptr && M88IsMarketPanelDuplicate(label)) {
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        return false;
    }

    bool autoActive = false;
    if (label != nullptr && std::strcmp(label, "추가데이터") == 0) {
        std::lock_guard<std::mutex> lock(g_m88History.mutex);
        autoActive = g_m88History.active || g_m88History.requestInFlight;
    }
    if (autoActive) ImGui::BeginDisabled();
    const bool pressed = ImGui::Button(label, size);
    if (autoActive) ImGui::EndDisabled();
    return autoActive ? false : pressed;
}

bool ImGui::M88InputInt(
    const char* label,
    int* value,
    int step,
    int stepFast,
    ImGuiInputTextFlags flags)
{
    const bool changed = ImGui::InputInt(
        label,
        value,
        step,
        stepFast,
        flags);

    if (label != nullptr &&
        std::strcmp(label, "##visible_bars_m87") == 0)
    {
        static int targetBars = 1800;
        {
            std::lock_guard<std::mutex> lock(g_m88History.mutex);
            targetBars = static_cast<int>(g_m88History.targetBars);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| 조회");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(68.0f);
        if (ImGui::InputInt(
                "##target_history_bars_m88",
                &targetBars,
                0,
                0))
        {
            targetBars = (std::max)(100, (std::min)(20000, targetBars));
            const trading::app::MarketDataSnapshot snapshot =
                g_marketDataModule.Snapshot();
            M88SetHistoryTarget(
                static_cast<std::size_t>(targetBars),
                snapshot);
        }
    }

    return changed;
}

void ImGui::M88TextDisabled(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    ImGui::TextDisabledV(format, arguments);
    va_end(arguments);

    if (format != nullptr &&
        std::strcmp(format, "수신 %zu봉%s") == 0)
    {
        std::size_t target = 0U;
        bool loading = false;
        {
            std::lock_guard<std::mutex> lock(g_m88History.mutex);
            target = g_m88History.targetBars;
            loading = g_m88History.active || g_m88History.requestInFlight;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            loading ? "/ 목표 %zu봉 자동조회 중" : "/ 목표 %zu봉",
            target);
    }
}
