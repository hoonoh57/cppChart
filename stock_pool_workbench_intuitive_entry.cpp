#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "core/intuitive_strength_engine.h"

// Keep the previous relative-strength workbench compiled as a Legacy fallback,
// but do not make it the application's entry point.
#define wWinMain StockPoolLegacyWinMain
#include "stock_pool_workbench_entry.cpp"
#undef wWinMain

namespace
{
    using trading::stock_pool::MemberSeries;
    using trading::stock_pool::intuitive::BuildStrengthSnapshot;
    using trading::stock_pool::intuitive::BuyStateName;
    using trading::stock_pool::intuitive::CalculateStrengthSeries;
    using trading::stock_pool::intuitive::MemberStrengthSeries;
    using trading::stock_pool::intuitive::StrengthConfig;
    using trading::stock_pool::intuitive::StrengthPoint;
    using trading::stock_pool::intuitive::StrengthRow;
    using trading::stock_pool::intuitive::StrengthSnapshot;

    struct IntuitiveUiState final
    {
        StrengthConfig config;
        std::vector<MemberStrengthSeries> series;
        StrengthSnapshot snapshot;
        int asOfIndex = -1;
        bool showLegacy = false;
        bool calculated = false;
    };

    IntuitiveUiState g_intuitive;

    int CommonPointCount()
    {
        if (g_intuitive.series.empty()) return 0;
        std::size_t common = static_cast<std::size_t>(-1);
        for (const auto& member : g_intuitive.series) {
            common = (std::min)(common, member.points.size());
        }
        return common == static_cast<std::size_t>(-1)
            ? 0
            : static_cast<int>(common);
    }

    void RebuildIntuitiveSnapshot()
    {
        const int common = CommonPointCount();
        if (common <= 0) {
            g_intuitive.snapshot = {};
            g_intuitive.asOfIndex = -1;
            return;
        }
        g_intuitive.asOfIndex = (std::max)(
            0,
            (std::min)(common - 1, g_intuitive.asOfIndex));
        g_intuitive.snapshot = BuildStrengthSnapshot(
            g_intuitive.series,
            static_cast<std::size_t>(g_intuitive.asOfIndex),
            g_intuitive.config);
    }

    bool EnsureIntuitiveSource()
    {
        if (g_state.sourceMode == SourceMode::Historical1516) {
            if (g_state.rawMembers.empty()) {
                g_state.status =
                    "1516 Frozen Cohort가 없습니다 — 먼저 1516 가져오기로 종목을 확정하십시오.";
                return false;
            }
            if (HistoricalMembersNeedHydration() && !HydrateHistoricalCohort()) {
                return false;
            }
        }
        else if (g_state.members.empty()) {
            LoadSource();
        }

        if (g_state.members.empty() && !g_state.rawMembers.empty()) {
            ApplyTimeframe();
        }
        return !g_state.members.empty();
    }

    bool CalculateIntuitiveStrength()
    {
        if (!EnsureIntuitiveSource()) return false;

        g_intuitive.series = CalculateStrengthSeries(
            g_state.members,
            g_intuitive.config);
        const int common = CommonPointCount();
        if (common <= 0) {
            g_state.status = "직관강도 계산 실패 — 계산 가능한 봉이 없습니다.";
            g_intuitive.calculated = false;
            return false;
        }

        g_intuitive.asOfIndex = common - 1;
        RebuildIntuitiveSnapshot();
        g_intuitive.calculated = true;

        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "직관강도 계산 완료: %zu종목 x %d봉 | JMA(%d,%d,%d) vs JMA(%d,%d,%d) | fresh <= %d봉 | Tick=N/A(실제 체결 adapter 대기)",
            g_intuitive.series.size(),
            common,
            g_intuitive.config.fastJmaPeriod,
            g_intuitive.config.jmaPhase,
            g_intuitive.config.jmaPower,
            g_intuitive.config.slowJmaPeriod,
            g_intuitive.config.jmaPhase,
            g_intuitive.config.jmaPower,
            g_intuitive.config.maxFreshBars);
        g_state.status = message;
        return true;
    }

    void FormatPackedTime(
        trading::stock_pool::EpochMillis timestamp,
        char* buffer,
        std::size_t bufferSize)
    {
        if (timestamp <= 0) {
            std::snprintf(buffer, bufferSize, "--:--");
            return;
        }
        const long long packed = static_cast<long long>(timestamp / 1000LL);
        const int hhmmss = static_cast<int>(packed % 1000000LL);
        const int hour = hhmmss / 10000;
        const int minute = (hhmmss / 100) % 100;
        std::snprintf(buffer, bufferSize, "%02d:%02d", hour, minute);
    }

    const MemberStrengthSeries* FindSeries(std::size_t memberIndex)
    {
        for (const auto& member : g_intuitive.series) {
            if (member.memberIndex == memberIndex) return &member;
        }
        return nullptr;
    }

    ImU32 PriceCandleColor(const trading::stock_pool::Bar& bar)
    {
        if (bar.close > bar.open) return IM_COL32(235, 72, 72, 230);
        if (bar.close < bar.open) return IM_COL32(55, 130, 225, 230);
        return IM_COL32(150, 150, 160, 210);
    }

    void DrawIntuitiveToolbar()
    {
        ImGui::BeginChild("##intuitive_toolbar", ImVec2(0.0f, 104.0f), true);

        static const char* sourceLabels[] = {
            "개발 fixture",
            "1516 과거 포착",
            "실시간 조건식"};
        int sourceIndex = static_cast<int>(g_state.sourceMode);
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::Combo(
                "데이터소스",
                &sourceIndex,
                sourceLabels,
                IM_ARRAYSIZE(sourceLabels)))
        {
            g_state.sourceMode = static_cast<SourceMode>(sourceIndex);
            g_intuitive.calculated = false;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputText("조건식", g_state.condition, sizeof(g_state.condition));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(105.0f);
        ImGui::InputText("일자", g_state.tradingDate, sizeof(g_state.tradingDate));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(65.0f);
        ImGui::InputText("포착", g_state.captureTime, sizeof(g_state.captureTime));

        ImGui::SameLine();
        static const int timeframes[] = {1, 3, 5, 10, 15};
        static const char* timeframeLabels[] = {"1분", "3분", "5분", "10분", "15분"};
        int timeframeIndex = 0;
        for (int index = 0; index < IM_ARRAYSIZE(timeframes); ++index) {
            if (timeframes[index] == g_state.timeframeMinutes) timeframeIndex = index;
        }
        ImGui::SetNextItemWidth(68.0f);
        if (ImGui::Combo(
                "봉",
                &timeframeIndex,
                timeframeLabels,
                IM_ARRAYSIZE(timeframeLabels)))
        {
            g_state.timeframeMinutes = timeframes[timeframeIndex];
            if (!g_state.rawMembers.empty() &&
                !HistoricalMembersNeedHydration())
            {
                ApplyTimeframe();
                if (g_intuitive.calculated) CalculateIntuitiveStrength();
            }
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.0f);
        if (ImGui::InputInt(
                "돌파유효봉",
                &g_intuitive.config.maxFreshBars,
                0,
                0))
        {
            g_intuitive.config.maxFreshBars = (std::max)(
                0,
                (std::min)(20, g_intuitive.config.maxFreshBars));
            if (g_intuitive.calculated) CalculateIntuitiveStrength();
        }

        ImGui::SameLine();
        if (ImGui::Button("1516 가져오기")) {
            g_state.sourceMode = SourceMode::Historical1516;
            g_historicalImport.requestOpen = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("직관강도 분석")) CalculateIntuitiveStrength();
        ImGui::SameLine();
        ImGui::Checkbox("Legacy 상대강도", &g_intuitive.showLegacy);

        if (g_intuitive.calculated) {
            const int common = CommonPointCount();
            int slider = g_intuitive.asOfIndex;
            ImGui::SetNextItemWidth(470.0f);
            if (ImGui::SliderInt(
                    "인과 시점",
                    &slider,
                    0,
                    (std::max)(0, common - 1)))
            {
                g_intuitive.asOfIndex = slider;
                RebuildIntuitiveSnapshot();
            }
            ImGui::SameLine();
            char time[16]{};
            FormatPackedTime(g_intuitive.snapshot.asOf, time, sizeof(time));
            ImGui::Text("%s", time);
        }

        ImGui::TextDisabled(
            "상태: %s | 순위는 percentile이 아니라 Fresh JMA 교차 당시 slope(%%)로 결정 | MACD/ATR·OBV는 원값 확인 | Tick은 실제 체결데이터 전까지 N/A",
            g_state.status.c_str());
        ImGui::EndChild();
    }

    void DrawIntuitiveGrid()
    {
        if (!g_intuitive.calculated || g_intuitive.snapshot.rows.empty()) {
            ImGui::TextWrapped(
                "1516 종목을 확정한 뒤 '직관강도 분석'을 누르십시오. 상대순위 점수 대신 JMA7/20 실제 교차강도와 파동을 표시합니다.");
            return;
        }

        ImGui::Text(
            "직관 우선순위 | 매수유효 %d개 / 전체 %zu개 | 돌파유효 <= %d봉",
            static_cast<int>(std::count_if(
                g_intuitive.snapshot.rows.begin(),
                g_intuitive.snapshot.rows.end(),
                [](const StrengthRow& row) { return row.buyEligible; })),
            g_intuitive.snapshot.rows.size(),
            g_intuitive.config.maxFreshBars);

        if (!ImGui::BeginTable(
                "##intuitive_strength_grid",
                11,
                ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_Resizable,
                ImVec2(0.0f, -1.0f)))
        {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("우선", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("종목", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("상태", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("JMA현재%", ImGuiTableColumnFlags_WidthFixed, 66.0f);
        ImGui::TableSetupColumn("돌파강도%", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("파동JMA%", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("경과", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("MACD/ATR", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("OBVimp", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("누적%", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("Tick/min", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableHeadersRow();

        for (const StrengthRow& row : g_intuitive.snapshot.rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (row.buyPriority > 0) ImGui::Text("%d", row.buyPriority);
            else ImGui::TextDisabled("-");

            ImGui::TableSetColumnIndex(1);
            const bool selected =
                static_cast<int>(row.memberIndex) == g_state.selectedMemberIndex;
            const std::string label =
                row.code + " " + row.name + "##intuitive_" + row.code;
            if (ImGui::Selectable(
                    label.c_str(),
                    selected,
                    ImGuiSelectableFlags_SpanAllColumns))
            {
                g_state.selectedMemberIndex = static_cast<int>(row.memberIndex);
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(BuyStateName(row));
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%+.3f", row.point.fastJmaSlopePercent);
            ImGui::TableSetColumnIndex(4);
            if (row.point.barsSinceCross >= 0)
                ImGui::Text("%+.3f", row.point.crossJmaSlopePercent);
            else
                ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(5);
            if (row.point.barsSinceCross >= 0)
                ImGui::Text("%+.2f", row.point.waveJmaGainPercent);
            else
                ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(6);
            if (row.point.barsSinceCross >= 0)
                ImGui::Text("%d", row.point.barsSinceCross);
            else
                ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%+.3f", row.point.macdHistogramAtr);
            ImGui::TableSetColumnIndex(8);
            ImGui::Text("%+.3f", row.point.obvImpulse);
            ImGui::TableSetColumnIndex(9);
            ImGui::Text("%+.2f", row.point.sessionReturnPercent);
            ImGui::TableSetColumnIndex(10);
            ImGui::TextDisabled("N/A");
        }
        ImGui::EndTable();
    }

    double GlobalSlopeScale()
    {
        double maximum = 0.0;
        if (g_intuitive.asOfIndex < 0) return 0.1;
        for (const auto& member : g_intuitive.series) {
            const std::size_t end = (std::min)(
                member.points.size(),
                static_cast<std::size_t>(g_intuitive.asOfIndex + 1));
            for (std::size_t index = 0U; index < end; ++index) {
                maximum = (std::max)(
                    maximum,
                    std::abs(member.points[index].fastJmaSlopePercent));
            }
        }
        return (std::max)(0.1, maximum);
    }

    void DrawMemberWysiwygStrip(
        const StrengthRow& row,
        const MemberStrengthSeries& strength,
        float height,
        double slopeScale)
    {
        if (row.memberIndex >= g_state.members.size() || strength.points.empty()) return;
        const MemberSeries& member = g_state.members[row.memberIndex];
        const std::size_t end = (std::min)(
            strength.points.size(),
            static_cast<std::size_t>((std::max)(0, g_intuitive.asOfIndex) + 1));
        if (end == 0U || member.bars.size() < end) return;

        const ImVec2 available = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton(
            ("##wysiwyg_" + row.code).c_str(),
            ImVec2((std::max)(available.x, 480.0f), height));
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        const bool selected =
            static_cast<int>(row.memberIndex) == g_state.selectedMemberIndex;
        draw->AddRectFilled(minimum, maximum, IM_COL32(12, 14, 18, 255));
        draw->AddRect(
            minimum,
            maximum,
            selected ? IM_COL32(255, 205, 90, 230) : IM_COL32(65, 70, 82, 210),
            0.0f,
            0,
            selected ? 2.0f : 1.0f);

        const float labelWidth = 190.0f;
        const float chartLeft = minimum.x + labelWidth;
        const float chartRight = maximum.x - 8.0f;
        const float priceTop = minimum.y + 24.0f;
        const float priceBottom = minimum.y + height * 0.69f;
        const float strengthTop = priceBottom + 5.0f;
        const float strengthBottom = maximum.y - 7.0f;
        const float strengthZero = (strengthTop + strengthBottom) * 0.5f;
        const float width = (std::max)(1.0f, chartRight - chartLeft);

        double priceMin = member.bars[0].low;
        double priceMax = member.bars[0].high;
        for (std::size_t index = 0U; index < end; ++index) {
            priceMin = (std::min)(priceMin, member.bars[index].low);
            priceMax = (std::max)(priceMax, member.bars[index].high);
            priceMin = (std::min)(priceMin, strength.points[index].fastJma);
            priceMin = (std::min)(priceMin, strength.points[index].slowJma);
            priceMax = (std::max)(priceMax, strength.points[index].fastJma);
            priceMax = (std::max)(priceMax, strength.points[index].slowJma);
        }
        const double priceRange = (std::max)(1.0e-9, priceMax - priceMin);

        const auto xAt = [&](std::size_t index) {
            return end <= 1U
                ? chartLeft
                : chartLeft +
                    static_cast<float>(index) /
                    static_cast<float>(end - 1U) * width;
        };
        const auto yPrice = [&](double price) {
            const double unit = (price - priceMin) / priceRange;
            return priceBottom -
                static_cast<float>(unit) * (priceBottom - priceTop);
        };

        // Fresh-entry windows are visible on the price chart. They are a gate,
        // not a score: old strong waves remain visible but lose buy eligibility.
        for (std::size_t index = 0U; index < end; ++index) {
            if (!strength.points[index].crossUp) continue;
            const std::size_t freshEnd = (std::min)(
                end - 1U,
                index + static_cast<std::size_t>(
                    (std::max)(0, g_intuitive.config.maxFreshBars)));
            draw->AddRectFilled(
                ImVec2(xAt(index), priceTop),
                ImVec2(xAt(freshEnd), strengthBottom),
                IM_COL32(42, 130, 82, 24));
        }

        std::vector<ImVec2> fastPoints;
        std::vector<ImVec2> slowPoints;
        fastPoints.reserve(end);
        slowPoints.reserve(end);
        const float candleWidth = (std::max)(1.0f, width /
            static_cast<float>((std::max)(std::size_t{1}, end)) * 0.55f);

        for (std::size_t index = 0U; index < end; ++index) {
            const auto& bar = member.bars[index];
            const auto& point = strength.points[index];
            const float x = xAt(index);
            const ImU32 candle = PriceCandleColor(bar);
            draw->AddLine(
                ImVec2(x, yPrice(bar.high)),
                ImVec2(x, yPrice(bar.low)),
                candle,
                1.0f);
            float openY = yPrice(bar.open);
            float closeY = yPrice(bar.close);
            if (std::abs(openY - closeY) < 1.0f) closeY = openY + 1.0f;
            draw->AddRectFilled(
                ImVec2(x - candleWidth * 0.5f, (std::min)(openY, closeY)),
                ImVec2(x + candleWidth * 0.5f, (std::max)(openY, closeY)),
                candle);

            fastPoints.emplace_back(x, yPrice(point.fastJma));
            slowPoints.emplace_back(x, yPrice(point.slowJma));

            const double normalized = (std::max)(
                -1.0,
                (std::min)(1.0, point.fastJmaSlopePercent / slopeScale));
            const float barY = strengthZero -
                static_cast<float>(normalized) *
                ((strengthBottom - strengthTop) * 0.46f);
            const ImU32 histogram = normalized >= 0.0
                ? IM_COL32(38, 190, 112, 190)
                : IM_COL32(210, 70, 80, 190);
            draw->AddRectFilled(
                ImVec2(x - candleWidth * 0.45f, (std::min)(barY, strengthZero)),
                ImVec2(x + candleWidth * 0.45f, (std::max)(barY, strengthZero)),
                histogram);

            if (point.crossUp) {
                draw->AddLine(
                    ImVec2(x, priceTop),
                    ImVec2(x, strengthBottom),
                    IM_COL32(70, 230, 135, 190),
                    1.2f);
            }
            else if (point.crossDown) {
                draw->AddLine(
                    ImVec2(x, priceTop),
                    ImVec2(x, strengthBottom),
                    IM_COL32(235, 80, 90, 160),
                    1.0f);
            }
        }

        if (fastPoints.size() >= 2U) {
            draw->AddPolyline(
                fastPoints.data(),
                static_cast<int>(fastPoints.size()),
                IM_COL32(220, 70, 205, 235),
                0,
                1.6f);
            draw->AddPolyline(
                slowPoints.data(),
                static_cast<int>(slowPoints.size()),
                IM_COL32(165, 170, 180, 210),
                0,
                1.4f);
        }
        draw->AddLine(
            ImVec2(chartLeft, strengthZero),
            ImVec2(chartRight, strengthZero),
            IM_COL32(120, 125, 135, 90),
            1.0f);

        char priority[24]{};
        if (row.buyPriority > 0)
            std::snprintf(priority, sizeof(priority), "BUY#%d", row.buyPriority);
        else
            std::snprintf(priority, sizeof(priority), "-");

        char line1[256]{};
        std::snprintf(
            line1,
            sizeof(line1),
            "%s %s | %s | %s",
            row.code.c_str(),
            row.name.c_str(),
            priority,
            BuyStateName(row));
        draw->AddText(
            ImVec2(minimum.x + 7.0f, minimum.y + 7.0f),
            row.buyEligible ? IM_COL32(80, 235, 145, 255) : IM_COL32(205, 205, 215, 235),
            line1);

        char line2[256]{};
        std::snprintf(
            line2,
            sizeof(line2),
            "JMA now %+0.3f%% | cross %+0.3f%% | wave %+0.2f%% | age %d",
            row.point.fastJmaSlopePercent,
            row.point.crossJmaSlopePercent,
            row.point.waveJmaGainPercent,
            row.point.barsSinceCross);
        draw->AddText(
            ImVec2(minimum.x + 7.0f, minimum.y + 29.0f),
            IM_COL32(190, 190, 205, 230),
            line2);

        char line3[256]{};
        std::snprintf(
            line3,
            sizeof(line3),
            "MACD/ATR %+0.3f | OBV %+0.3f | session %+0.2f%% | Tick N/A",
            row.point.macdHistogramAtr,
            row.point.obvImpulse,
            row.point.sessionReturnPercent);
        draw->AddText(
            ImVec2(minimum.x + 7.0f, minimum.y + 50.0f),
            IM_COL32(155, 160, 175, 220),
            line3);

        draw->AddText(
            ImVec2(chartLeft + 4.0f, strengthTop),
            IM_COL32(130, 140, 155, 210),
            "JMA7 slope histogram (same scale for every stock)");
    }

    void DrawIntuitiveMultiChart()
    {
        if (!g_intuitive.calculated || g_intuitive.snapshot.rows.empty()) {
            ImGui::TextDisabled("직관강도 분석 후 멀티차트가 표시됩니다.");
            return;
        }

        ImGui::TextWrapped(
            "WYSIWYG: 가격 candle + JMA7(자주) + JMA20(회색) + JMA7 slope histogram. 초록 세로선=엄격 상승교차, 붉은선=하락교차/reset, 옅은 초록 영역=신규매수 유효봉. 모든 종목의 slope histogram은 동일 scale입니다.");
        ImGui::TextColored(
            ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
            "Tick participation은 실제 체결건 데이터가 아직 연결되지 않아 N/A이며 거래량으로 대체하지 않습니다.");

        const double slopeScale = GlobalSlopeScale();
        ImGui::TextDisabled("공통 JMA slope 시각 scale: +/- %.3f%% / 봉", slopeScale);

        ImGui::BeginChild(
            "##intuitive_multi_scroll",
            ImVec2(0.0f, 0.0f),
            false,
            ImGuiWindowFlags_HorizontalScrollbar);
        for (const StrengthRow& row : g_intuitive.snapshot.rows) {
            const MemberStrengthSeries* strength = FindSeries(row.memberIndex);
            if (strength == nullptr) continue;
            DrawMemberWysiwygStrip(row, *strength, 132.0f, slopeScale);
            if (ImGui::IsItemClicked()) {
                g_state.selectedMemberIndex = static_cast<int>(row.memberIndex);
            }
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
        }
        ImGui::EndChild();
    }

    void DrawIntuitiveWorkbench()
    {
        DrawIntuitiveToolbar();
        if (g_intuitive.showLegacy) {
            DrawWorkbench();
            return;
        }

        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::BeginChild(
            "직관 우선순위",
            ImVec2(width * 0.39f, 0.0f),
            true);
        DrawIntuitiveGrid();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild(
            "가격 + 직관강도 멀티차트",
            ImVec2(0.0f, 0.0f),
            true);
        DrawIntuitiveMultiChart();
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
        L"StockPoolIntuitiveWorkbench",
        nullptr};
    RegisterClassExW(&windowClass);
    HWND window = CreateWindowW(
        windowClass.lpszClassName,
        L"종목풀 분석 워크벤치 — WYSIWYG 직관강도 / JMA 파동",
        WS_OVERLAPPEDWINDOW,
        30,
        25,
        1780,
        1040,
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
    io.IniFilename = "stock_pool_intuitive_workbench_layout.ini";
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 2.0f;
    style.WindowPadding = ImVec2(6.0f, 6.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);

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
    g_state.timeframeMinutes = 10;
    g_state.status =
        "WYSIWYG 직관강도 대기 — 1516 가져오기 후 직관강도 분석";

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
            "##stock_pool_intuitive_host",
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus);
        DrawIntuitiveWorkbench();
        ImGui::End();

        // The existing wrapper owns the 1516 modal and then calls ImGui::Render().
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
