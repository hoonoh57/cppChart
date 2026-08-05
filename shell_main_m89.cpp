#include "imgui.h"
#include "imgui_internal.h"
#include "app/market_data_module.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    using M89Clock = std::chrono::steady_clock;

    struct M89HistoryController final
    {
        std::mutex mutex;
        std::size_t targetBars = 1800U;
        std::size_t pageSizeBars = 900U;
        std::size_t downloadedBars = 0U;
        std::size_t eligibleBars = 0U;
        bool active = false;
        bool requestInFlight = false;
        bool onePageOnly = false;
        bool applyVisiblePending = false;
        bool anchorReached = false;
        std::string code;
        int minuteUnit = 1;
        std::string anchorDate;
        std::string resolvedEndDate;
        std::string error;
        M89Clock::time_point lastRequestAt{};
    };

    M89HistoryController g_m89History;

    bool M89FormatDate(
        trading::EpochMillis timestampMs,
        char* buffer,
        std::size_t bufferSize)
    {
        if (timestampMs <= 0 || buffer == nullptr || bufferSize < 11U) {
            return false;
        }
        const std::time_t seconds = static_cast<std::time_t>(timestampMs / 1000);
        std::tm local{};
#if defined(_WIN32)
        if (localtime_s(&local, &seconds) != 0) return false;
#else
        if (localtime_r(&seconds, &local) == nullptr) return false;
#endif
        std::snprintf(
            buffer,
            bufferSize,
            "%04d-%02d-%02d",
            local.tm_year + 1900,
            local.tm_mon + 1,
            local.tm_mday);
        return true;
    }

    bool M89ValidDateText(const std::string& value)
    {
        if (value.empty()) return true;
        if (value.size() != 10U || value[4] != '-' || value[7] != '-') {
            return false;
        }
        for (std::size_t index = 0; index < value.size(); ++index) {
            if (index == 4U || index == 7U) continue;
            if (value[index] < '0' || value[index] > '9') return false;
        }
        return true;
    }

    std::string M89BarDate(const trading::Bar& bar)
    {
        char value[16]{};
        return M89FormatDate(bar.closeTimestampMs, value, sizeof(value))
            ? std::string(value)
            : std::string();
    }

    std::size_t M89TargetBars()
    {
        std::lock_guard<std::mutex> lock(g_m89History.mutex);
        return g_m89History.targetBars;
    }

    void M89SetTargetBars(std::size_t value)
    {
        value = (std::max)(100U, (std::min)(20000U, value));
        std::lock_guard<std::mutex> lock(g_m89History.mutex);
        g_m89History.targetBars = value;
    }

    void M89BeginRequestState(
        const std::string& code,
        int minuteUnit)
    {
        std::lock_guard<std::mutex> lock(g_m89History.mutex);
        g_m89History.code = code;
        g_m89History.minuteUnit = minuteUnit;
        g_m89History.downloadedBars = 0U;
        g_m89History.eligibleBars = 0U;
        g_m89History.active = true;
        g_m89History.requestInFlight = true;
        g_m89History.onePageOnly = false;
        g_m89History.applyVisiblePending = false;
        g_m89History.anchorReached = g_m89History.anchorDate.empty();
        g_m89History.resolvedEndDate.clear();
        g_m89History.error.clear();
        g_m89History.lastRequestAt = M89Clock::now();
    }

    void M89StopRequestState(const std::string& error = {})
    {
        std::lock_guard<std::mutex> lock(g_m89History.mutex);
        g_m89History.active = false;
        g_m89History.requestInFlight = false;
        g_m89History.onePageOnly = false;
        if (!error.empty()) g_m89History.error = error;
    }

    bool M89HistoricalAnchorActive()
    {
        char today[16]{};
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        std::snprintf(
            today,
            sizeof(today),
            "%04d-%02d-%02d",
            local.tm_year + 1900,
            local.tm_mon + 1,
            local.tm_mday);

        std::lock_guard<std::mutex> lock(g_m89History.mutex);
        return !g_m89History.anchorDate.empty() &&
            g_m89History.anchorDate < today;
    }
}

namespace trading::app
{
    class M89MarketDataModule final
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
                M89StopRequestState();
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
            std::string anchor;
            {
                std::lock_guard<std::mutex> lock(g_m89History.mutex);
                anchor = g_m89History.anchorDate;
            }
            if (!M89ValidDateText(anchor)) {
                error = "기준일은 YYYY-MM-DD 형식이어야 합니다.";
                M89StopRequestState(error);
                return false;
            }
            if (!inner_.BeginRequest(code, minuteUnit, error)) return false;
            {
                std::lock_guard<std::mutex> lock(historyMutex_);
                historyBars_.clear();
                requestCode_ = code;
                requestMinuteUnit_ = minuteUnit;
            }
            M89BeginRequestState(code, minuteUnit);
            return true;
        }

        MarketDataApplyResult ApplyMinuteBars(
            const MinuteBarsPage& page,
            const Continuation& continuation)
        {
            if (!page.result.ok || page.bars.empty()) {
                M89StopRequestState();
                return inner_.ApplyMinuteBars(page, continuation);
            }

            MinuteBarsPage merged = page;
            std::size_t rawCount = 0U;
            std::size_t eligibleCount = 0U;
            bool anchorReached = false;
            std::string resolvedEndDate;
            bool noEligibleAtEnd = false;
            {
                std::lock_guard<std::mutex> historyLock(historyMutex_);
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

                std::size_t target = 1800U;
                std::string anchor;
                {
                    std::lock_guard<std::mutex> stateLock(g_m89History.mutex);
                    target = g_m89History.targetBars;
                    anchor = g_m89History.anchorDate;
                    g_m89History.pageSizeBars = (std::max)(
                        g_m89History.pageSizeBars,
                        page.bars.size());
                }

                rawCount = historyBars_.size();
                std::vector<Bar> eligible;
                eligible.reserve(historyBars_.size());
                std::string earliestDate;
                for (const auto& item : historyBars_) {
                    const std::string date = M89BarDate(item.second);
                    if (earliestDate.empty() && !date.empty()) earliestDate = date;
                    if (anchor.empty() || (!date.empty() && date <= anchor)) {
                        eligible.push_back(item.second);
                    }
                }

                anchorReached = anchor.empty() ||
                    (!earliestDate.empty() && earliestDate <= anchor);
                eligibleCount = eligible.size();
                if (!eligible.empty()) {
                    resolvedEndDate = M89BarDate(eligible.back());
                }

                const bool hasMore =
                    (continuation.continueYn == "Y" ||
                     continuation.continueYn == "y") &&
                    !continuation.nextKey.empty();
                noEligibleAtEnd = !hasMore && !anchor.empty() && eligible.empty();

                std::vector<Bar>* source = &eligible;
                std::vector<Bar> fallback;
                if (eligible.empty() && !historyBars_.empty()) {
                    fallback.reserve(historyBars_.size());
                    for (const auto& item : historyBars_) {
                        fallback.push_back(item.second);
                    }
                    source = &fallback;
                }

                const std::size_t keep = (std::min)(target, source->size());
                merged.bars.clear();
                merged.bars.reserve(keep);
                if (keep > 0U) {
                    merged.bars.insert(
                        merged.bars.end(),
                        source->end() - static_cast<std::ptrdiff_t>(keep),
                        source->end());
                }
            }

            if (noEligibleAtEnd || merged.bars.empty()) {
                MarketDataApplyResult failed;
                failed.error = "선택한 기준일까지 실제 분봉을 찾지 못했습니다.";
                inner_.SetError(failed.error);
                M89StopRequestState(failed.error);
                return failed;
            }

            const MarketDataApplyResult applied =
                inner_.ApplyMinuteBars(merged, continuation);
            if (!applied.applied) {
                if (!applied.stale) M89StopRequestState(applied.error);
                return applied;
            }

            const bool hasMore =
                (continuation.continueYn == "Y" ||
                 continuation.continueYn == "y") &&
                !continuation.nextKey.empty();
            {
                std::lock_guard<std::mutex> lock(g_m89History.mutex);
                if (page.code == g_m89History.code &&
                    page.minuteUnit == g_m89History.minuteUnit)
                {
                    g_m89History.requestInFlight = false;
                    g_m89History.downloadedBars = rawCount;
                    g_m89History.eligibleBars = eligibleCount;
                    g_m89History.anchorReached = anchorReached;
                    g_m89History.resolvedEndDate = resolvedEndDate;

                    const bool complete =
                        anchorReached && eligibleCount >= g_m89History.targetBars;
                    if (g_m89History.onePageOnly || complete || !hasMore) {
                        g_m89History.active = false;
                        g_m89History.onePageOnly = false;
                        g_m89History.applyVisiblePending = true;
                    }
                    else {
                        g_m89History.active = true;
                    }
                }
            }
            return applied;
        }

        MarketDataApplyResult ApplyStockTradeTick(const StockTradeTick& tick)
        {
            if (M89HistoricalAnchorActive()) {
                MarketDataApplyResult ignored;
                ignored.stale = true;
                ignored.error = "과거 기준일 차트에서 실시간 체결을 무시했습니다.";
                return ignored;
            }
            return inner_.ApplyStockTradeTick(tick);
        }

        void SetError(const std::string& error)
        {
            M89StopRequestState(error);
            inner_.SetError(error);
        }

        void SetStockTradeSubscriptionRequested(bool requested) noexcept
        {
            inner_.SetStockTradeSubscriptionRequested(requested);
        }

        bool TryGetLatestQuote(std::string& code, PriceWon& priceWon) const
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
    bool M89Button(
        const char* label,
        const ImVec2& size = ImVec2(0.0f, 0.0f));

    bool M89InputInt(
        const char* label,
        int* value,
        int step = 1,
        int stepFast = 100,
        ImGuiInputTextFlags flags = 0);

    void M89TextDisabled(const char* format, ...);
}

#define MarketDataModule M89MarketDataModule
#define Button M89Button
#define InputInt M89InputInt
#define TextDisabled M89TextDisabled
#include "shell_main_m87.cpp"
#undef TextDisabled
#undef InputInt
#undef Button
#undef MarketDataModule

namespace
{
    bool g_m89Pumping = false;

    int M89TimeFrameIndex(const char* label)
    {
        static const char* labels[] = {
            "1분", "3분", "5분", "10분", "15분", "30분", "60분"};
        for (int index = 0; index < IM_ARRAYSIZE(labels); ++index) {
            if (std::strcmp(label, labels[index]) == 0) return index;
        }
        return -1;
    }

    bool M89InMarketPanel()
    {
        const ImGuiWindow* window = ImGui::GetCurrentWindowRead();
        return window != nullptr && std::strcmp(window->Name, "실제 시세") == 0;
    }

    void M89PrepareChartQuery()
    {
        std::lock_guard<std::mutex> lock(g_m89History.mutex);
        g_m89History.anchorDate = g_m85AnchorDate;
        g_m89History.onePageOnly = false;
        g_m89History.error.clear();
    }

    bool M89ShiftDateText(int days)
    {
        if (std::strlen(g_m85AnchorDate) != 10U) return false;
        std::tm date{};
        if (std::sscanf(
                g_m85AnchorDate,
                "%d-%d-%d",
                &date.tm_year,
                &date.tm_mon,
                &date.tm_mday) != 3)
        {
            return false;
        }
        date.tm_year -= 1900;
        date.tm_mon -= 1;
        date.tm_hour = 12;
        date.tm_mday += days;
        const std::time_t shifted = std::mktime(&date);
        if (shifted == static_cast<std::time_t>(-1)) return false;
#if defined(_WIN32)
        localtime_s(&date, &shifted);
#else
        localtime_r(&shifted, &date);
#endif
        std::snprintf(
            g_m85AnchorDate,
            sizeof(g_m85AnchorDate),
            "%04d-%02d-%02d",
            date.tm_year + 1900,
            date.tm_mon + 1,
            date.tm_mday);
        return true;
    }

    void M89SetTodayText()
    {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        std::snprintf(
            g_m85AnchorDate,
            sizeof(g_m85AnchorDate),
            "%04d-%02d-%02d",
            local.tm_year + 1900,
            local.tm_mon + 1,
            local.tm_mday);
    }

    void M89PumpHistoryAutoLoad()
    {
        if (g_m89Pumping) return;
        g_m89Pumping = true;

        const trading::app::MarketDataSnapshot snapshot =
            g_marketDataModule.Snapshot();
        bool requestNext = false;
        bool applyVisible = false;
        trading::Continuation continuation;
        std::string code;
        int minuteUnit = 1;
        std::size_t downloaded = 0U;
        std::size_t target = 0U;
        {
            std::lock_guard<std::mutex> lock(g_m89History.mutex);
            target = g_m89History.targetBars;
            downloaded = g_m89History.downloadedBars;
            if (snapshot.state == trading::app::MarketDataState::Error) {
                g_m89History.active = false;
                g_m89History.requestInFlight = false;
            }
            if (g_m89History.applyVisiblePending &&
                !g_mainRenderSurface.timeAxis.Empty() &&
                g_mainRenderSurface.timeAxis.Size() == snapshot.barCount)
            {
                g_m89History.applyVisiblePending = false;
                applyVisible = true;
            }

            const bool matching =
                snapshot.code == g_m89History.code &&
                snapshot.minuteUnit == g_m89History.minuteUnit;
            const bool hasMore =
                (snapshot.continuation.continueYn == "Y" ||
                 snapshot.continuation.continueYn == "y") &&
                !snapshot.continuation.nextKey.empty();
            const auto elapsed = M89Clock::now() - g_m89History.lastRequestAt;
            if (g_m89History.active && matching &&
                !g_m89History.requestInFlight && hasMore &&
                elapsed >= std::chrono::milliseconds(1100))
            {
                g_m89History.requestInFlight = true;
                g_m89History.lastRequestAt = M89Clock::now();
                continuation = snapshot.continuation;
                code = snapshot.code;
                minuteUnit = snapshot.minuteUnit;
                requestNext = true;
            }
        }

        if (applyVisible) ApplyVisibleBarCount(true);

        if (requestNext) {
            std::string error;
            if (!g_runtimeRunner ||
                !g_runtimeRunner->RequestStockMinuteBars(
                    code,
                    minuteUnit,
                    continuation,
                    error))
            {
                M89StopRequestState(error);
                g_log.Add("FAULT", "자동 과거데이터 요청 실패: %s", error.c_str());
            }
            else {
                g_log.Add(
                    "DATA",
                    "기준일 과거데이터 요청: %s %d분 다운로드 %zu봉 / 조회 %zu봉",
                    code.c_str(),
                    minuteUnit,
                    downloaded,
                    target);
            }
        }

        g_m89Pumping = false;
    }

    void M89RequestOneAdditionalPage()
    {
        const trading::app::MarketDataSnapshot snapshot =
            g_marketDataModule.Snapshot();
        const bool hasMore =
            (snapshot.continuation.continueYn == "Y" ||
             snapshot.continuation.continueYn == "y") &&
            !snapshot.continuation.nextKey.empty();
        if (!hasMore || !g_runtimeRunner) return;

        std::size_t oldTarget = 0U;
        std::size_t pageSize = 900U;
        {
            std::lock_guard<std::mutex> lock(g_m89History.mutex);
            if (g_m89History.active || g_m89History.requestInFlight) return;
            const auto elapsed = M89Clock::now() - g_m89History.lastRequestAt;
            if (elapsed < std::chrono::milliseconds(1100)) {
                g_log.Add("REJECT", "추가데이터 요청 간격을 기다리세요.");
                return;
            }
            oldTarget = g_m89History.targetBars;
            pageSize = (std::max)(1U, g_m89History.pageSizeBars);
            g_m89History.targetBars = (std::min)(20000U, oldTarget + pageSize);
            g_m89History.active = true;
            g_m89History.onePageOnly = true;
            g_m89History.requestInFlight = true;
            g_m89History.lastRequestAt = M89Clock::now();
        }

        std::string error;
        if (!g_runtimeRunner->RequestStockMinuteBars(
                snapshot.code,
                snapshot.minuteUnit,
                snapshot.continuation,
                error))
        {
            std::lock_guard<std::mutex> lock(g_m89History.mutex);
            g_m89History.targetBars = oldTarget;
            g_m89History.active = false;
            g_m89History.onePageOnly = false;
            g_m89History.requestInFlight = false;
            g_m89History.error = error;
            g_log.Add("FAULT", "추가데이터 요청 실패: %s", error.c_str());
            return;
        }

        g_log.Add(
            "DATA",
            "첫 캔들 이전 1페이지 요청: 조회 %zu -> %zu봉",
            oldTarget,
            (std::min)(20000U, oldTarget + pageSize));
    }

    void M89SyncVisibleCountFromViewport(int* value, const char* label)
    {
        if (value == nullptr || label == nullptr ||
            !g_mainRenderSurface.viewport.initialized)
        {
            return;
        }
        const ImGuiID id = ImGui::GetID(label);
        if (ImGui::GetActiveID() == id) return;
        const double span =
            g_mainRenderSurface.viewport.visibleEnd -
            g_mainRenderSurface.viewport.visibleStart;
        const int visible = (std::max)(12, static_cast<int>(std::llround(span)) + 1);
        *value = (std::min)(2000, visible);
    }
}

bool ImGui::M89Button(const char* label, const ImVec2& size)
{
    M89PumpHistoryAutoLoad();
    if (label == nullptr) return ImGui::Button(label, size);

    const int timeFrame = M89TimeFrameIndex(label);
    if (timeFrame >= 0) {
        if (M89InMarketPanel()) {
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
            return false;
        }
        const bool pressed = ImGui::Button(label, size);
        if (pressed) g_timeFrameIndex = timeFrame;
        return false;
    }

    if (M89InMarketPanel() && std::strcmp(label, "재조회") == 0) {
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        return false;
    }

    if (std::strcmp(label, "봉 적용") == 0 ||
        std::strcmp(label, "이동##date_m87") == 0 ||
        std::strcmp(label, "재조회") == 0)
    {
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        return false;
    }

    if (std::strcmp(label, "<##date_m87") == 0) {
        const bool pressed = ImGui::SmallButton(label);
        if (pressed) M89ShiftDateText(-1);
        return false;
    }
    if (std::strcmp(label, ">##date_m87") == 0) {
        const bool pressed = ImGui::SmallButton(label);
        if (pressed) M89ShiftDateText(1);
        return false;
    }
    if (std::strcmp(label, "오늘/최신##date_m87") == 0) {
        const bool pressed = ImGui::SmallButton(label);
        if (pressed) M89SetTodayText();
        return false;
    }

    if (std::strcmp(label, "차트 조회") == 0) {
        const bool pressed = ImGui::Button(label, size);
        if (pressed) M89PrepareChartQuery();
        return pressed;
    }

    if (std::strcmp(label, "추가데이터") == 0) {
        bool loading = false;
        {
            std::lock_guard<std::mutex> lock(g_m89History.mutex);
            loading = g_m89History.active || g_m89History.requestInFlight;
        }
        if (loading) ImGui::BeginDisabled();
        const bool pressed = ImGui::Button(label, size);
        if (loading) ImGui::EndDisabled();
        if (pressed && !loading) M89RequestOneAdditionalPage();
        return false;
    }

    return ImGui::Button(label, size);
}

bool ImGui::M89InputInt(
    const char* label,
    int* value,
    int step,
    int stepFast,
    ImGuiInputTextFlags flags)
{
    const bool visibleField =
        label != nullptr && std::strcmp(label, "##visible_bars_m87") == 0;
    if (visibleField) M89SyncVisibleCountFromViewport(value, label);

    const bool changed = ImGui::InputInt(
        label,
        value,
        step,
        stepFast,
        flags);

    if (visibleField) {
        if (changed && value != nullptr) {
            *value = (std::max)(12, (std::min)(2000, *value));
            ApplyVisibleBarCount(false);
        }

        static int targetBars = 1800;
        targetBars = static_cast<int>(M89TargetBars());
        ImGui::SameLine();
        ImGui::TextDisabled("| 조회");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(68.0f);
        if (ImGui::InputInt(
                "##target_history_bars_m89",
                &targetBars,
                0,
                0))
        {
            targetBars = (std::max)(100, (std::min)(20000, targetBars));
            M89SetTargetBars(static_cast<std::size_t>(targetBars));
        }
    }

    return changed;
}

void ImGui::M89TextDisabled(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    ImGui::TextDisabledV(format, arguments);
    va_end(arguments);

    if (format != nullptr && std::strcmp(format, "수신 %zu봉%s") == 0) {
        std::size_t target = 0U;
        std::size_t downloaded = 0U;
        bool loading = false;
        std::string anchor;
        std::string resolved;
        std::string error;
        {
            std::lock_guard<std::mutex> lock(g_m89History.mutex);
            target = g_m89History.targetBars;
            downloaded = g_m89History.downloadedBars;
            loading = g_m89History.active || g_m89History.requestInFlight;
            anchor = g_m89History.anchorDate;
            resolved = g_m89History.resolvedEndDate;
            error = g_m89History.error;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            loading ? "/ 다운로드 %zu / 조회 %zu 자동조회 중" :
                      "/ 다운로드 %zu / 조회 %zu",
            downloaded,
            target);
        if (!anchor.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled(
                resolved.empty() ? "/ 기준일 %s" : "/ 기준일 %s 마지막 %s",
                anchor.c_str(),
                resolved.c_str());
        }
        if (!error.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(
                ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
                "%s",
                error.c_str());
        }
    }
}
