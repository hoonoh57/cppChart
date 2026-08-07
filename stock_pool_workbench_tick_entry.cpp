#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "core/intuitive_strength_engine.h"
#include "platform/stock_pool_tick_client.h"

#define wWinMain StockPoolLegacyWinMain
#include "stock_pool_workbench_entry.cpp"
#undef wWinMain

namespace
{
    using trading::stock_pool::Bar;
    using trading::stock_pool::EpochMillis;
    using trading::stock_pool::MemberSeries;
    using trading::stock_pool::intuitive::BuildStrengthSnapshotAtTime;
    using trading::stock_pool::intuitive::BuyStateName;
    using trading::stock_pool::intuitive::CalculateStrengthSeries;
    using trading::stock_pool::intuitive::MemberStrengthSeries;
    using trading::stock_pool::intuitive::StrengthConfig;
    using trading::stock_pool::intuitive::StrengthPoint;
    using trading::stock_pool::intuitive::StrengthRow;
    using trading::stock_pool::intuitive::StrengthSnapshot;

    struct TickWorkbenchState final
    {
        StrengthConfig config;
        std::vector<MemberSeries> members;
        std::vector<MemberStrengthSeries> series;
        StrengthSnapshot snapshot;
        int tickSize = 360;
        int warmupBars = 160;
        int asOfMinute = 10 * 60;
        bool calculated = false;
        bool showLegacy = false;
        std::string previousTradingDate;
    };

    TickWorkbenchState g_tick;

    std::string DigitsOnly(const char* text)
    {
        std::string result;
        if (text == nullptr) return result;
        for (const unsigned char* p =
                 reinterpret_cast<const unsigned char*>(text);
             *p != 0;
             ++p)
        {
            if (*p >= '0' && *p <= '9') result.push_back(static_cast<char>(*p));
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

    EpochMillis MakePackedTimestamp(
        const std::string& date,
        int secondOfDay)
    {
        if (date.size() != 8U) return 0;
        const int hour = secondOfDay / 3600;
        const int minute = (secondOfDay / 60) % 60;
        const int second = secondOfDay % 60;
        char buffer[32]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%s%02d%02d%02d",
            date.c_str(),
            hour,
            minute,
            second);
        return static_cast<EpochMillis>(std::strtoll(buffer, nullptr, 10)) * 1000LL;
    }

    void FormatMinute(int minuteOfDay, char* buffer, std::size_t size)
    {
        std::snprintf(
            buffer,
            size,
            "%02d:%02d 완료",
            minuteOfDay / 60,
            minuteOfDay % 60);
    }

    const MemberStrengthSeries* FindStrength(std::size_t memberIndex)
    {
        for (const auto& series : g_tick.series) {
            if (series.memberIndex == memberIndex) return &series;
        }
        return nullptr;
    }

    void RebuildSnapshot()
    {
        if (!g_tick.calculated) {
            g_tick.snapshot = {};
            return;
        }
        const std::string date = DigitsOnly(g_state.tradingDate);
        if (date.size() != 8U) return;
        g_tick.snapshot = BuildStrengthSnapshotAtTime(
            g_tick.series,
            MakePackedTimestamp(date, g_tick.asOfMinute * 60 + 59),
            g_tick.config);
    }

    bool LoadTickStrength()
    {
        if (g_state.rawMembers.empty()) {
            g_state.status =
                "틱강도 분석 거부 — 먼저 1516 Frozen Cohort를 확정하십시오.";
            return false;
        }

        const std::string date = DigitsOnly(g_state.tradingDate);
        if (date.size() != 8U) {
            g_state.status = "틱강도 분석 일자는 YYYY-MM-DD 형식이어야 합니다.";
            return false;
        }

        g_tick.members.clear();
        g_tick.series.clear();
        g_tick.previousTradingDate.clear();
        g_tick.calculated = false;
        g_state.status =
            "server32 실제 CYBOS T" + std::to_string(g_tick.tickSize) +
            " 적재 중 — 전일 warm-up + 당일 09:00~10:00";

        for (const MemberSeries& cohort : g_state.rawMembers) {
            const auto fetched =
                trading::stock_pool::platform::FetchTickSeriesViaServer32(
                    cohort.code,
                    date,
                    g_tick.tickSize,
                    "100000",
                    static_cast<std::size_t>((std::max)(40, g_tick.warmupBars)),
                    ".env");
            if (!fetched.ok) {
                g_state.status = "틱봉 적재 실패 — " + fetched.error;
                g_tick.members.clear();
                return false;
            }

            MemberSeries member;
            member.code = cohort.code;
            member.name = cohort.name;
            member.market = cohort.market;
            member.bars = fetched.bars;
            g_tick.members.push_back(std::move(member));
            if (g_tick.previousTradingDate.empty()) {
                g_tick.previousTradingDate = fetched.previousTradingDate;
            }
        }

        g_tick.config.sessionStart = MakePackedTimestamp(date, 9 * 3600);
        g_tick.config.evaluationStart = MakePackedTimestamp(
            date, 9 * 3600 + 3 * 60);
        g_tick.config.evaluationEnd = MakePackedTimestamp(date, 10 * 3600);
        g_tick.series = CalculateStrengthSeries(g_tick.members, g_tick.config);
        g_tick.asOfMinute = 10 * 60;
        g_tick.calculated = true;
        RebuildSnapshot();

        g_state.status =
            "실제 T" + std::to_string(g_tick.tickSize) +
            " 준비 완료 | 전일 " + g_tick.previousTradingDate +
            " warm-up " + std::to_string(g_tick.warmupBars) +
            "봉 | 지표연속 | 파동 09:00 reset | 평가 09:03~10:00";
        return true;
    }

    void DrawTickToolbar()
    {
        ImGui::BeginChild("##tick_toolbar", ImVec2(0.0f, 118.0f), true);

        ImGui::SetNextItemWidth(118.0f);
        ImGui::InputText("일자", g_state.tradingDate, sizeof(g_state.tradingDate));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(135.0f);
        ImGui::InputText("조건식", g_state.condition, sizeof(g_state.condition));

        static const int tickSizes[] = {60, 120, 180, 360, 720};
        static const char* tickLabels[] = {
            "60틱", "120틱", "180틱", "360틱", "720틱"};
        int tickIndex = 3;
        for (int index = 0; index < IM_ARRAYSIZE(tickSizes); ++index) {
            if (g_tick.tickSize == tickSizes[index]) tickIndex = index;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(76.0f);
        if (ImGui::Combo(
                "틱캔들",
                &tickIndex,
                tickLabels,
                IM_ARRAYSIZE(tickLabels)))
        {
            g_tick.tickSize = tickSizes[tickIndex];
            g_tick.calculated = false;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(64.0f);
        if (ImGui::InputInt("전일 warm-up봉", &g_tick.warmupBars, 0, 0)) {
            g_tick.warmupBars =
                (std::max)(40, (std::min)(600, g_tick.warmupBars));
            g_tick.calculated = false;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(54.0f);
        if (ImGui::InputInt("돌파유효봉", &g_tick.config.maxFreshBars, 0, 0)) {
            g_tick.config.maxFreshBars =
                (std::max)(0, (std::min)(30, g_tick.config.maxFreshBars));
            if (g_tick.calculated) {
                g_tick.series = CalculateStrengthSeries(
                    g_tick.members,
                    g_tick.config);
                RebuildSnapshot();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("1516 가져오기")) {
            g_state.sourceMode = SourceMode::Historical1516;
            g_historicalImport.requestOpen = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("전일Warm-up + 틱강도 분석")) LoadTickStrength();
        ImGui::SameLine();
        ImGui::Checkbox("Legacy", &g_tick.showLegacy);

        if (g_tick.calculated) {
            int minute = g_tick.asOfMinute;
            ImGui::SetNextItemWidth(620.0f);
            if (ImGui::SliderInt(
                    "인과 분마감",
                    &minute,
                    9 * 60 + 3,
                    10 * 60,
                    ""))
            {
                g_tick.asOfMinute = minute;
                RebuildSnapshot();
            }
            ImGui::SameLine();
            char clock[20]{};
            FormatMinute(g_tick.asOfMinute, clock, sizeof(clock));
            ImGui::Text("%s", clock);
        }

        ImGui::TextDisabled(
            "상태: %s | 전일봉=지표 warm-up 전용 | 순위=분 마감 기준 | Tn 봉 순서는 보존",
            g_state.status.c_str());
        ImGui::EndChild();
    }

    void DrawGrid()
    {
        if (!g_tick.calculated || g_tick.snapshot.rows.empty()) {
            ImGui::TextWrapped(
                "1516 종목 확정 후 실제 틱강도 분석을 실행하십시오. 당일 09:00부터 지표를 새로 초기화하지 않습니다.");
            return;
        }

        const int eligible = static_cast<int>(std::count_if(
            g_tick.snapshot.rows.begin(),
            g_tick.snapshot.rows.end(),
            [](const StrengthRow& row) { return row.buyEligible; }));
        ImGui::Text(
            "매수유효 %d / %zu | T%d | 전일 %s warm-up | 09:03~10:00",
            eligible,
            g_tick.snapshot.rows.size(),
            g_tick.tickSize,
            g_tick.previousTradingDate.c_str());

        if (!ImGui::BeginTable(
                "##tick_strength_grid",
                12,
                ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_Resizable,
                ImVec2(0.0f, -1.0f)))
        {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("우선", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("종목", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("상태", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("JMA현재%", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("돌파강도%", ImGuiTableColumnFlags_WidthFixed, 68.0f);
        ImGui::TableSetupColumn("경과", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("틱/분", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableSetupColumn("틱가속", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("파동JMA%", ImGuiTableColumnFlags_WidthFixed, 66.0f);
        ImGui::TableSetupColumn("MACD/ATR", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("OBVimp", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("누적%", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableHeadersRow();

        for (const StrengthRow& row : g_tick.snapshot.rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (row.buyPriority > 0) ImGui::Text("%d", row.buyPriority);
            else ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s %s", row.code.c_str(), row.name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(BuyStateName(row));
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%+.1f", row.point.fastJmaSlopePercent);
            ImGui::TableSetColumnIndex(4);
            if (row.point.barsSinceCross >= 0)
                ImGui::Text("%+.1f", row.point.crossJmaSlopePercent);
            else
                ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(5);
            if (row.point.barsSinceCross >= 0)
                ImGui::Text("%d", row.point.barsSinceCross);
            else
                ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(6);
            if (row.point.tickAvailable)
                ImGui::Text("%.0f", row.point.tickRatePerMinute);
            else
                ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(7);
            if (row.point.tickAvailable)
                ImGui::Text("%.2fx", row.point.tickAcceleration);
            else
                ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(8);
            ImGui::Text("%+.2f", row.point.waveJmaGainPercent);
            ImGui::TableSetColumnIndex(9);
            ImGui::Text("%+.3f", row.point.macdHistogramAtr);
            ImGui::TableSetColumnIndex(10);
            ImGui::Text("%+.3f", row.point.obvImpulse);
            ImGui::TableSetColumnIndex(11);
            ImGui::Text("%+.2f", row.point.sessionReturnPercent);
        }
        ImGui::EndTable();
    }

    double GlobalSlopeScale()
    {
        double scale = 0.1;
        for (const auto& series : g_tick.series) {
            for (const StrengthPoint& point : series.points) {
                if (!point.inSession || point.asOf > g_tick.config.evaluationEnd) continue;
                scale = (std::max)(scale, std::abs(point.fastJmaSlopePercent));
            }
        }
        return scale;
    }

    double GlobalTickRateScale()
    {
        double scale = 1.0;
        for (const auto& series : g_tick.series) {
            for (const StrengthPoint& point : series.points) {
                if (!point.inSession || point.asOf > g_tick.config.evaluationEnd) continue;
                if (point.tickAvailable) {
                    scale = (std::max)(scale, point.tickRatePerMinute);
                }
            }
        }
        return scale;
    }

    ImU32 CandleColor(const Bar& bar)
    {
        if (bar.close > bar.open) return IM_COL32(235, 72, 72, 230);
        if (bar.close < bar.open) return IM_COL32(55, 130, 225, 230);
        return IM_COL32(150, 150, 160, 210);
    }

    void DrawMemberStrip(
        const StrengthRow& row,
        const MemberStrengthSeries& strength,
        double slopeScale,
        double tickScale)
    {
        if (row.memberIndex >= g_tick.members.size()) return;
        const MemberSeries& member = g_tick.members[row.memberIndex];
        const std::string date = DigitsOnly(g_state.tradingDate);
        const int startSecond = 9 * 3600;
        const int endSecond = 10 * 3600;
        const int currentSecond = g_tick.asOfMinute * 60 + 59;

        ImGui::InvisibleButton(
            ("##tick_strip_" + row.code).c_str(),
            ImVec2((std::max)(620.0f, ImGui::GetContentRegionAvail().x), 160.0f));
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(minimum, maximum, IM_COL32(12, 14, 18, 255));
        draw->AddRect(minimum, maximum, IM_COL32(60, 66, 78, 220));

        const float labelWidth = 210.0f;
        const float left = minimum.x + labelWidth;
        const float right = maximum.x - 8.0f;
        const float priceTop = minimum.y + 22.0f;
        const float priceBottom = minimum.y + 99.0f;
        const float strengthTop = priceBottom + 3.0f;
        const float strengthBottom = maximum.y - 6.0f;
        const float strengthZero = strengthTop +
            (strengthBottom - strengthTop) * 0.50f;
        const float width = (std::max)(1.0f, right - left);

        const auto xTime = [&](double second) {
            const double unit = (second - static_cast<double>(startSecond)) /
                static_cast<double>(endSecond - startSecond);
            return left + static_cast<float>(
                (std::max)(0.0, (std::min)(1.0, unit))) * width;
        };

        std::vector<std::size_t> visible;
        std::map<int, int> barsPerMinute;
        for (std::size_t index = 0U; index < member.bars.size(); ++index) {
            const Bar& bar = member.bars[index];
            if (PackedDate(bar.closeTimestampMs) != date) continue;
            const int second = PackedTimeSeconds(bar.closeTimestampMs);
            if (second < startSecond || second > currentSecond) continue;
            visible.push_back(index);
            ++barsPerMinute[second / 60];
        }
        if (visible.empty()) return;

        double priceMin = member.bars[visible.front()].low;
        double priceMax = member.bars[visible.front()].high;
        for (std::size_t index : visible) {
            priceMin = (std::min)(priceMin, member.bars[index].low);
            priceMax = (std::max)(priceMax, member.bars[index].high);
            priceMin = (std::min)(priceMin, strength.points[index].fastJma);
            priceMin = (std::min)(priceMin, strength.points[index].slowJma);
            priceMax = (std::max)(priceMax, strength.points[index].fastJma);
            priceMax = (std::max)(priceMax, strength.points[index].slowJma);
        }
        const double priceRange = (std::max)(1.0e-9, priceMax - priceMin);
        const auto yPrice = [&](double price) {
            return priceBottom - static_cast<float>(
                (price - priceMin) / priceRange) * (priceBottom - priceTop);
        };

        draw->AddLine(
            ImVec2(xTime(9 * 3600 + 3 * 60), priceTop),
            ImVec2(xTime(9 * 3600 + 3 * 60), strengthBottom),
            IM_COL32(255, 205, 90, 160),
            1.2f);

        std::map<int, int> ordinalInMinute;
        std::vector<ImVec2> fast;
        std::vector<ImVec2> slow;
        std::vector<ImVec2> tickLine;
        for (std::size_t index : visible) {
            const Bar& bar = member.bars[index];
            const StrengthPoint& point = strength.points[index];
            const int rawSecond = PackedTimeSeconds(bar.closeTimestampMs);
            const int minuteKey = rawSecond / 60;
            const int count = (std::max)(1, barsPerMinute[minuteKey]);
            const int ordinal = ordinalInMinute[minuteKey]++;
            const double displaySecond =
                static_cast<double>(minuteKey * 60) +
                (static_cast<double>(ordinal + 1) /
                 static_cast<double>(count + 1)) * 60.0;
            const float x = xTime(displaySecond);
            const float candleHalf = (std::max)(0.8f, (std::min)(2.4f, width / 900.0f));
            const ImU32 color = CandleColor(bar);

            draw->AddLine(
                ImVec2(x, yPrice(bar.high)),
                ImVec2(x, yPrice(bar.low)),
                color,
                1.0f);
            draw->AddRectFilled(
                ImVec2(x - candleHalf, yPrice((std::max)(bar.open, bar.close))),
                ImVec2(x + candleHalf, yPrice((std::min)(bar.open, bar.close))),
                color);

            fast.emplace_back(x, yPrice(point.fastJma));
            slow.emplace_back(x, yPrice(point.slowJma));

            const double normalizedSlope = (std::max)(
                -1.0,
                (std::min)(1.0, point.fastJmaSlopePercent / slopeScale));
            const float slopeY = strengthZero -
                static_cast<float>(normalizedSlope) *
                (strengthBottom - strengthTop) * 0.43f;
            draw->AddRectFilled(
                ImVec2(x - 1.0f, (std::min)(slopeY, strengthZero)),
                ImVec2(x + 1.0f, (std::max)(slopeY, strengthZero)),
                normalizedSlope >= 0.0
                    ? IM_COL32(45, 205, 120, 190)
                    : IM_COL32(220, 75, 85, 190));

            if (point.tickAvailable) {
                const double tickUnit = (std::max)(
                    0.0,
                    (std::min)(1.0, point.tickRatePerMinute / tickScale));
                const float tickY = strengthBottom -
                    static_cast<float>(tickUnit) *
                    (strengthBottom - strengthTop) * 0.92f;
                tickLine.emplace_back(x, tickY);
            }

            if (point.crossUp) {
                draw->AddLine(
                    ImVec2(x, priceTop),
                    ImVec2(x, strengthBottom),
                    IM_COL32(70, 235, 135, 190),
                    1.3f);
            }
            else if (point.crossDown) {
                draw->AddLine(
                    ImVec2(x, priceTop),
                    ImVec2(x, strengthBottom),
                    IM_COL32(240, 80, 90, 170),
                    1.0f);
            }
        }

        if (fast.size() > 1U) {
            draw->AddPolyline(
                fast.data(),
                static_cast<int>(fast.size()),
                IM_COL32(225, 70, 205, 235),
                0,
                1.5f);
            draw->AddPolyline(
                slow.data(),
                static_cast<int>(slow.size()),
                IM_COL32(180, 185, 195, 220),
                0,
                1.4f);
        }
        if (tickLine.size() > 1U) {
            draw->AddPolyline(
                tickLine.data(),
                static_cast<int>(tickLine.size()),
                IM_COL32(65, 205, 235, 235),
                0,
                1.5f);
        }
        draw->AddLine(
            ImVec2(left, strengthZero),
            ImVec2(right, strengthZero),
            IM_COL32(110, 115, 125, 90));

        char line1[256]{};
        std::snprintf(
            line1,
            sizeof(line1),
            "%s %s | %s | BUY#%d",
            row.code.c_str(),
            row.name.c_str(),
            BuyStateName(row),
            row.buyPriority);
        draw->AddText(
            ImVec2(minimum.x + 6.0f, minimum.y + 7.0f),
            row.buyEligible
                ? IM_COL32(75, 235, 145, 255)
                : IM_COL32(205, 205, 215, 240),
            line1);

        char line2[256]{};
        std::snprintf(
            line2,
            sizeof(line2),
            "JMA %+0.1f%% | cross %+0.1f%% | age %d | session %+0.2f%%",
            row.point.fastJmaSlopePercent,
            row.point.crossJmaSlopePercent,
            row.point.barsSinceCross,
            row.point.sessionReturnPercent);
        draw->AddText(
            ImVec2(minimum.x + 6.0f, minimum.y + 29.0f),
            IM_COL32(190, 195, 205, 230),
            line2);

        char line3[256]{};
        if (row.point.tickAvailable) {
            std::snprintf(
                line3,
                sizeof(line3),
                "Tick %.0f/min | accel %.2fx | MACD/ATR %+0.3f | OBV %+0.3f",
                row.point.tickRatePerMinute,
                row.point.tickAcceleration,
                row.point.macdHistogramAtr,
                row.point.obvImpulse);
        }
        else {
            std::snprintf(
                line3,
                sizeof(line3),
                "Tick - | MACD/ATR %+0.3f | OBV %+0.3f",
                row.point.macdHistogramAtr,
                row.point.obvImpulse);
        }
        draw->AddText(
            ImVec2(minimum.x + 6.0f, minimum.y + 50.0f),
            IM_COL32(150, 165, 180, 230),
            line3);
        draw->AddText(
            ImVec2(left + 4.0f, strengthTop),
            IM_COL32(120, 145, 165, 210),
            "JMA slope histogram + raw Tick/min (공통 scale)");
    }

    void DrawCharts()
    {
        if (!g_tick.calculated || g_tick.snapshot.rows.empty()) {
            ImGui::TextDisabled("틱강도 분석 후 09:00~10:00 멀티차트가 표시됩니다.");
            return;
        }
        const double slopeScale = GlobalSlopeScale();
        const double tickScale = GlobalTickRateScale();
        ImGui::TextWrapped(
            "X축=09:00~10:00 실제 분 단위. 같은 분의 여러 Tn봉은 CYBOS 반환순서를 보존해 분 내부에 균등 배치합니다. 밀도와 순서는 실제지만 분내 초 위치는 시각화용입니다. 노란선=09:03 평가 시작, 청록선=실제 분당 틱수.");
        ImGui::TextDisabled(
            "모든 종목 공통 scale: JMA slope +/- %.1f%%/bar | Tick %.0f/min",
            slopeScale,
            tickScale);

        ImGui::BeginChild("##tick_chart_scroll", ImVec2(0.0f, 0.0f), false);
        for (const StrengthRow& row : g_tick.snapshot.rows) {
            const MemberStrengthSeries* strength = FindStrength(row.memberIndex);
            if (strength == nullptr) continue;
            DrawMemberStrip(row, *strength, slopeScale, tickScale);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
        }
        ImGui::EndChild();
    }

    void DrawWorkbenchTick()
    {
        DrawTickToolbar();
        if (g_tick.showLegacy) {
            DrawWorkbench();
            return;
        }
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::BeginChild(
            "틱강도 우선순위",
            ImVec2(width * 0.38f, 0.0f),
            true);
        DrawGrid();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild(
            "실제밀도 멀티 틱차트",
            ImVec2(0.0f, 0.0f),
            true);
        DrawCharts();
        ImGui::EndChild();
    }
}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW windowClass = {
        sizeof(windowClass),
        CS_CLASSDC,
        WindowProcedure,
        0,
        0,
        instance,
        nullptr,
        LoadCursorW(nullptr, IDC_ARROW),
        nullptr,
        nullptr,
        L"StockPoolTickStrengthWorkbench",
        nullptr};
    RegisterClassExW(&windowClass);
    HWND window = CreateWindowW(
        windowClass.lpszClassName,
        L"종목풀 분석 워크벤치 — 전일 Warm-up / 장초반 Tick WYSIWYG 강도",
        WS_OVERLAPPEDWINDOW,
        25,
        20,
        1820,
        1080,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!CreateDeviceD3D(window)) {
        CleanupDeviceD3D();
        UnregisterClassW(windowClass.lpszClassName, instance);
        return 1;
    }
    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = "stock_pool_tick_strength_layout.ini";
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 2.0f;
    style.WindowPadding = ImVec2(6.0f, 6.0f);

    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(g_device, g_context);
    {
        ImFontConfig configuration;
        configuration.OversampleH = 2;
        configuration.OversampleV = 1;
        const char* candidates[] = {
            "C:\\Windows\\Fonts\\malgun.ttf",
            "C:\\Windows\\Fonts\\gulim.ttc"};
        for (const char* candidate : candidates) {
            if (GetFileAttributesA(candidate) == INVALID_FILE_ATTRIBUTES) continue;
#if IMGUI_VERSION_NUM < 19200
            if (io.Fonts->AddFontFromFileTTF(
                    candidate,
                    16.0f,
                    &configuration,
                    GetKoreanRanges(io)) != nullptr)
#else
            if (io.Fonts->AddFontFromFileTTF(
                    candidate,
                    16.0f,
                    &configuration) != nullptr)
#endif
            {
                break;
            }
        }
    }
    ImGui_ImplDX11_CreateDeviceObjects();

    g_state.sourceMode = SourceMode::Historical1516;
    g_tick.config.maxFreshBars = 3;
    g_state.status =
        "전일 warm-up + 실제 틱강도 대기 — 1516 가져오기 후 분석";

    bool running = true;
    while (running) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) running = false;
        }
        if (!running) break;

        if (g_resizeWidth != 0U && g_resizeHeight != 0U) {
            if (g_renderTarget != nullptr) {
                g_renderTarget->Release();
                g_renderTarget = nullptr;
            }
            g_swapChain->ResizeBuffers(
                0,
                g_resizeWidth,
                g_resizeHeight,
                DXGI_FORMAT_UNKNOWN,
                0);
            g_resizeWidth = 0U;
            g_resizeHeight = 0U;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::Begin(
            "##stock_pool_tick_host",
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus);
        DrawWorkbenchTick();
        ImGui::End();

        ImGui::StockPoolRender();
        const float clearColor[4] = {0.05f, 0.055f, 0.065f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
        g_context->ClearRenderTargetView(g_renderTarget, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
        g_swapChain->Present(1, 0);

        MsgWaitForMultipleObjectsEx(
            0,
            nullptr,
            80,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);
    }

    ImGui_ImplDX11_InvalidateDeviceObjects();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, instance);
    return 0;
}