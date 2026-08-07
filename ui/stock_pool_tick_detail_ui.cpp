#include "stock_pool_tick_detail_ui.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
            int minute = 0;
            int ordinal = 0;
            int count = 0;
        };

        struct Pane final
        {
            bool enabled = false;
            float top = 0.0f;
            float bottom = 0.0f;
        };

        std::string DigitsOnly(const std::string& text)
        {
            std::string result;
            for (unsigned char ch : text) {
                if (ch >= '0' && ch <= '9') result.push_back(static_cast<char>(ch));
            }
            return result;
        }

        int PackedTimeSeconds(EpochMillis timestamp)
        {
            if (timestamp <= 0) return 0;
            const int hhmmss = static_cast<int>((timestamp / 1000LL) % 1000000LL);
            return (hhmmss / 10000) * 3600 +
                ((hhmmss / 100) % 100) * 60 +
                hhmmss % 100;
        }

        std::string PackedDate(EpochMillis timestamp)
        {
            if (timestamp <= 0) return {};
            return std::to_string((timestamp / 1000LL) / 1000000LL);
        }

        void FormatClock(int secondOfDay, char* buffer, std::size_t size)
        {
            secondOfDay = (std::max)(0, secondOfDay);
            std::snprintf(
                buffer,
                size,
                "%02d:%02d",
                secondOfDay / 3600,
                (secondOfDay / 60) % 60);
        }

        bool BuyEligible(const intuitive::StrengthPoint& point) noexcept
        {
            return point.inEvaluationWindow &&
                point.fresh &&
                point.bullishRegime &&
                point.barsSinceCross >= 0 &&
                point.crossJmaSlopePercent > 0.0;
        }

        ImU32 CandleColor(const Bar& bar)
        {
            if (bar.close > bar.open) return IM_COL32(235, 72, 72, 240);
            if (bar.close < bar.open) return IM_COL32(55, 130, 225, 240);
            return IM_COL32(155, 155, 165, 230);
        }

        float SymmetricY(double value, double scale, float top, float bottom)
        {
            scale = (std::max)(scale, 1.0e-9);
            const double normalized = (std::max)(
                -1.0,
                (std::min)(1.0, value / scale));
            const float center = (top + bottom) * 0.5f;
            return center - static_cast<float>(normalized) *
                (bottom - top) * 0.44f;
        }

        void PaneFrame(
            ImDrawList* draw,
            float left,
            float right,
            float top,
            float bottom,
            const char* title)
        {
            draw->AddRectFilled(
                ImVec2(left, top),
                ImVec2(right, bottom),
                IM_COL32(10, 12, 16, 255));
            draw->AddRect(
                ImVec2(left, top),
                ImVec2(right, bottom),
                IM_COL32(55, 62, 75, 220));
            draw->AddText(
                ImVec2(left + 5.0f, top + 4.0f),
                IM_COL32(155, 166, 182, 235),
                title);
        }

        bool DrawParameters(intuitive::StrengthConfig& config)
        {
            bool changed = false;
            auto edit = [&](const char* label, int& value, int minimum, int maximum, float width) {
                int draft = value;
                ImGui::SetNextItemWidth(width);
                if (!ImGui::InputInt(label, &draft, 0, 0)) return;
                draft = (std::max)(minimum, (std::min)(maximum, draft));
                if (draft == value) return;
                value = draft;
                changed = true;
            };

            edit("JMA Fast", config.fastJmaPeriod, 1, 200, 64.0f);
            ImGui::SameLine();
            edit("JMA Slow", config.slowJmaPeriod, 1, 300, 64.0f);
            ImGui::SameLine();
            edit("Phase", config.jmaPhase, -100, 100, 58.0f);
            ImGui::SameLine();
            edit("Power", config.jmaPower, 1, 8, 48.0f);
            ImGui::SameLine();
            edit("돌파유효봉", config.maxFreshBars, 0, 30, 48.0f);
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
        ImGui::SetNextWindowSize(ImVec2(1480.0f, 920.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(title.c_str(), &state.open)) {
            ImGui::End();
            return false;
        }

        const std::string date = DigitsOnly(tradingDate);
        char asOf[16]{};
        FormatClock(asOfMinute * 60, asOf, sizeof(asOf));
        ImGui::Text(
            "%s %s | 사용자 지정일 %s | T%d | 인과 마감 %s",
            member->code.c_str(),
            member->name.c_str(),
            date.c_str(),
            tickSize,
            asOf);
        ImGui::SameLine();
        ImGui::TextDisabled("| HH:mm + 분내 순서, 가짜 초 없음");

        const bool changed = DrawParameters(config);
        if (changed) {
            ImGui::TextColored(
                ImVec4(0.95f, 0.78f, 0.32f, 1.0f),
                "설정 변경 — 동일 실제 Tn 데이터로 전체 종목 재계산");
        }

        ImGui::Separator();
        ImGui::Checkbox("캔들", &state.showCandles);
        ImGui::SameLine(); ImGui::Checkbox("JMA Fast", &state.showFastJma);
        ImGui::SameLine(); ImGui::Checkbox("JMA Slow", &state.showSlowJma);
        ImGui::SameLine(); ImGui::Checkbox("교차신호", &state.showCrossSignals);
        ImGui::SameLine(); ImGui::Checkbox("매수유효", &state.showBuyEligible);
        ImGui::SameLine(); ImGui::Checkbox("JMA slope", &state.showSlope);
        ImGui::SameLine(); ImGui::Checkbox("Tick/min", &state.showTickRate);
        ImGui::SameLine(); ImGui::Checkbox("MACD/ATR", &state.showMacdAtr);
        ImGui::SameLine(); ImGui::Checkbox("OBV", &state.showObv);
        ImGui::TextDisabled(
            "차트에 마우스를 올리면 시간/OHLC/JMA/교차/Gate 값을 같은 봉 기준으로 표시합니다.");

        if (date.size() != 8U || member->bars.empty() ||
            member->bars.size() != strength->points.size())
        {
            ImGui::TextWrapped(
                "상세분석 데이터 계약 오류: date=%s bars=%zu points=%zu",
                date.c_str(),
                member->bars.size(),
                strength->points.size());
            ImGui::End();
            return changed;
        }

        constexpr int sessionStart = 9 * 3600;
        constexpr int sessionEnd = 10 * 3600;
        const int currentEnd = (std::min)(sessionEnd, asOfMinute * 60 + 59);

        std::vector<std::size_t> visible;
        std::map<int, int> minuteCounts;
        for (std::size_t i = 0U; i < member->bars.size(); ++i) {
            const Bar& bar = member->bars[i];
            if (PackedDate(bar.closeTimestampMs) != date) continue;
            const int second = PackedTimeSeconds(bar.closeTimestampMs);
            if (second < sessionStart || second > currentEnd) continue;
            visible.push_back(i);
            ++minuteCounts[second / 60];
        }
        if (visible.empty()) {
            ImGui::Text("%s 09:00~%s 완료 T%d 봉이 없습니다.", date.c_str(), asOf, tickSize);
            ImGui::End();
            return changed;
        }

        const intuitive::StrengthPoint& latest = strength->points[visible.back()];
        ImGui::Text(
            "현재봉 JMA%d %+.1f%%/bar | JMA%d %+.1f%%/bar | cross %+.1f%% | age %d | 누적 %+.2f%%",
            config.fastJmaPeriod,
            latest.fastJmaSlopePercent,
            config.slowJmaPeriod,
            latest.slowJmaSlopePercent,
            latest.crossJmaSlopePercent,
            latest.barsSinceCross,
            latest.sessionReturnPercent);
        ImGui::SameLine();
        if (BuyEligible(latest)) {
            ImGui::TextColored(ImVec4(0.25f, 0.95f, 0.58f, 1.0f), "매수유효");
        }
        else {
            ImGui::TextDisabled("매수 Gate 미충족");
        }

        const float width = (std::max)(940.0f, ImGui::GetContentRegionAvail().x);
        constexpr float priceHeight = 360.0f;
        constexpr float subHeight = 100.0f;
        float height = priceHeight + 34.0f;
        if (state.showSlope) height += subHeight;
        if (state.showTickRate) height += subHeight;
        if (state.showMacdAtr) height += subHeight;
        if (state.showObv) height += subHeight;

        ImGui::InvisibleButton("##tick_detail_canvas", ImVec2(width, height));
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        const float plotLeft = minimum.x + 65.0f;
        const float plotRight = maximum.x - 10.0f;
        const float plotWidth = (std::max)(1.0f, plotRight - plotLeft);
        const auto xAt = [&](double second) {
            const double unit =
                (second - sessionStart) /
                static_cast<double>(sessionEnd - sessionStart);
            return plotLeft + static_cast<float>(
                (std::max)(0.0, (std::min)(1.0, unit))) * plotWidth;
        };

        float top = minimum.y;
        const float priceTop = top;
        const float priceBottom = top + priceHeight;
        PaneFrame(draw, minimum.x, maximum.x, priceTop, priceBottom,
            "가격 / JMA / 교차 / 매수유효");
        top = priceBottom;

        Pane slope;
        Pane tick;
        Pane macd;
        Pane obv;
        auto allocate = [&](bool enabled, Pane& pane, const char* label) {
            if (!enabled) return;
            pane.enabled = true;
            pane.top = top;
            pane.bottom = top + subHeight;
            PaneFrame(draw, minimum.x, maximum.x, pane.top, pane.bottom, label);
            top = pane.bottom;
        };
        allocate(state.showSlope, slope, "JMA slope %/bar");
        allocate(state.showTickRate, tick, "실제 완료 Tick/min");
        allocate(state.showMacdAtr, macd, "MACD histogram / ATR");
        allocate(state.showObv, obv, "OBV impulse");
        const float axisTop = top;

        double pMin = member->bars[visible.front()].low;
        double pMax = member->bars[visible.front()].high;
        double slopeScale = 0.1;
        double tickScale = 1.0;
        double macdScale = 0.01;
        double obvScale = 0.01;
        for (std::size_t i : visible) {
            const Bar& bar = member->bars[i];
            const intuitive::StrengthPoint& point = strength->points[i];
            pMin = (std::min)(pMin, bar.low);
            pMax = (std::max)(pMax, bar.high);
            if (state.showFastJma) {
                pMin = (std::min)(pMin, point.fastJma);
                pMax = (std::max)(pMax, point.fastJma);
            }
            if (state.showSlowJma) {
                pMin = (std::min)(pMin, point.slowJma);
                pMax = (std::max)(pMax, point.slowJma);
            }
            slopeScale = (std::max)(slopeScale,
                (std::max)(std::abs(point.fastJmaSlopePercent),
                           std::abs(point.slowJmaSlopePercent)));
            if (point.tickAvailable && std::isfinite(point.tickRatePerMinute)) {
                tickScale = (std::max)(tickScale, point.tickRatePerMinute);
            }
            macdScale = (std::max)(macdScale, std::abs(point.macdHistogramAtr));
            obvScale = (std::max)(obvScale, std::abs(point.obvImpulse));
        }
        const double rawRange = (std::max)(1.0e-9, pMax - pMin);
        pMin -= rawRange * 0.08;
        pMax += rawRange * 0.08;
        const double pRange = (std::max)(1.0e-9, pMax - pMin);
        const float pricePlotTop = priceTop + 22.0f;
        const float pricePlotBottom = priceBottom - 10.0f;
        const auto yPrice = [&](double value) {
            return pricePlotBottom - static_cast<float>((value - pMin) / pRange) *
                (pricePlotBottom - pricePlotTop);
        };

        for (int minute = 9 * 60; minute <= 10 * 60; minute += 10) {
            const float x = xAt(minute * 60.0);
            draw->AddLine(ImVec2(x, pricePlotTop), ImVec2(x, axisTop),
                IM_COL32(65, 72, 84, 115));
            char clock[16]{};
            FormatClock(minute * 60, clock, sizeof(clock));
            draw->AddText(ImVec2(x - 18.0f, axisTop + 7.0f),
                IM_COL32(175, 182, 195, 235), clock);
        }
        const float gateX = xAt(9 * 3600 + 3 * 60);
        draw->AddLine(ImVec2(gateX, pricePlotTop), ImVec2(gateX, axisTop),
            IM_COL32(255, 205, 90, 190), 1.3f);
        draw->AddText(ImVec2(gateX + 3.0f, pricePlotTop + 3.0f),
            IM_COL32(255, 210, 105, 230), "09:03 평가 시작");

        auto zeroLine = [&](const Pane& pane) {
            if (!pane.enabled) return;
            const float y = (pane.top + 18.0f + pane.bottom - 6.0f) * 0.5f;
            draw->AddLine(ImVec2(plotLeft, y), ImVec2(plotRight, y),
                IM_COL32(95, 105, 120, 110));
        };
        zeroLine(slope);
        zeroLine(macd);
        zeroLine(obv);

        std::map<int, int> ordinal;
        std::vector<DisplayBar> display;
        std::vector<ImVec2> fastLine;
        std::vector<ImVec2> slowLine;
        std::vector<ImVec2> tickLine;
        std::vector<ImVec2> macdLine;
        std::vector<ImVec2> obvLine;
        const std::size_t densityBase = (std::max<std::size_t>)(25U, visible.size());
        const float half = (std::max)(1.2f,
            (std::min)(5.0f, plotWidth / static_cast<float>(densityBase) * 0.28f));

        for (std::size_t i : visible) {
            const Bar& bar = member->bars[i];
            const intuitive::StrengthPoint& point = strength->points[i];
            const int minute = PackedTimeSeconds(bar.closeTimestampMs) / 60;
            const int count = (std::max)(1, minuteCounts[minute]);
            const int order = ordinal[minute]++;
            const double displaySecond = minute * 60.0 +
                (static_cast<double>(order + 1) / static_cast<double>(count + 1)) * 60.0;
            const float x = xAt(displaySecond);
            display.push_back(DisplayBar{i, x, minute, order, count});

            if (state.showBuyEligible && BuyEligible(point)) {
                draw->AddRectFilled(
                    ImVec2(x - half * 1.8f, pricePlotTop),
                    ImVec2(x + half * 1.8f, pricePlotBottom),
                    IM_COL32(45, 215, 120, 30));
            }
            if (state.showCandles) {
                const ImU32 color = CandleColor(bar);
                draw->AddLine(ImVec2(x, yPrice(bar.high)), ImVec2(x, yPrice(bar.low)), color);
                const float y1 = yPrice((std::max)(bar.open, bar.close));
                const float y2 = yPrice((std::min)(bar.open, bar.close));
                if (std::abs(y2 - y1) < 1.0f) {
                    draw->AddLine(ImVec2(x - half, y1), ImVec2(x + half, y1), color, 1.5f);
                }
                else {
                    draw->AddRectFilled(ImVec2(x - half, y1), ImVec2(x + half, y2), color);
                }
            }
            if (state.showFastJma) fastLine.emplace_back(x, yPrice(point.fastJma));
            if (state.showSlowJma) slowLine.emplace_back(x, yPrice(point.slowJma));

            if (state.showCrossSignals && point.crossUp) {
                const float y = yPrice(bar.low) + 5.0f;
                draw->AddTriangleFilled(ImVec2(x, y), ImVec2(x - 6.0f, y + 10.0f),
                    ImVec2(x + 6.0f, y + 10.0f), IM_COL32(60, 240, 135, 245));
            }
            else if (state.showCrossSignals && point.crossDown) {
                const float y = yPrice(bar.high) - 5.0f;
                draw->AddTriangleFilled(ImVec2(x, y), ImVec2(x - 6.0f, y - 10.0f),
                    ImVec2(x + 6.0f, y - 10.0f), IM_COL32(242, 85, 95, 245));
            }
            if (state.showBuyEligible && BuyEligible(point)) {
                const float y = (std::min)(pricePlotBottom - 5.0f, yPrice(bar.low) + 20.0f);
                draw->AddCircleFilled(ImVec2(x, y), 5.0f, IM_COL32(65, 245, 145, 255));
                draw->AddText(ImVec2(x + 6.0f, y - 8.0f), IM_COL32(85, 245, 155, 255), "B");
            }

            if (slope.enabled) {
                const float paneTop = slope.top + 18.0f;
                const float paneBottom = slope.bottom - 6.0f;
                const float zero = (paneTop + paneBottom) * 0.5f;
                const float y = SymmetricY(point.fastJmaSlopePercent, slopeScale,
                    paneTop, paneBottom);
                draw->AddRectFilled(ImVec2(x - 1.5f, (std::min)(y, zero)),
                    ImVec2(x + 1.5f, (std::max)(y, zero)),
                    point.fastJmaSlopePercent >= 0.0
                        ? IM_COL32(55, 210, 125, 205)
                        : IM_COL32(225, 78, 92, 205));
            }
            if (tick.enabled && point.tickAvailable && std::isfinite(point.tickRatePerMinute)) {
                const float paneTop = tick.top + 18.0f;
                const float paneBottom = tick.bottom - 6.0f;
                const double unit = (std::max)(0.0,
                    (std::min)(1.0, point.tickRatePerMinute / tickScale));
                tickLine.emplace_back(x,
                    paneBottom - static_cast<float>(unit) * (paneBottom - paneTop));
            }
            if (macd.enabled) {
                macdLine.emplace_back(x, SymmetricY(point.macdHistogramAtr, macdScale,
                    macd.top + 18.0f, macd.bottom - 6.0f));
            }
            if (obv.enabled) {
                obvLine.emplace_back(x, SymmetricY(point.obvImpulse, obvScale,
                    obv.top + 18.0f, obv.bottom - 6.0f));
            }
        }

        auto polyline = [&](const std::vector<ImVec2>& points, ImU32 color, float thickness) {
            if (points.size() < 2U) return;
            draw->AddPolyline(points.data(), static_cast<int>(points.size()), color, 0, thickness);
        };
        polyline(fastLine, IM_COL32(235, 70, 205, 250), 2.0f);
        polyline(slowLine, IM_COL32(190, 195, 205, 245), 1.7f);
        polyline(tickLine, IM_COL32(65, 205, 235, 245), 1.7f);
        polyline(macdLine, IM_COL32(255, 180, 80, 245), 1.6f);
        polyline(obvLine, IM_COL32(150, 220, 110, 245), 1.6f);

        if (hovered && !display.empty()) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const DisplayBar* nearest = &display.front();
            float distance = std::abs(mouse.x - nearest->x);
            for (const DisplayBar& candidate : display) {
                const float d = std::abs(mouse.x - candidate.x);
                if (d < distance) {
                    nearest = &candidate;
                    distance = d;
                }
            }
            const Bar& bar = member->bars[nearest->index];
            const intuitive::StrengthPoint& point = strength->points[nearest->index];
            draw->AddLine(ImVec2(nearest->x, pricePlotTop), ImVec2(nearest->x, axisTop),
                IM_COL32(230, 235, 245, 180));

            char clock[16]{};
            FormatClock(nearest->minute * 60, clock, sizeof(clock));
            const char* cross = point.crossUp ? "UP" : (point.crossDown ? "DOWN" : "-");
            ImGui::BeginTooltip();
            ImGui::Text("%s [분내 %d/%d] T%d", clock,
                nearest->ordinal + 1, nearest->count, tickSize);
            ImGui::TextDisabled("CYBOS HH:mm 계약 — 분내 초 위치는 생성하지 않음");
            ImGui::Separator();
            ImGui::Text("O %.0f H %.0f L %.0f C %.0f", bar.open, bar.high, bar.low, bar.close);
            ImGui::Text("JMA%d %.2f (%+.1f%%) | JMA%d %.2f (%+.1f%%)",
                config.fastJmaPeriod, point.fastJma, point.fastJmaSlopePercent,
                config.slowJmaPeriod, point.slowJma, point.slowJmaSlopePercent);
            ImGui::Text("교차 %s | cross %+.1f%% | age %d | wave %+.2f%%",
                cross, point.crossJmaSlopePercent, point.barsSinceCross,
                point.waveJmaGainPercent);
            ImGui::Text("평가 %s | bullish %s | fresh %s | BUY %s",
                point.inEvaluationWindow ? "Y" : "N",
                point.bullishRegime ? "Y" : "N",
                point.fresh ? "Y" : "N",
                BuyEligible(point) ? "Y" : "N");
            if (point.tickAvailable && std::isfinite(point.tickRatePerMinute)) {
                ImGui::Text("Tick %.0f/min | accel %.2fx",
                    point.tickRatePerMinute, point.tickAcceleration);
            }
            ImGui::Text("MACD/ATR %+.4f | OBV %+.4f | session %+.2f%%",
                point.macdHistogramAtr, point.obvImpulse, point.sessionReturnPercent);
            ImGui::EndTooltip();
        }

        ImGui::End();
        return changed;
    }
}
