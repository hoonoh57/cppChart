#include <windows.h>
#include <d3d11.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "core/stock_pool_engine.h"
#include "app/stock_pool_evaluator.h"
#include "app/stock_pool_fixture.h"

namespace
{
    using trading::stock_pool::BacktestResult;
    using trading::stock_pool::Bar;
    using trading::stock_pool::LeaderState;
    using trading::stock_pool::MemberSeries;
    using trading::stock_pool::RankRow;
    using trading::stock_pool::RankingEngine;
    using trading::stock_pool::RankingSnapshot;
    using trading::stock_pool::ScoringProfile;
    using trading::stock_pool::TimeRegime;
    using trading::stock_pool::evaluation::EvaluationReport;

    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    IDXGISwapChain* g_swapChain = nullptr;
    ID3D11RenderTargetView* g_renderTarget = nullptr;
    UINT g_resizeWidth = 0U;
    UINT g_resizeHeight = 0U;

    enum class SourceMode
    {
        Fixture,
        Historical1516,
        LiveCondition
    };

    struct WorkbenchState final
    {
        SourceMode sourceMode = SourceMode::Fixture;
        char condition[64] = "다량어";
        char tradingDate[16] = "2026-08-06";
        char captureTime[8] = "09:00";
        int timeframeMinutes = 1;
        int topM = 2;
        int replaySpeed = 10;
        bool playing = false;
        double playAccumulator = 0.0;
        std::size_t nextBarIndex = 0U;
        int selectedMemberIndex = 0;
        std::string status;
        std::vector<MemberSeries> rawMembers;
        std::vector<MemberSeries> members;
        RankingEngine replayEngine;
        std::vector<RankingSnapshot> replayHistory;
        BacktestResult backtest;
        EvaluationReport evaluation;
        ScoringProfile profile;
    };

    WorkbenchState g_state;

    extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
        HWND,
        UINT,
        WPARAM,
        LPARAM);

    const RankRow* FindRow(
        const RankingSnapshot& snapshot,
        const std::string& code)
    {
        for (const RankRow& row : snapshot.rows) {
            if (row.code == code) return &row;
        }
        return nullptr;
    }

    const RankingSnapshot* CurrentSnapshot()
    {
        return g_state.replayHistory.empty()
            ? nullptr
            : &g_state.replayHistory.back();
    }

    int MaximumBarCount()
    {
        std::size_t maximum = 0U;
        for (const MemberSeries& member : g_state.members) {
            maximum = (std::max)(maximum, member.bars.size());
        }
        return static_cast<int>(maximum);
    }

    void FormatTime(int barIndex, char* buffer, std::size_t bufferSize)
    {
        const int totalMinutes =
            9 * 60 + barIndex * g_state.timeframeMinutes;
        const int hour = totalMinutes / 60;
        const int minute = totalMinutes % 60;
        std::snprintf(buffer, bufferSize, "%02d:%02d", hour, minute);
    }

    std::vector<MemberSeries> AggregateMembers(
        const std::vector<MemberSeries>& source,
        int timeframeMinutes)
    {
        timeframeMinutes = (std::max)(1, timeframeMinutes);
        if (timeframeMinutes == 1) return source;

        std::vector<MemberSeries> result;
        result.reserve(source.size());
        for (const MemberSeries& member : source) {
            MemberSeries aggregated;
            aggregated.code = member.code;
            aggregated.name = member.name;
            aggregated.market = member.market;
            for (std::size_t start = 0U;
                 start < member.bars.size();
                 start += static_cast<std::size_t>(timeframeMinutes))
            {
                const std::size_t end = (std::min)(
                    member.bars.size(),
                    start + static_cast<std::size_t>(timeframeMinutes));
                if (end <= start) continue;
                Bar bar = member.bars[start];
                double intensityTotal = 0.0;
                for (std::size_t index = start; index < end; ++index) {
                    const Bar& input = member.bars[index];
                    bar.high = (std::max)(bar.high, input.high);
                    bar.low = (std::min)(bar.low, input.low);
                    bar.close = input.close;
                    bar.closeTimestampMs = input.closeTimestampMs;
                    bar.cumulativeTurnover = input.cumulativeTurnover;
                    intensityTotal += input.tradeIntensity;
                }
                bar.tradeIntensity =
                    intensityTotal / static_cast<double>(end - start);
                aggregated.bars.push_back(bar);
            }
            result.push_back(std::move(aggregated));
        }
        return result;
    }

    void ResetReplay()
    {
        g_state.playing = false;
        g_state.playAccumulator = 0.0;
        g_state.replayEngine.Reset();
        g_state.replayHistory.clear();
        g_state.nextBarIndex = g_state.members.empty()
            ? 0U
            : static_cast<std::size_t>(
                (std::max)(1, g_state.profile.minimumHistoryBars) - 1);
    }

    void ApplyTimeframe()
    {
        g_state.members = AggregateMembers(
            g_state.rawMembers,
            g_state.timeframeMinutes);
        g_state.profile.openingEndMinute =
            (std::max)(1, 60 / g_state.timeframeMinutes);
        ResetReplay();
        g_state.backtest = {};
        g_state.evaluation = {};
    }

    void LoadSource()
    {
        g_state.rawMembers.clear();
        g_state.members.clear();
        g_state.backtest = {};
        g_state.evaluation = {};

        if (g_state.sourceMode == SourceMode::Fixture) {
            g_state.rawMembers =
                trading::stock_pool::fixture::BuildDeterministicFixture();
            ApplyTimeframe();
            g_state.status =
                "개발 fixture 로드 완료 — 비실데이터이며 UI/인과성 검증 전용";
            return;
        }

        ResetReplay();
        if (g_state.sourceMode == SourceMode::Historical1516) {
            g_state.status =
                "1516 과거 포착 source 미연결 — 빈 화면으로 fail-closed";
        }
        else {
            g_state.status =
                "실시간 조건식 source 미연결 — 빈 화면으로 fail-closed";
        }
    }

    bool StepReplay()
    {
        const int maximumBars = MaximumBarCount();
        if (maximumBars <= 0 ||
            g_state.nextBarIndex >= static_cast<std::size_t>(maximumBars))
        {
            g_state.playing = false;
            return false;
        }
        g_state.replayHistory.push_back(
            g_state.replayEngine.Evaluate(
                g_state.members,
                g_state.nextBarIndex,
                g_state.topM,
                g_state.profile));
        ++g_state.nextBarIndex;
        return true;
    }

    void RebuildReplayToCount(int snapshotCount)
    {
        snapshotCount = (std::max)(0, snapshotCount);
        ResetReplay();
        for (int index = 0; index < snapshotCount; ++index) {
            if (!StepReplay()) break;
        }
    }

    void RunBacktest()
    {
        if (g_state.members.empty()) {
            g_state.status = "백테스트 거부 — 데이터소스가 비어 있습니다.";
            return;
        }
        g_state.backtest = trading::stock_pool::RunTopMBacktest(
            g_state.members,
            g_state.topM,
            g_state.profile);
        g_state.evaluation =
            trading::stock_pool::evaluation::EvaluateWinnerCapture(
                g_state.members,
                g_state.backtest,
                3);
        g_state.replayHistory = g_state.backtest.snapshots;
        g_state.nextBarIndex =
            static_cast<std::size_t>(MaximumBarCount());
        g_state.replayEngine.Reset();
        g_state.playing = false;
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "인과 백테스트 완료: snapshot %zu개, trade %zu개, 실제 승자 Top-3 포착 %d/%d",
            g_state.backtest.snapshots.size(),
            g_state.backtest.trades.size(),
            g_state.evaluation.capturedWinnerCount,
            g_state.evaluation.winnerCount);
        g_state.status = message;
    }

    ImU32 MemberColor(std::size_t index, int alpha)
    {
        static constexpr std::array<std::array<int, 3>, 12> palette = {{
            {{56, 211, 159}}, {{255, 159, 67}}, {{255, 99, 132}},
            {{54, 162, 235}}, {{153, 102, 255}}, {{255, 205, 86}},
            {{75, 192, 192}}, {{201, 203, 207}}, {{255, 120, 180}},
            {{120, 180, 255}}, {{120, 220, 120}}, {{220, 160, 90}}
        }};
        const auto& color = palette[index % palette.size()];
        return IM_COL32(color[0], color[1], color[2], alpha);
    }

    ImU32 StrengthCellColor(double strength)
    {
        if (strength >= 180.0) return IM_COL32(28, 150, 92, 230);
        if (strength >= 150.0) return IM_COL32(42, 116, 145, 220);
        if (strength >= 100.0) return IM_COL32(75, 82, 102, 210);
        if (strength >= 60.0) return IM_COL32(126, 80, 66, 210);
        return IM_COL32(128, 45, 60, 220);
    }

    void DrawToolbar()
    {
        ImGui::BeginChild("##stock_pool_toolbar", ImVec2(0.0f, 88.0f), true);

        static const char* sourceLabels[] = {
            "개발 fixture (비실데이터)",
            "1516 과거 포착",
            "실시간 조건식"};
        int sourceIndex = static_cast<int>(g_state.sourceMode);
        ImGui::SetNextItemWidth(190.0f);
        if (ImGui::Combo(
                "데이터소스",
                &sourceIndex,
                sourceLabels,
                IM_ARRAYSIZE(sourceLabels)))
        {
            g_state.sourceMode = static_cast<SourceMode>(sourceIndex);
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputText("조건식", g_state.condition, sizeof(g_state.condition));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(105.0f);
        ImGui::InputText(
            "일자",
            g_state.tradingDate,
            sizeof(g_state.tradingDate));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(65.0f);
        ImGui::InputText(
            "포착",
            g_state.captureTime,
            sizeof(g_state.captureTime));

        ImGui::SameLine();
        static const int timeframes[] = {1, 3, 5, 10, 15};
        static const char* timeframeLabels[] = {
            "1분", "3분", "5분", "10분", "15분"};
        int timeframeIndex = 0;
        for (int index = 0; index < IM_ARRAYSIZE(timeframes); ++index) {
            if (timeframes[index] == g_state.timeframeMinutes) {
                timeframeIndex = index;
            }
        }
        ImGui::SetNextItemWidth(70.0f);
        if (ImGui::Combo(
                "Timeframe",
                &timeframeIndex,
                timeframeLabels,
                IM_ARRAYSIZE(timeframeLabels)))
        {
            g_state.timeframeMinutes = timeframes[timeframeIndex];
            if (!g_state.rawMembers.empty()) ApplyTimeframe();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(55.0f);
        if (ImGui::InputInt("Top-M", &g_state.topM, 0, 0)) {
            g_state.topM = (std::max)(1, (std::min)(10, g_state.topM));
            ResetReplay();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(55.0f);
        if (ImGui::InputInt("재생속도", &g_state.replaySpeed, 0, 0)) {
            g_state.replaySpeed =
                (std::max)(1, (std::min)(120, g_state.replaySpeed));
        }

        ImGui::SameLine();
        if (ImGui::Button("불러오기")) LoadSource();
        ImGui::SameLine();
        if (ImGui::Button("백테스트")) RunBacktest();
        ImGui::SameLine();
        if (ImGui::Button(g_state.playing ? "일시정지" : "재생")) {
            if (!g_state.members.empty()) g_state.playing = !g_state.playing;
        }
        ImGui::SameLine();
        if (ImGui::Button("1스텝")) StepReplay();
        ImGui::SameLine();
        if (ImGui::Button("처음")) ResetReplay();

        const RankingSnapshot* snapshot = CurrentSnapshot();
        ImGui::TextDisabled(
            "상태: %s | 종목 %zu | snapshot %zu | %s%s",
            g_state.status.c_str(),
            g_state.members.size(),
            g_state.replayHistory.size(),
            snapshot != nullptr
                ? trading::stock_pool::TimeRegimeName(snapshot->regime)
                : "대기",
            snapshot != nullptr && snapshot->noTrade
                ? " | NoTrade"
                : "");
        ImGui::EndChild();
    }

    void DrawRankingGrid()
    {
        const RankingSnapshot* snapshot = CurrentSnapshot();
        if (snapshot == nullptr) {
            ImGui::TextDisabled(
                "불러오기 후 재생 또는 백테스트를 실행하십시오.");
            return;
        }

        ImGui::Text(
            "종목풀 %zu개 | 상승비율 %.1f%% | 중앙수익 %.2f%% | 리더격차 %.1f",
            snapshot->rows.size(),
            snapshot->breadthPositive * 100.0,
            snapshot->medianSessionReturnPercent,
            snapshot->leaderSeparation);
        if (snapshot->noTrade) {
            ImGui::SameLine();
            ImGui::TextColored(
                ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                "NoTrade: %s",
                snapshot->noTradeReason.c_str());
        }

        if (!ImGui::BeginTable(
                "##stock_pool_grid",
                10,
                ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_Resizable,
                ImVec2(0.0f, -1.0f)))
        {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("순위", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("종목", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("상태", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableSetupColumn("강도", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("1분%", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupColumn("5분%", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupColumn("누적%", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("거래대금", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("순위Δ", ImGuiTableColumnFlags_WidthFixed, 46.0f);
        ImGui::TableSetupColumn("유지", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableHeadersRow();

        for (const RankRow& row : snapshot->rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", row.rank);
            ImGui::TableSetColumnIndex(1);
            const bool selected =
                row.memberIndex ==
                static_cast<std::size_t>(g_state.selectedMemberIndex);
            const std::string label =
                row.code + " " + row.name + "##grid_" + row.code;
            if (ImGui::Selectable(
                    label.c_str(),
                    selected,
                    ImGuiSelectableFlags_SpanAllColumns))
            {
                g_state.selectedMemberIndex =
                    static_cast<int>(row.memberIndex);
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(
                trading::stock_pool::LeaderStateName(row.state));
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.0f%s", row.strength, row.published ? "*" : "");
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%+.2f", row.return1mPercent);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%+.2f", row.return5mPercent);
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%+.2f", row.sessionReturnPercent);
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%.0f", row.turnoverPercentile);
            ImGui::TableSetColumnIndex(8);
            ImGui::Text("%+d", row.rankChange);
            ImGui::TableSetColumnIndex(9);
            ImGui::Text("%d", row.topMStreak);
        }
        ImGui::EndTable();
    }

    void DrawStrengthChart()
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 size(
            (std::max)(available.x, 320.0f),
            (std::max)(available.y, 220.0f));
        ImGui::InvisibleButton("##strength_chart", size);
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(minimum, maximum, IM_COL32(14, 16, 21, 255));
        draw->AddRect(minimum, maximum, IM_COL32(75, 80, 94, 255));

        const float left = minimum.x + 42.0f;
        const float right = maximum.x - 8.0f;
        const float top = minimum.y + 24.0f;
        const float bottom = maximum.y - 24.0f;
        const float width = (std::max)(1.0f, right - left);
        const float height = (std::max)(1.0f, bottom - top);

        for (double level : {50.0, 100.0, 150.0, 180.0, 200.0}) {
            const float y = bottom -
                static_cast<float>(level / 200.0) * height;
            const ImU32 color =
                level == 100.0
                    ? IM_COL32(180, 180, 180, 130)
                    : IM_COL32(70, 75, 88, 110);
            draw->AddLine(ImVec2(left, y), ImVec2(right, y), color, 1.0f);
            char label[16]{};
            std::snprintf(label, sizeof(label), "%.0f", level);
            draw->AddText(ImVec2(minimum.x + 4.0f, y - 7.0f), color, label);
        }

        if (g_state.replayHistory.empty() || g_state.members.empty()) {
            draw->AddText(
                ImVec2(left + 12.0f, top + 12.0f),
                IM_COL32(170, 170, 180, 255),
                "상대강도 시계열 대기");
            return;
        }

        const int selected = (std::max)(
            0,
            (std::min)(
                static_cast<int>(g_state.members.size()) - 1,
                g_state.selectedMemberIndex));
        const RankingSnapshot& current = g_state.replayHistory.back();
        const std::size_t count = g_state.replayHistory.size();
        for (std::size_t memberIndex = 0U;
             memberIndex < g_state.members.size();
             ++memberIndex)
        {
            std::vector<ImVec2> points;
            points.reserve(count);
            for (std::size_t snapshotIndex = 0U;
                 snapshotIndex < count;
                 ++snapshotIndex)
            {
                const RankRow* row = FindRow(
                    g_state.replayHistory[snapshotIndex],
                    g_state.members[memberIndex].code);
                if (row == nullptr || !row->eligible) continue;
                const float x = count <= 1U
                    ? left
                    : left +
                        static_cast<float>(snapshotIndex) /
                        static_cast<float>(count - 1U) * width;
                const float y = bottom -
                    static_cast<float>(
                        (std::max)(0.0, (std::min)(200.0, row->strength)) /
                        200.0) * height;
                points.emplace_back(x, y);
            }
            if (points.size() < 2U) continue;
            const RankRow* currentRow = FindRow(
                current,
                g_state.members[memberIndex].code);
            const bool highlighted =
                static_cast<int>(memberIndex) == selected;
            const bool currentTop =
                currentRow != nullptr && currentRow->rank > 0 &&
                currentRow->rank <= g_state.topM;
            draw->AddPolyline(
                points.data(),
                static_cast<int>(points.size()),
                MemberColor(
                    memberIndex,
                    highlighted ? 255 : (currentTop ? 190 : 65)),
                0,
                highlighted ? 3.0f : (currentTop ? 1.8f : 1.0f));
        }

        const std::size_t regimeIndex =
            static_cast<std::size_t>(
                (std::max)(1, g_state.profile.openingEndMinute));
        if (count > regimeIndex) {
            const float x = left +
                static_cast<float>(regimeIndex) /
                static_cast<float>(count - 1U) * width;
            draw->AddLine(
                ImVec2(x, top),
                ImVec2(x, bottom),
                IM_COL32(255, 195, 80, 150),
                1.0f);
            draw->AddText(
                ImVec2(x + 4.0f, top),
                IM_COL32(255, 205, 100, 220),
                "구조 프로필 전환");
        }

        const std::string selectedCode = g_state.members[selected].code;
        const RankRow* selectedRow = FindRow(current, selectedCode);
        char title[256]{};
        std::snprintf(
            title,
            sizeof(title),
            "%s %s | 강도 %.1f | 순위 %d | %s",
            selectedCode.c_str(),
            g_state.members[selected].name.c_str(),
            selectedRow != nullptr ? selectedRow->strength : 0.0,
            selectedRow != nullptr ? selectedRow->rank : 0,
            trading::stock_pool::TimeRegimeName(current.regime));
        draw->AddText(
            ImVec2(left, minimum.y + 4.0f),
            MemberColor(static_cast<std::size_t>(selected), 255),
            title);

        char startLabel[16]{};
        char endLabel[16]{};
        FormatTime(
            static_cast<int>(g_state.replayHistory.front().asOfIndex),
            startLabel,
            sizeof(startLabel));
        FormatTime(
            static_cast<int>(current.asOfIndex),
            endLabel,
            sizeof(endLabel));
        draw->AddText(
            ImVec2(left, bottom + 4.0f),
            IM_COL32(150, 155, 165, 255),
            startLabel);
        const ImVec2 endSize = ImGui::CalcTextSize(endLabel);
        draw->AddText(
            ImVec2(right - endSize.x, bottom + 4.0f),
            IM_COL32(150, 155, 165, 255),
            endLabel);

        if (ImGui::IsItemHovered()) {
            const float mouseX = ImGui::GetIO().MousePos.x;
            const double ratio = Clamp(
                static_cast<double>((mouseX - left) / width),
                0.0,
                1.0);
            const std::size_t snapshotIndex = count <= 1U
                ? 0U
                : static_cast<std::size_t>(
                    std::llround(ratio * static_cast<double>(count - 1U)));
            const RankingSnapshot& hovered =
                g_state.replayHistory[snapshotIndex];
            char time[16]{};
            FormatTime(
                static_cast<int>(hovered.asOfIndex),
                time,
                sizeof(time));
            ImGui::BeginTooltip();
            ImGui::Text("%s | %s", time, trading::stock_pool::TimeRegimeName(hovered.regime));
            for (std::size_t index = 0U;
                 index < hovered.rows.size() && index < 5U;
                 ++index)
            {
                const RankRow& row = hovered.rows[index];
                ImGui::Text(
                    "%d. %s %.1f %s",
                    row.rank,
                    row.name.c_str(),
                    row.strength,
                    row.published ? "[공급]" : "");
            }
            ImGui::EndTooltip();
        }
    }

    void DrawHeatmap(float requestedHeight)
    {
        const RankingSnapshot* current = CurrentSnapshot();
        if (current == nullptr || g_state.replayHistory.empty()) {
            ImGui::TextDisabled("백테스트 결과가 없습니다.");
            return;
        }

        const int rowCount = static_cast<int>(current->rows.size());
        const float rowHeight = 18.0f;
        const float height = (std::max)(
            requestedHeight,
            rowHeight * static_cast<float>(rowCount) + 24.0f);
        ImGui::InvisibleButton(
            "##stock_pool_heatmap",
            ImVec2(ImGui::GetContentRegionAvail().x, height));
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(minimum, maximum, IM_COL32(15, 17, 22, 255));
        draw->AddRect(minimum, maximum, IM_COL32(70, 76, 90, 255));

        const float labelWidth = 150.0f;
        const float chartLeft = minimum.x + labelWidth;
        const float chartWidth = (std::max)(1.0f, maximum.x - chartLeft - 4.0f);
        const float cellWidth = chartWidth /
            static_cast<float>(g_state.replayHistory.size());

        for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
            const RankRow& currentRow = current->rows[rowIndex];
            const float y0 = minimum.y + 4.0f + rowIndex * rowHeight;
            const float y1 = y0 + rowHeight - 2.0f;
            const bool selected =
                currentRow.memberIndex ==
                static_cast<std::size_t>(g_state.selectedMemberIndex);
            if (selected) {
                draw->AddRectFilled(
                    ImVec2(minimum.x + 2.0f, y0),
                    ImVec2(maximum.x - 2.0f, y1),
                    IM_COL32(45, 55, 72, 150));
            }
            draw->AddText(
                ImVec2(minimum.x + 6.0f, y0 + 1.0f),
                MemberColor(currentRow.memberIndex, 240),
                (currentRow.code + " " + currentRow.name).c_str());

            for (std::size_t snapshotIndex = 0U;
                 snapshotIndex < g_state.replayHistory.size();
                 ++snapshotIndex)
            {
                const RankRow* row = FindRow(
                    g_state.replayHistory[snapshotIndex],
                    currentRow.code);
                if (row == nullptr || !row->eligible) continue;
                const float x0 = chartLeft +
                    static_cast<float>(snapshotIndex) * cellWidth;
                const float x1 = chartLeft +
                    static_cast<float>(snapshotIndex + 1U) * cellWidth;
                draw->AddRectFilled(
                    ImVec2(x0, y0),
                    ImVec2(x1 + 0.5f, y1),
                    StrengthCellColor(row->strength));
                if (row->published) {
                    draw->AddRect(
                        ImVec2(x0, y0),
                        ImVec2(x1 + 0.5f, y1),
                        IM_COL32(255, 225, 95, 255),
                        0.0f,
                        0,
                        1.0f);
                }
            }
        }

        if (ImGui::IsItemHovered()) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const int rowIndex = static_cast<int>(
                (mouse.y - minimum.y - 4.0f) / rowHeight);
            const int snapshotIndex = cellWidth > 0.0f
                ? static_cast<int>((mouse.x - chartLeft) / cellWidth)
                : -1;
            if (rowIndex >= 0 && rowIndex < rowCount &&
                snapshotIndex >= 0 &&
                snapshotIndex <
                    static_cast<int>(g_state.replayHistory.size()))
            {
                const RankRow& currentRow = current->rows[rowIndex];
                const RankingSnapshot& snapshot =
                    g_state.replayHistory[snapshotIndex];
                const RankRow* row = FindRow(snapshot, currentRow.code);
                if (row != nullptr) {
                    char time[16]{};
                    FormatTime(
                        static_cast<int>(snapshot.asOfIndex),
                        time,
                        sizeof(time));
                    ImGui::BeginTooltip();
                    ImGui::Text("%s %s", currentRow.code.c_str(), currentRow.name.c_str());
                    ImGui::Text("시각 %s | 순위 %d | 강도 %.1f", time, row->rank, row->strength);
                    ImGui::Text("상태 %s | Top-M 유지 %d", trading::stock_pool::LeaderStateName(row->state), row->topMStreak);
                    ImGui::Text("누적수익 %+.2f%% | 5분 %+.2f%%", row->sessionReturnPercent, row->return5mPercent);
                    ImGui::EndTooltip();
                }
            }
        }
    }

    void DrawManagementTab()
    {
        ImGui::TextWrapped(
            "이 화면은 조건식 포착종목 전체를 하나의 축소시장으로 보고, 미래정보 없이 계산된 상대강도와 지속 순위로 Top-M만 공급합니다. 상세차트는 수정하거나 내장하지 않습니다.");
        const RankingSnapshot* snapshot = CurrentSnapshot();
        if (snapshot == nullptr || g_state.members.empty()) return;
        const int selected = (std::max)(
            0,
            (std::min)(
                static_cast<int>(g_state.members.size()) - 1,
                g_state.selectedMemberIndex));
        const RankRow* row = FindRow(
            *snapshot,
            g_state.members[selected].code);
        if (row == nullptr) return;
        ImGui::Separator();
        ImGui::Text("선택 종목: %s %s [%s]", row->code.c_str(), row->name.c_str(), row->market.c_str());
        ImGui::Text("현재 강도 %.1f | 순위 %d | 순위 변화 %+d | 상태 %s", row->strength, row->rank, row->rankChange, trading::stock_pool::LeaderStateName(row->state));
        ImGui::Text("Top-M 유지 %d snapshot | 거래대금 percentile %.0f | 가속도 percentile %.0f", row->topMStreak, row->turnoverPercentile, row->turnoverAccelerationPercentile);
        ImGui::Text("공급 여부: %s", row->published ? "Top-M 공급" : "관찰/대기");
    }

    void DrawEvaluationTab()
    {
        ImGui::TextColored(
            ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
            "사후 label 전용 — 이 값은 ranking engine이나 장중 화면으로 역유입되지 않습니다.");
        if (g_state.evaluation.winnerCount == 0) {
            ImGui::TextDisabled("백테스트를 실행하십시오.");
            return;
        }
        ImGui::Text(
            "실제 최고상승 Top-%d 중 causal Top-M 포착 %d개 (%.1f%%)",
            g_state.evaluation.winnerCount,
            g_state.evaluation.capturedWinnerCount,
            g_state.evaluation.captureRatePercent);
        if (ImGui::BeginTable(
                "##winner_capture",
                5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("종목");
            ImGui::TableSetupColumn("사후 최고수익률");
            ImGui::TableSetupColumn("포착");
            ImGui::TableSetupColumn("최초 공급시각");
            ImGui::TableSetupColumn("판정");
            ImGui::TableHeadersRow();
            for (const auto& winner : g_state.evaluation.winners) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s %s", winner.code.c_str(), winner.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%+.2f%%", winner.maximumReturnPercent);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(winner.captured ? "예" : "아니오");
                ImGui::TableSetColumnIndex(3);
                if (winner.captured) {
                    char time[16]{};
                    FormatTime(winner.firstPublishedMinute, time, sizeof(time));
                    ImGui::TextUnformatted(time);
                }
                else {
                    ImGui::TextUnformatted("-");
                }
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(
                    winner.captured ? "True/Missed 분석 대상" : "MissedWinner");
            }
            ImGui::EndTable();
        }
    }

    void DrawBacktestTab()
    {
        if (g_state.backtest.snapshots.empty()) {
            ImGui::TextDisabled("백테스트를 실행하십시오.");
            return;
        }
        ImGui::Text(
            "Trade %zu | 승률 %.1f%% | 평균 %+.2f%% | 최고 %+.2f%% | 최저 %+.2f%%",
            g_state.backtest.trades.size(),
            g_state.backtest.winRatePercent,
            g_state.backtest.averageReturnPercent,
            g_state.backtest.bestReturnPercent,
            g_state.backtest.worstReturnPercent);
        DrawHeatmap(220.0f);
        if (ImGui::BeginTable(
                "##trade_results",
                6,
                ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY,
                ImVec2(0.0f, 150.0f)))
        {
            ImGui::TableSetupColumn("종목");
            ImGui::TableSetupColumn("진입");
            ImGui::TableSetupColumn("청산");
            ImGui::TableSetupColumn("진입가");
            ImGui::TableSetupColumn("청산가");
            ImGui::TableSetupColumn("수익률");
            ImGui::TableHeadersRow();
            for (const auto& trade : g_state.backtest.trades) {
                char entry[16]{};
                char exit[16]{};
                FormatTime(static_cast<int>(trade.entryIndex), entry, sizeof(entry));
                FormatTime(static_cast<int>(trade.exitIndex), exit, sizeof(exit));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s %s", trade.code.c_str(), trade.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(entry);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(exit);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.0f", trade.entryPrice);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.0f", trade.exitPrice);
                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%+.2f%%", trade.returnPercent);
            }
            ImGui::EndTable();
        }
    }

    void DrawSimulationTab()
    {
        const int maximumSnapshots = (std::max)(
            0,
            MaximumBarCount() - g_state.profile.minimumHistoryBars + 1);
        int currentSnapshots =
            static_cast<int>(g_state.replayHistory.size());
        ImGui::SetNextItemWidth(420.0f);
        if (ImGui::SliderInt(
                "인과 replay 위치",
                &currentSnapshots,
                0,
                maximumSnapshots))
        {
            RebuildReplayToCount(currentSnapshots);
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "입력 timestamp <= snapshot.as_of 불변식");
        const RankingSnapshot* snapshot = CurrentSnapshot();
        if (snapshot != nullptr) {
            char time[16]{};
            FormatTime(static_cast<int>(snapshot->asOfIndex), time, sizeof(time));
            ImGui::Text(
                "현재 %s | %s | %s",
                time,
                trading::stock_pool::TimeRegimeName(snapshot->regime),
                snapshot->noTrade ? snapshot->noTradeReason.c_str() : "Top-M 발행 가능");
        }
        ImGui::TextWrapped(
            "과거 1516 모드에서는 선택 일자·조건식·포착시각의 Frozen Cohort만 로드하고, 미래 수익률은 replay 종료 후 성과검증 탭에서만 결합합니다. 실시간 모드는 초기 membership과 편입/탈락 delta를 같은 engine 계약으로 공급합니다.");
    }

    void DrawWorkbench()
    {
        DrawToolbar();

        const float availableHeight = ImGui::GetContentRegionAvail().y;
        const float middleHeight = (std::max)(330.0f, availableHeight * 0.58f);
        ImGui::BeginChild("##ranking_area", ImVec2(0.0f, middleHeight), false);
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::BeginChild(
            "종목풀 그리드",
            ImVec2(width * 0.48f, 0.0f),
            true);
        DrawRankingGrid();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild(
            "동시 상대강도 차트",
            ImVec2(0.0f, 0.0f),
            true);
        DrawStrengthChart();
        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::BeginChild("##analysis_area", ImVec2(0.0f, 0.0f), true);
        if (ImGui::BeginTabBar("##stock_pool_tabs")) {
            if (ImGui::BeginTabItem("종목풀 관리")) {
                DrawManagementTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("성과검증")) {
                DrawEvaluationTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("백테스트")) {
                DrawBacktestTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("시뮬레이션")) {
                DrawSimulationTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
    }

    void CreateRenderTarget()
    {
        ID3D11Texture2D* backBuffer = nullptr;
        if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
            g_device->CreateRenderTargetView(
                backBuffer,
                nullptr,
                &g_renderTarget);
            backBuffer->Release();
        }
    }

    bool CreateDeviceD3D(HWND window)
    {
        DXGI_SWAP_CHAIN_DESC description{};
        description.BufferCount = 2;
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.BufferDesc.RefreshRate.Numerator = 60;
        description.BufferDesc.RefreshRate.Denominator = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.OutputWindow = window;
        description.SampleDesc.Count = 1;
        description.Windowed = TRUE;
        description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        D3D_FEATURE_LEVEL featureLevel{};
        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0};
        const HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            levels,
            2,
            D3D11_SDK_VERSION,
            &description,
            &g_swapChain,
            &g_device,
            &featureLevel,
            &g_context);
        if (FAILED(result)) return false;
        CreateRenderTarget();
        return true;
    }

    void CleanupDeviceD3D()
    {
        if (g_renderTarget != nullptr) {
            g_renderTarget->Release();
            g_renderTarget = nullptr;
        }
        if (g_swapChain != nullptr) {
            g_swapChain->Release();
            g_swapChain = nullptr;
        }
        if (g_context != nullptr) {
            g_context->Release();
            g_context = nullptr;
        }
        if (g_device != nullptr) {
            g_device->Release();
            g_device = nullptr;
        }
    }

    LRESULT WINAPI WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wordParameter,
        LPARAM longParameter)
    {
        if (ImGui_ImplWin32_WndProcHandler(
                window,
                message,
                wordParameter,
                longParameter))
        {
            return true;
        }
        switch (message) {
        case WM_SIZE:
            if (wordParameter != SIZE_MINIMIZED) {
                g_resizeWidth = LOWORD(longParameter);
                g_resizeHeight = HIWORD(longParameter);
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wordParameter & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(
            window,
            message,
            wordParameter,
            longParameter);
    }

    const ImWchar* GetKoreanRanges(ImGuiIO& io)
    {
        static ImVector<ImWchar> ranges;
        if (ranges.Size == 0) {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
            static const ImWchar korean[] = {
                0x3131, 0x318E,
                0xAC00, 0xD7A3,
                0x2010, 0x2027,
                0x3000, 0x303F,
                0xFF01, 0xFF60,
                0};
            builder.AddRanges(korean);
            builder.BuildRanges(&ranges);
        }
        return ranges.Data;
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
        L"StockPoolWorkbench",
        nullptr};
    RegisterClassExW(&windowClass);
    HWND window = CreateWindowW(
        windowClass.lpszClassName,
        L"종목풀 분석 워크벤치 — 인과 상대강도 / Top-M",
        WS_OVERLAPPEDWINDOW,
        30,
        25,
        1720,
        1020,
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
    io.IniFilename = "stock_pool_workbench_layout.ini";
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
            if (GetFileAttributesA(candidate) == INVALID_FILE_ATTRIBUTES) {
                continue;
            }
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

    LoadSource();
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

        if (g_state.playing) {
            g_state.playAccumulator +=
                static_cast<double>(io.DeltaTime) *
                static_cast<double>(g_state.replaySpeed);
            while (g_state.playAccumulator >= 1.0) {
                if (!StepReplay()) break;
                g_state.playAccumulator -= 1.0;
            }
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::Begin(
            "##stock_pool_host",
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus);
        DrawWorkbench();
        ImGui::End();

        ImGui::Render();
        const float clearColor[4] = {0.05f, 0.055f, 0.065f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
        g_context->ClearRenderTargetView(g_renderTarget, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
        g_swapChain->Present(1, 0);

        if (!g_state.playing) {
            MsgWaitForMultipleObjectsEx(
                0,
                nullptr,
                80,
                QS_ALLINPUT,
                MWMO_INPUTAVAILABLE);
        }
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
