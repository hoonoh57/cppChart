#include "stock_pool_tick_detail_ui.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace trading::stock_pool::ui
{
    namespace
    {
        struct DisplayBar final
        {
            std::size_t index = 0U;
            float x = 0.0f;
            int minuteKey = 0;
            int ordinal = 0;
            int count = 0;
        };

        std::string DigitsOnly(const std::string& value)
        {
            std::string result;
            result.reserve(value.size());
            for (unsigned char character : value) {
                if (character >= '0' && character <= '9') {
                    result.push_back(static_cast<char>(character));
                }
            }
            return result;
        }

        int HmsToSeconds(int hhmmss)
        {
            return (hhmmss / 10000) * 3600 +
                ((hhmmss / 100) % 100) * 60 +
                (hhmmss % 100);
        }

        int PackedTimeSeconds(EpochMillis timestamp)
        {
            if (timestamp <= 0) return 0;
            const long long packed = timestamp / 1000LL;
            return HmsToSeconds(static_cast<int>(packed % 1000000LL));
        }

        std::string PackedDate(EpochMillis timestamp)
        {
            if (timestamp <= 0) return {};
            const long long packed = timestamp / 1000LL;
            return std::to_string(packed / 1000000LL);
        }

        void FormatClock(int secondOfDay, char* buffer, std::size_t size)
        {
            if (buffer == nullptr || size == 0U) return;
            secondOfDay = (std::max)(0, secondOfDay);
            std::snprintf(
                buffer,
                size,
                "%02d:%02d",
                secondOfDay / 3600,
                (secondOfDay / 60) % 60);
        }

        bool IsBuyEligible(const intuitive::StrengthPoint& point) noexcept
        {
            return
                point.inEvaluationWindow &&
                point.fresh &&
                point.bullishRegime &&
                point.barsSinceCross >= 0 &&
                point.crossJmaSlopePercent > 0.0;
        }

        ImU32 CandleColor(const Bar& bar)
        {
            if (bar.close > bar.open) return IM_COL32(235, 72, 72, 240);
            if (bar.close < bar.open) return IM_COL32(55, 130, 225, 240);
            return IM_COL32(150, 150, 160, 220);
        }

        float SymmetricY(
            double value,
            double maximumAbsolute,
            float top,
            float bottom)
        {
            maximumAbsolute = (std::max)(maximumAbsolute, 1.0e-9);
            const double normalized = (std::max)(
                -1.0,
                (std::min)(1.0, value / maximumAbsolute));
            const float center = (top + bottom) * 0.5f;
            const float half = (bottom - top) * 0.46f;
            return center - static_cast<float>(normalized) * half;
        }

        void DrawPaneFrame(
            ImDrawList* draw,
            const ImVec2& minimum,
            const ImVec2& maximum,
            const char* title)
        {
            draw->AddRectFilled(minimum, maximum, IM_COL32(10, 12, 16, 255));
            draw->AddRect(minimum, maximum, IM_COL32(55, 62, 75, 220));
            if (title != nullptr && title[0] != '\0') {
                draw->AddText(
                    ImVec2(minimum.x + 5.0f, minimum.y + 4.0f),
                    IM_COL32(155, 165, 180, 235),
                    title);
            }
        }

        void DrawZeroLine(
            ImDrawList* draw,
            float left,
            float right,
            float top,
            float bottom)
        {
            const float y = (top + bottom) * 0.5f;
            draw->AddLine(
                ImVec2(left, y),
                ImVec2(right, y),
                IM_COL32(95, 105, 120, 110),
                1.0f);
        }

        bool EditCalculationParameters(intuitive::StrengthConfig& config)
        {
            bool changed = false;

            int fast = config.fastJmaPeriod;
            ImGui::SetNextItemWidth(66.0f);
            if (ImGui::InputInt("JMA Fast", &fast, 0, 0)) {
                fast = (std::max)(1, (std::min)(200, fast));
                if (fast != config.fastJmaPeriod) {
                    config.fastJmaPeriod = fast;
                    changed = true;
                }
            }
            ImGui::SameLine();

            int slow = config.slowJmaPeriod;
            ImGui::SetNextItemWidth(66.0f);
            if (ImGui::InputInt("JMA Slow", &slow, 0, 0)) {
                slow = (std::max)(1, (std::min)(300, slow));
                if (slow != config.slowJmaPeriod) {
                    config.slowJmaPeriod = slow;
                    changed = true;
                }
            }
            ImGui::SameLine();

            int phase = config.jmaPhase;
            ImGui::SetNextItemWidth(62.0f);
            if (ImGui::InputInt("Phase", &phase, 0, 0)) {
                phase = (std::max)(-100, (std::min)(100, phase));
                if (phase != config.jmaPhase) {
                    config.jmaPhase = phase;
                    changed = true;
                }
            }
            ImGui::SameLine();

            int power = config.jmaPower;
            ImGui::SetNextItemWidth(54.0f);
            if (ImGui::InputInt("Power", &power, 0, 0)) {
                power = (std::max)(1, (std::min)(8, power));
                if (power != config.jmaPower) {
                    config.jmaPower = power;
                    changed = true;
                }
            }
            ImGui::SameLine();

            int freshBars = config.maxFreshBars;
            ImGui::SetNextItemWidth(54.0f);
            if (ImGui::InputInt("돌파유효봉", &freshBars, 0, 0)) {
                freshBars = (std::max)(0, (std::min)(30, freshBars));
                if (freshBars != config.maxFreshBars) {
                    config.maxFreshBars = freshBars;
                    changed = true;
                }
            }

            return changed;
        }
    }

    void OpenTickDetail(
        TickDetailUiState& state,
        std::size_t memberIndex) noexcept
    {
        state.memberIndex = memberIndex;
        state.open = true;
    }

    bool DrawTickDetailWindow(
        TickDetailUiState& state,
        const MemberSeries* member,
        const intuitive::MemberStrengthSeries* strength,
        intuitive::StrengthConfig& config,
        const std::string& tradingDate,
        int tickSize,
        int asOfMinute)
    {
        if (!state.open) return false;
        if (member == nullptr || strength == nullptr) {
            state.open = false;
            return false;
        }

        const std::string title =
            "틱 상세분석 — " + member->code + " " + member->name +
            "###stock_pool_tick_detail";
        ImGui::SetNextWindowSize(ImVec2(1500.0f, 930.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(title.c_str(), &state.open)) {
            ImGui::End();
            return false;
        }

        const std::string date = DigitsOnly(tradingDate);
        char asOfClock[16]{};
        FormatClock(asOfMinute * 60, asOfClock, sizeof(asOfClock));
        ImGui::Text(
            "%s %s | 사용자 지정일 %s | T%d | 인과 마감 %s",
            member->code.c_str(),
            member->name.c_str(),
            date.c_str(),
            tickSize,
            asOfClock);
        ImGui::SameLine();
        ImGui::TextDisabled(
            "| 초 단위가 없는 Tn은 HH:mm + 분내 순서로만 표시");

        const bool configChanged = EditCalculationParameters(config);
        if (configChanged) {
            ImGui::TextColored(
                ImVec4(0.95f, 0.78f, 0.32f, 1.0f),
                "설정 변경됨 — 동일한 실제 Tn 데이터로 전체 종목 강도를 재계산합니다.");
        }

        ImGui::Separator();
        ImGui::Checkbox("캔들", &state.showCandles);
        ImGui::SameLine();
        ImGui::Checkbox("JMA Fast", &state.showFastJma);
        ImGui::SameLine();
        ImGui::Checkbox("JMA Slow", &state.showSlowJma);
        ImGui::SameLine();
        ImGui::Checkbox("교차신호", &state.showCrossSignals);
        ImGui::SameLine();
        ImGui::Checkbox("매수유효", &state.showBuyEligible);
        ImGui::SameLine();
        ImGui::Checkbox("JMA slope", &state.showSlope);
        ImGui::SameLine();
        ImGui::Checkbox("Tick/min", &state.showTickRate);
        ImGui::SameLine();
        ImGui::Checkbox("MACD/ATR", &state.showMacdAtr);
        ImGui::SameLine();
        ImGui::Checkbox("OBV", &state.showObv);

        ImGui::TextDisabled(
            "마우스를 차트에 올리면 해당 Tn 봉의 시간/OHLC/JMA/교차/매수 Gate를 표시합니다."
            " 초 위치를 추정하지 않습니다.");

        if (member->bars.empty() || strength->points.empty() ||
            member->bars.size() != strength->points.size() || date.size() != 8U)
        {
            ImGui::Separator();
            ImGui::TextWrapped(
                "상세차트 데이터 정렬 계약이 충족되지 않았습니다. bars=%zu points=%zu date=%s",
                member->bars.size(),
                strength->points.size(),
                date.c_str());
            ImGui::End();
            return configChanged;
        }

        constexpr int startSecond = 9 * 3600;
        constexpr int endSecond = 10 * 3600;
        const int currentSecond = (std::min)(endSecond, asOfMinute * 60 + 59);

        std::vector<std::size_t> visible;
        std::map<int, int> barsPerMinute;
        for (std::size_t index = 0U; index < member->bars.size(); ++index) {
            const Bar& bar = member->bars[index];
            if (PackedDate(bar.closeTimestampMs) != date) continue;
            const int second = PackedTimeSeconds(bar.closeTimestampMs);
            if (second < startSecond || second > currentSecond) continue;
            visible.push_back(index);
            ++barsPerMinute[second / 60];
        }

        if (visible.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped(
                "사용자 지정일 %s의 09:00~%s 완료 T%d 봉이 없습니다.",
                date.c_str(),
                asOfClock,
                tickSize);
            ImGui::End();
            return configChanged;
        }

        const intuitive::StrengthPoint& currentPoint =
            strength->points[visible.back()];
        ImGui::Text(
            "현재봉: JMA%d slope %+.1f%% | JMA%d slope %+.1f%% | cross %+.1f%% | age %d | 누적 %+.2f%%",
            config.fastJmaPeriod,
            currentPoint.fastJmaSlopePercent,
            config.slowJmaPeriod,
            currentPoint.slowJmaSlopePercent,
            currentPoint.crossJmaSlopePercent,
            currentPoint.barsSinceCross,
            currentPoint.sessionReturnPercent);
        ImGui::SameLine();
        if (IsBuyEligible(currentPoint)) {
            ImGui::TextColored(
                ImVec4(0.25f, 0.95f, 0.58f, 1.0f),
                "매수유효");
        }
        else {
            ImGui::TextDisabled("매수 Gate 미충족");
        }

        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float canvasWidth = (std::max)(920.0f, availableWidth);
        const float priceHeight = 380.0f;
        const float indicatorHeight = 108.0f;
        float canvasHeight = priceHeight + 38.0f;
        if (state.showSlope) canvasHeight += indicatorHeight;
        if (state.showTickRate) canvasHeight += indicatorHeight;
        if (state.showMacdAtr) canvasHeight += indicatorHeight;
        if (state.showObv) canvasHeight += indicatorHeight;

        ImGui::InvisibleButton(
            "##tick_detail_canvas",
            ImVec2(canvasWidth, canvasHeight));
        const ImVec2 canvasMin = ImGui::GetItemRectMin();
        const ImVec2 canvasMax = ImGui::GetItemRectMax();
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        const float plotLeft = canvasMin.x + 66.0f;
        const float plotRight = canvasMax.x - 12.0f;
        const float plotWidth = (std::max)(1.0f, plotRight - plotLeft);
        const auto xTime = [&](double second) {
            const double unit =
                (second - static_cast<double>(startSecond)) /
                static_cast<double>(endSecond - startSecond);
            return plotLeft + static_cast<float>(
                (std::max)(0.0, (std::min)(1.0, unit))) * plotWidth;
        };

        float nextTop = canvasMin.y;
        const float priceTop = nextTop;
        const float priceBottom = priceTop + priceHeight;
        DrawPaneFrame(
            draw,
            ImVec2(canvasMin.x, priceTop),
            ImVec2(canvasMax.x, priceBottom),
            "가격 / JMA / 교차 / 매수유효");
        nextTop = priceBottom;

        struct PaneBounds final
        {
            bool enabled = false;
            float top = 0.0f;
            float bottom = 0.0f;
        };
        PaneBounds slopePane;
        PaneBounds tickPane;
        PaneBounds macdPane;
        PaneBounds obvPane;

        const auto allocatePane = [&](bool enabled, PaneBounds& pane, const char* title) mutable {
            if (!enabled) return;
            pane.enabled = true;
            pane.top = nextTop;
            pane.bottom = nextTop + indicatorHeight;
            DrawPaneFrame(
                draw,
                ImVec2(canvasMin.x, pane.top),
                ImVec2(canvasMax.x, pane.bottom),
                title);
            nextTop = pane.bottom;
        };

        allocatePane(state.showSlope, slopePane, "JMA slope %/bar");
        allocatePane(state.showTickRate, tickPane, "실제 완료 Tick/min");
        allocatePane(state.showMacdAtr, macdPane, "MACD histogram / ATR");
        allocatePane(state.showObv, obvPane, "OBV impulse");
        const float timeAxisTop = nextTop;
        const float timeAxisBottom = canvasMax.y;

        double priceMinimum = member->bars[visible.front()].low;
        double priceMaximum = member->bars[visible.front()].high;
        double slopeMaximum = 0.1;
        double tickMaximum = 1.0;
        double macdMaximum = 0.01;
        double obvMaximum = 0.01;
        for (std::size_t index : visible) {
            const Bar& bar = member->bars[index];
            const intuitive::StrengthPoint& point = strength->points[index];
            priceMinimum = (std::min)(priceMinimum, bar.low);
            priceMaximum = (std::max)(priceMaximum, bar.high);
            if (state.showFastJma) {
                priceMinimum = (std::min)(priceMinimum, point.fastJma);
                priceMaximum = (std::max)(priceMaximum, point.fastJma);
            }
            if (state.showSlowJma) {
                priceMinimum = (std::min)(priceMinimum, point.slowJma);
                priceMaximum = (std::max)(priceMaximum, point.slowJma);
            }
            slopeMaximum = (std::max)(
                slopeMaximum,
                (std::max)(
                    std::abs(point.fastJmaSlopePercent),
                    std::abs(point.slowJmaSlopePercent)));
            if (point.tickAvailable && std::isfinite(point.tickRatePerMinute)) {
                tickMaximum = (std::max)(tickMaximum, point.tickRatePerMinute);
            }
            macdMaximum = (std::max)(
                macdMaximum,
                std::abs(point.macdHistogramAtr));
            obvMaximum = (std::max)(obvMaximum, std::abs(point.obvImpulse));
        }

        const double rawPriceRange = (std::max)(1.0e-9, priceMaximum - priceMinimum);
        priceMinimum -= rawPriceRange * 0.08;
        priceMaximum += rawPriceRange * 0.08;
        const double priceRange = (std::max)(1.0e-9, priceMaximum - priceMinimum);
        const float pricePlotTop = priceTop + 22.0f;
        const float pricePlotBottom = priceBottom - 12.0f;
        const auto yPrice = [&](double value) {
            return pricePlotBottom - static_cast<float>(
                (value - priceMinimum) / priceRange) *
                (pricePlotBottom - pricePlotTop);
        };

        for (int minute = 9 * 60; minute <= 10 * 60; minute += 10) {
            const float x = xTime(static_cast<double>(minute * 60));
            draw->AddLine(
                ImVec2(x, pricePlotTop),
                ImVec2(x, timeAxisTop),
                IM_COL32(65, 72, 84, 115),
                1.0f);
            char clock[16]{};
            FormatClock(minute * 60, clock, sizeof(clock));
            draw->AddText(
                ImVec2(x - 18.0f, timeAxisTop + 8.0f),
                IM_COL32(170, 178, 190, 235),
                clock);
        }

        const float evaluationX = xTime(9 * 3600 + 3 * 60);
        draw->AddLine(
            ImVec2(evaluationX, pricePlotTop),
            ImVec2(evaluationX, timeAxisTop),
            IM_COL32(255, 205, 90, 190),
            1.3f);
        draw->AddText(
            ImVec2(evaluationX + 3.0f, pricePlotTop + 3.0f),
            IM_COL32(255, 210, 105, 230),
            "09:03 평가 시작");

        if (slopePane.enabled) {
            DrawZeroLine(draw, plotLeft, plotRight, slopePane.top + 18.0f, slopePane.bottom - 6.0f);
        }
        if (macdPane.enabled) {
            DrawZeroLine(draw, plotLeft, plotRight, macdPane.top + 18.0f, macdPane.bottom - 6.0f);
        }
        if (obvPane.enabled) {
            DrawZeroLine(draw, plotLeft, plotRight, obvPane.top + 18.0f, obvPane.bottom - 6.0f);
        }

        std::map<int, int> ordinalInMinute;
        std::vector<DisplayBar> displayBars;
        displayBars.reserve(visible.size());
        std::vector<ImVec2> fastLine;
        std::vector<ImVec2> slowLine;
        std::vector<ImVec2> tickLine;
        std::vector<ImVec2> macdLine;
        std::vector<ImVec2> obvLine;

        const float candleHalf = (std::max)(
            1.2f,
            (std::min)(5.0f, plotWidth / static_cast<float>((std::max)(25U, visible.size())) * 0.28f));

        for (std::size_t index : visible) {
            const Bar& bar = member->bars[index];
            const intuitive::StrengthPoint& point = strength->points[index];
            const int rawSecond = PackedTimeSeconds(bar.closeTimestampMs);
            const int minuteKey = rawSecond / 60;
            const int count = (std::max)(1, barsPerMinute[minuteKey]);
            const int ordinal = ordinalInMinute[minuteKey]++;
            const double displaySecond =
                static_cast<double>(minuteKey * 60) +
                (static_cast<double>(ordinal + 1) /
                 static_cast<double>(count + 1)) * 60.0;
            const float x = xTime(displaySecond);
            displayBars.push_back(DisplayBar{index, x, minuteKey, ordinal, count});

            if (state.showBuyEligible && IsBuyEligible(point)) {
                draw->AddRectFilled(
                    ImVec2(x - candleHalf * 1.8f, pricePlotTop),
                    ImVec2(x + candleHalf * 1.8f, pricePlotBottom),
                    IM_COL32(45, 215, 120, 30));
            }

            if (state.showCandles) {
                const ImU32 color = CandleColor(bar);
                draw->AddLine(
                    ImVec2(x, yPrice(bar.high)),
                    ImVec2(x, yPrice(bar.low)),
                    color,
                    1.0f);
                const float bodyTop = yPrice((std::max)(bar.open, bar.close));
                const float bodyBottom = yPrice((std::min)(bar.open, bar.close));
                if (std::abs(bodyBottom - bodyTop) < 1.0f) {
                    draw->AddLine(
                        ImVec2(x - candleHalf, bodyTop),
                        ImVec2(x + candleHalf, bodyTop),
                        color,
                        1.5f);
                }
                else {
                    draw->AddRectFilled(
                        ImVec2(x - candleHalf, bodyTop),
                        ImVec2(x + candleHalf, bodyBottom),
                        color);
                }
            }

            if (state.showFastJma) {
                fastLine.emplace_back(x, yPrice(point.fastJma));
            }
            if (state.showSlowJma) {
                slowLine.emplace_back(x, yPrice(point.slowJma));
            }

            if (state.showCrossSignals && point.crossUp) {
                const float y = yPrice(bar.low) + 5.0f;
                draw->AddTriangleFilled(
                    ImVec2(x, y),
                    ImVec2(x - 6.0f, y + 10.0f),
                    ImVec2(x + 6.0f, y + 10.0f),
                    IM_COL32(60, 240, 135, 245));
                draw->AddLine(
                    ImVec2(x, pricePlotTop),
                    ImVec2(x, pricePlotBottom),
                    IM_COL32(60, 230, 130, 90),
                    1.0f);
            }
            else if (state.showCrossSignals && point.crossDown) {
                const float y = yPrice(bar.high) - 5.0f;
                draw->AddTriangleFilled(
                    ImVec2(x, y),
                    ImVec2(x - 6.0f, y - 10.0f),
                    ImVec2(x + 6.0f, y - 10.0f),
                    IM_COL32(242, 85, 95, 245));
                draw->AddLine(
                    ImVec2(x, pricePlotTop),
                    ImVec2(x, pricePlotBottom),
                    IM_COL32(235, 80, 90, 85),
                    1.0f);
            }

            if (state.showBuyEligible && IsBuyEligible(point)) {
                const float markerY = (std::min)(
                    pricePlotBottom - 5.0f,
                    yPrice(bar.low) + 20.0f);
                draw->AddCircleFilled(
                    ImVec2(x, markerY),
                    5.0f,
                    IM_COL32(65, 245, 145, 255));
                draw->AddText(
                    ImVec2(x + 6.0f, markerY - 8.0f),
                    IM_COL32(85, 245, 155, 255),
                    "B");
            }

            if (slopePane.enabled) {
                const float top = slopePane.top + 18.0f;
                const float bottom = slopePane.bottom - 6.0f;
                const float zero = (top + bottom) * 0.5f;
                const float fastY = SymmetricY(
                    point.fastJmaSlopePercent,
                    slopeMaximum,
                    top,
                    bottom);
                draw->AddRectFilled(
                    ImVec2(x - 1.5f, (std::min)(fastY, zero)),
                    ImVec2(x + 1.5f, (std::max)(fastY, zero)),
                    point.fastJmaSlopePercent >= 0.0
                        ? IM_COL32(55, 210, 125, 205)
                        : IM_COL32(225, 78, 92, 205));
            }

            if (tickPane.enabled && point.tickAvailable &&
                std::isfinite(point.tickRatePerMinute))
            {
                const float top = tickPane.top + 18.0f;
                const float bottom = tickPane.bottom - 6.0f;
                const double unit = (std::max)(
                    0.0,
                    (std::min)(1.0, point.tickRatePerMinute / tickMaximum));
                tickLine.emplace_back(
                    x,
                    bottom - static_cast<float>(unit) * (bottom - top));
            }

            if (macdPane.enabled) {
                const float top = macdPane.top + 18.0f;
                const float bottom = macdPane.bottom - 6.0f;
                macdLine.emplace_back(
                    x,
                    SymmetricY(point.macdHistogramAtr, macdMaximum, top, bottom));
            }

            if (obvPane.enabled) {
                const float top = obvPane.top + 18.0f;
                const float bottom = obvPane.bottom - 6.0f;
                obvLine.emplace_back(
                    x,
                    SymmetricY(point.obvImpulse, obvMaximum, top, bottom));
            }
        }

        if (fastLine.size() > 1U) {
            draw->AddPolyline(
                fastLine.data(),
                static_cast<int>(fastLine.size()),
                IM_COL32(235, 70, 205, 250),
                0,
                2.0f);
        }
        if (slowLine.size() > 1U) {
            draw->AddPolyline(
                slowLine.data(),
                static_cast<int>(slowLine.size()),
                IM_COL32(190, 195, 205, 245),
                0,
                1.7f);
        }
        if (tickLine.size() > 1U) {
            draw->AddPolyline(
                tickLine.data(),
                static_cast<int>(tickLine.size()),
                IM_COL32(65, 205, 235, 245),
                0,
                1.7f);
        }
        if (macdLine.size() > 1U) {
            draw->AddPolyline(
                macdLine.data(),
                static_cast<int>(macdLine.size()),
                IM_COL32(255, 180, 80, 245),
                0,
                1.6f);
        }
        if (obvLine.size() > 1U) {
            draw->AddPolyline(
                obvLine.data(),
                static_cast<int>(obvLine.size()),
                IM_COL32(150, 220, 110, 245),
                0,
                1.6f);
        }

        char priceHigh[32]{};
        char priceLow[32]{};
        std::snprintf(priceHigh, sizeof(priceHigh), "%.0f", priceMaximum);
        std::snprintf(priceLow, sizeof(priceLow), "%.0f", priceMinimum);
        draw->AddText(
            ImVec2(canvasMin.x + 5.0f, pricePlotTop),
            IM_COL32(165, 175, 190, 225),
            priceHigh);
        draw->AddText(
            ImVec2(canvasMin.x + 5.0f, pricePlotBottom - 14.0f),
            IM_COL32(165, 175, 190, 225),
            priceLow);

        if (hovered && !displayBars.empty()) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const DisplayBar* nearest = &displayBars.front();
            float nearestDistance = std::abs(mouse.x - nearest->x);
            for (const DisplayBar& candidate : displayBars) {
                const float distance = std::abs(mouse.x - candidate.x);
                if (distance < nearestDistance) {
                    nearest = &candidate;
                    nearestDistance = distance;
                }
            }

            const Bar& bar = member->bars[nearest->index];
            const intuitive::StrengthPoint& point = strength->points[nearest->index];
            draw->AddLine(
                ImVec2(nearest->x, pricePlotTop),
                ImVec2(nearest->x, timeAxisTop),
                IM_COL32(230, 235, 245, 180),
                1.0f);
            draw->AddRect(
                ImVec2(nearest->x - candleHalf * 2.0f, pricePlotTop),
                ImVec2(nearest->x + candleHalf * 2.0f, pricePlotBottom),
                IM_COL32(230, 235, 245, 100));

            char clock[16]{};
            FormatClock(nearest->minuteKey * 60, clock, sizeof(clock));
            const char* cross = point.crossUp
                ? "UP"
                : (point.crossDown ? "DOWN" : "-");
            ImGui::BeginTooltip();
            ImGui::Text(
                "%s  [분내 %d/%d]  T%d",
                clock,
                nearest->ordinal + 1,
                nearest->count,
                tickSize);
            ImGui::TextDisabled(
                "CYBOS historical Tn 시간계약=HH:mm; 분내 초는 생성하지 않음");
            ImGui::Separator();
            ImGui::Text(
                "O %.0f  H %.0f  L %.0f  C %.0f",
                bar.open,
                bar.high,
                bar.low,
                bar.close);
            ImGui::Text(
                "JMA%d %.2f (%+.1f%%/bar) | JMA%d %.2f (%+.1f%%/bar)",
                config.fastJmaPeriod,
                point.fastJma,
                point.fastJmaSlopePercent,
                config.slowJmaPeriod,
                point.slowJma,
                point.slowJmaSlopePercent);
            ImGui::Text(
                "교차 %s | 교차강도 %+.1f%% | age %d | 파동JMA %+.2f%%",
                cross,
                point.crossJmaSlopePercent,
                point.barsSinceCross,
                point.waveJmaGainPercent);
            ImGui::Text(
                "평가시간 %s | bullish %s | fresh %s | BUY %s",
                point.inEvaluationWindow ? "Y" : "N",
                point.bullishRegime ? "Y" : "N",
                point.fresh ? "Y" : "N",
                IsBuyEligible(point) ? "Y" : "N");
            if (point.tickAvailable && std::isfinite(point.tickRatePerMinute)) {
                ImGui::Text(
                    "Tick %.0f/min | accel %.2fx",
                    point.tickRatePerMinute,
                    point.tickAcceleration);
            }
            else {
                ImGui::TextDisabled("Tick -");
            }
            ImGui::Text(
                "MACD/ATR %+.4f | OBV impulse %+.4f | session %+.2f%%",
                point.macdHistogramAtr,
                point.obvImpulse,
                point.sessionReturnPercent);
            ImGui::EndTooltip();
        }

        ImGui::End();
        return configChanged;
    }
}
