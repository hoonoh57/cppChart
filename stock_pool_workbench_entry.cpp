#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "app/stock_pool_1516_import.h"
#include "app/stock_pool_hydration.h"
#include "app/stock_pool_strength_cross.h"
#include "platform/stock_pool_gateway_client.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window,
    UINT message,
    WPARAM wordParameter,
    LPARAM longParameter);

namespace ImGui
{
    bool StockPoolButton(
        const char* label,
        const ImVec2& size = ImVec2(0.0f, 0.0f));
    void StockPoolTextDisabled(const char* format, ...);
    void StockPoolRender();
}

namespace
{
    double Clamp(double value, double minimum, double maximum)
    {
        return (std::max)(minimum, (std::min)(maximum, value));
    }

    LRESULT StockPoolImGuiWin32WndProcHandler(
        HWND window,
        UINT message,
        WPARAM wordParameter,
        LPARAM longParameter)
    {
        return ::ImGui_ImplWin32_WndProcHandler(
            window,
            message,
            wordParameter,
            longParameter);
    }
}

#define Button StockPoolButton
#define TextDisabled StockPoolTextDisabled
#define Render StockPoolRender
#define ImGui_ImplWin32_WndProcHandler StockPoolImGuiWin32WndProcHandler
#include "stock_pool_workbench_main.cpp"
#undef ImGui_ImplWin32_WndProcHandler
#undef Render
#undef TextDisabled
#undef Button

namespace
{
    using trading::stock_pool::MemberSeries;
    using trading::stock_pool::import1516::ImportedRow;
    using trading::stock_pool::import1516::ParseResult;
    using trading::stock_pool::import1516::ResolutionStatus;

    struct Historical1516ImportDialogState final
    {
        bool requestOpen = false;
        bool converted = false;
        bool cohortCommitted = false;
        long long cohortId = 0LL;
        std::array<char, 262144> clipboardText{};
        ParseResult parsed;
        std::string gatewayStatus;
        std::string conversionStatus;
    };

    Historical1516ImportDialogState g_historicalImport;

    std::size_t CountStatus(ResolutionStatus status)
    {
        std::size_t count = 0U;
        for (const ImportedRow& row : g_historicalImport.parsed.rows) {
            if (row.status == status) ++count;
        }
        return count;
    }

    void Paste1516Clipboard()
    {
        const char* clipboard = ImGui::GetClipboardText();
        if (clipboard == nullptr || clipboard[0] == '\0') {
            g_historicalImport.conversionStatus =
                "클립보드에 텍스트가 없습니다.";
            return;
        }
        std::snprintf(
            g_historicalImport.clipboardText.data(),
            g_historicalImport.clipboardText.size(),
            "%s",
            clipboard);
        g_historicalImport.converted = false;
        g_historicalImport.cohortCommitted = false;
        g_historicalImport.cohortId = 0LL;
        g_historicalImport.conversionStatus =
            "클립보드 내용을 붙였습니다. 변환을 누르십시오.";
    }

    void ApplyGatewayRejectionReasons(
        const std::vector<trading::stock_pool::platform::GatewayRejectedSymbol>& rejected)
    {
        for (const auto& item : rejected) {
            for (ImportedRow& row : g_historicalImport.parsed.rows) {
                if (row.name != item.name ||
                    row.status == ResolutionStatus::Resolved ||
                    row.status == ResolutionStatus::InvalidRow)
                {
                    continue;
                }
                row.reason = "server32: " + item.reason;
                if (item.reason == "exact_name_ambiguous") {
                    row.status = ResolutionStatus::AmbiguousSymbol;
                }
                else if (item.reason == "duplicate_input") {
                    row.status = ResolutionStatus::DuplicateSymbol;
                }
                else {
                    row.status = ResolutionStatus::MissingSymbol;
                }
                break;
            }
        }
    }

    void Convert1516Clipboard()
    {
        g_historicalImport.parsed =
            trading::stock_pool::import1516::ParseClipboardText(
                g_historicalImport.clipboardText.data());
        g_historicalImport.gatewayStatus.clear();
        g_historicalImport.converted = true;
        g_historicalImport.cohortCommitted = false;
        g_historicalImport.cohortId = 0LL;

        if (g_historicalImport.parsed.rows.empty()) {
            g_historicalImport.conversionStatus =
                "변환 가능한 1516 종목 행이 없습니다.";
            return;
        }

        std::vector<std::string> names;
        names.reserve(g_historicalImport.parsed.rows.size());
        for (const ImportedRow& row : g_historicalImport.parsed.rows) {
            if (row.status == ResolutionStatus::InvalidRow ||
                row.name.empty())
            {
                continue;
            }
            names.push_back(row.name);
        }

        const auto resolution =
            trading::stock_pool::platform::ResolveSymbolsViaServer32(
                names,
                ".env");
        if (!resolution.ok) {
            g_historicalImport.gatewayStatus = resolution.error;
            g_historicalImport.conversionStatus =
                "텍스트 변환은 완료했지만 server32 종목코드 조회에 실패했습니다.";
            return;
        }

        trading::stock_pool::import1516::ResolveExactSymbolNames(
            g_historicalImport.parsed.rows,
            resolution.entries);
        ApplyGatewayRejectionReasons(resolution.rejected);
        g_historicalImport.gatewayStatus =
            resolution.source + " | server32 batch resolve";

        const std::size_t resolved = CountStatus(ResolutionStatus::Resolved);
        const std::size_t rejectedCount =
            g_historicalImport.parsed.rows.size() - resolved;
        g_historicalImport.conversionStatus =
            "변환 완료: 코드 확정 " + std::to_string(resolved) +
            "개, 폐기 예정 " + std::to_string(rejectedCount) + "개";
    }

    void CommitHistoricalCohort()
    {
        g_state.timeframeMinutes = 10;
        const auto saved =
            trading::stock_pool::platform::Save1516CohortViaServer32(
                g_state.condition,
                g_state.tradingDate,
                g_state.captureTime,
                g_state.timeframeMinutes,
                g_historicalImport.parsed.rows,
                ".env");
        if (!saved.ok) {
            g_historicalImport.cohortCommitted = false;
            g_historicalImport.cohortId = 0LL;
            g_historicalImport.gatewayStatus = saved.error;
            g_historicalImport.conversionStatus =
                "Frozen Cohort DB 저장에 실패해 UI cohort도 확정하지 않았습니다.";
            return;
        }

        std::set<std::string> rejectedNames;
        for (const auto& rejected : saved.rejected) {
            rejectedNames.insert(rejected.name);
        }

        std::vector<MemberSeries> members;
        for (const ImportedRow& row : g_historicalImport.parsed.rows) {
            if (row.status != ResolutionStatus::Resolved ||
                rejectedNames.find(row.name) != rejectedNames.end())
            {
                continue;
            }
            MemberSeries member;
            member.code = row.code;
            member.name = row.name;
            member.market = row.market;
            members.push_back(std::move(member));
        }
        if (members.empty()) {
            g_historicalImport.conversionStatus =
                "DB 저장 응답에 확정 가능한 종목이 없어 UI cohort를 만들지 않았습니다.";
            return;
        }

        g_state.rawMembers = std::move(members);
        ApplyTimeframe();
        g_historicalImport.cohortCommitted = true;
        g_historicalImport.cohortId = saved.cohortId;
        g_historicalImport.gatewayStatus =
            saved.source + " | cohort_id=" +
            std::to_string(saved.cohortId) + " | hash=" +
            saved.rawImportHash;
        g_state.status =
            "1516 Frozen Cohort DB 저장/확정: " +
            std::to_string(g_state.members.size()) +
            "개 | cohort_id=" + std::to_string(saved.cohortId) +
            " | 10분 강도100 상향돌파 백테스트 대기";
    }

    bool HistoricalMembersNeedHydration()
    {
        if (g_state.rawMembers.empty()) return true;
        return std::any_of(
            g_state.rawMembers.begin(),
            g_state.rawMembers.end(),
            [](const MemberSeries& member) {
                return member.bars.empty();
            });
    }

    bool HydrateHistoricalCohort()
    {
        if (g_state.rawMembers.empty()) {
            g_state.status =
                "백테스트 거부 — 먼저 1516 Frozen Cohort를 확정하십시오.";
            return false;
        }

        g_state.status =
            "server32 실제 1분봉 hydration 진행 중 — 창이 응답할 때까지 기다리십시오.";
        const auto hydrated =
            trading::stock_pool::hydration::HydrateHistoricalMembers(
                g_state.rawMembers,
                g_state.tradingDate,
                g_state.captureTime,
                g_state.profile.minimumHistoryBars,
                ".env");
        if (!hydrated.ok) {
            g_state.status = "분봉 hydration 실패 — " + hydrated.error;
            return false;
        }

        g_state.rawMembers = hydrated.members;
        ApplyTimeframe();
        g_state.status =
            "분봉 hydration 완료: " +
            std::to_string(g_state.rawMembers.size()) + "종목 x " +
            std::to_string(hydrated.barsPerMember) +
            "개 공통 실제 1분봉 | " + hydrated.source;
        return true;
    }

    bool RunHistoricalBacktest()
    {
        if (g_state.rawMembers.empty()) {
            g_state.status =
                "백테스트 거부 — 먼저 1516 Frozen Cohort를 확정하십시오.";
            return false;
        }

        if (g_state.timeframeMinutes != 10) {
            g_state.timeframeMinutes = 10;
            if (!HistoricalMembersNeedHydration()) ApplyTimeframe();
        }

        if (HistoricalMembersNeedHydration() && !HydrateHistoricalCohort()) {
            return false;
        }

        const int availableBars = MaximumBarCount();
        if (availableBars < g_state.profile.minimumHistoryBars) {
            g_state.status =
                "백테스트 거부 — 10분 공통 봉이 " +
                std::to_string(availableBars) + "개이며 최소 " +
                std::to_string(g_state.profile.minimumHistoryBars) +
                "개가 필요합니다.";
            return false;
        }

        trading::stock_pool::strategy::StrengthCrossProfile strategyProfile;
        strategyProfile.entryStrength = 100.0;
        strategyProfile.takeProfitPercent = 1.0;
        strategyProfile.stopLossPercent = 1.0;
        strategyProfile.stopFirstWhenBothTouched = true;

        auto summary =
            trading::stock_pool::strategy::RunStrengthCrossBacktest(
                g_state.members,
                g_state.topM,
                g_state.profile,
                strategyProfile);
        g_state.backtest = std::move(summary.backtest);
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

        if (g_state.backtest.snapshots.empty()) {
            g_state.status =
                "백테스트 실패 — 분봉은 적재됐지만 causal snapshot이 생성되지 않았습니다.";
            return false;
        }

        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "10분 강도100 상향돌파 완료: cross %zu | trade %zu | TP %zu | SL %zu | 장마감 %zu | 순위는 진입조건 아님",
            summary.upwardCrossCount,
            g_state.backtest.trades.size(),
            summary.takeProfitCount,
            summary.stopLossCount,
            summary.sessionCloseCount);
        g_state.status = message;
        return true;
    }

    void DrawResolvedRows()
    {
        if (!ImGui::BeginTable(
                "##1516_resolved",
                9,
                ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_Resizable,
                ImVec2(0.0f, 230.0f)))
        {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("종목명");
        ImGui::TableSetupColumn("코드");
        ImGui::TableSetupColumn("1분");
        ImGui::TableSetupColumn("3분");
        ImGui::TableSetupColumn("7시간");
        ImGui::TableSetupColumn("최고");
        ImGui::TableSetupColumn("검색거래량");
        ImGui::TableSetupColumn("기타");
        ImGui::TableSetupColumn("상태");
        ImGui::TableHeadersRow();

        for (const ImportedRow& row : g_historicalImport.parsed.rows) {
            if (row.status != ResolutionStatus::Resolved) continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(row.code.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%+.2f%%", row.return1mPercent);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%+.2f%%", row.return3mPercent);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%+.2f%%", row.return7hPercent);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%+.2f%%", row.maximumReturnPercent);
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%lld", static_cast<long long>(row.captureVolume));
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%.2f", row.otherValue);
            ImGui::TableSetColumnIndex(8);
            ImGui::TextUnformatted("코드확정");
        }
        ImGui::EndTable();
    }

    void DrawRejectedRows()
    {
        if (!ImGui::BeginTable(
                "##1516_rejected",
                4,
                ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_Resizable,
                ImVec2(0.0f, 130.0f)))
        {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("원본 줄", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableSetupColumn("종목명");
        ImGui::TableSetupColumn("상태", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("폐기 사유");
        ImGui::TableHeadersRow();
        for (const ImportedRow& row : g_historicalImport.parsed.rows) {
            if (row.status == ResolutionStatus::Resolved) continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%zu", row.sourceLine);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(row.name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(
                trading::stock_pool::import1516::ResolutionStatusName(
                    row.status));
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(row.reason.c_str());
        }
        ImGui::EndTable();
    }

    void Draw1516ImportDialog()
    {
        if (g_historicalImport.requestOpen) {
            ImGui::OpenPopup("1516 클립보드 가져오기");
            g_historicalImport.requestOpen = false;
        }

        ImGui::SetNextWindowSize(ImVec2(1320.0f, 850.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal(
                "1516 클립보드 가져오기",
                nullptr,
                ImGuiWindowFlags_NoDocking))
        {
            return;
        }

        ImGui::TextWrapped(
            "키움 1516 성과검증 우측 검색 종목 목록에서 컨텍스트 메뉴의 복사(Z)를 실행한 뒤 붙여넣으십시오. 종목코드 조회와 cohort 저장은 server32가 MySQL을 담당합니다. 7시간·최고수익률은 사후 label로만 저장되며 장중 상대강도 계산에는 사용하지 않습니다.");
        ImGui::Separator();

        if (ImGui::Button("클립보드 붙여넣기")) Paste1516Clipboard();
        ImGui::SameLine();
        if (ImGui::Button("텍스트 지우기")) {
            g_historicalImport.clipboardText.fill('\0');
            g_historicalImport.parsed = {};
            g_historicalImport.converted = false;
            g_historicalImport.cohortCommitted = false;
            g_historicalImport.cohortId = 0LL;
            g_historicalImport.gatewayStatus.clear();
            g_historicalImport.conversionStatus.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("변환 + 종목코드 조회")) {
            Convert1516Clipboard();
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "server32 -> gate3.g3_symbol_master / exact name / delisted=0");

        ImGui::InputTextMultiline(
            "##1516_clipboard_text",
            g_historicalImport.clipboardText.data(),
            g_historicalImport.clipboardText.size(),
            ImVec2(-1.0f, 230.0f),
            ImGuiInputTextFlags_AllowTabInput);

        if (!g_historicalImport.conversionStatus.empty()) {
            ImGui::TextWrapped(
                "변환: %s",
                g_historicalImport.conversionStatus.c_str());
        }
        if (!g_historicalImport.gatewayStatus.empty()) {
            ImGui::TextWrapped(
                "데이터 게이트웨이: %s",
                g_historicalImport.gatewayStatus.c_str());
        }
        for (const std::string& diagnostic :
             g_historicalImport.parsed.diagnostics)
        {
            ImGui::TextColored(
                ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                "%s",
                diagnostic.c_str());
        }

        if (g_historicalImport.converted) {
            ImGui::SeparatorText("코드 확정 종목 — Frozen Cohort 포함");
            DrawResolvedRows();
            ImGui::SeparatorText("폐기 종목");
            DrawRejectedRows();
        }

        const std::size_t resolved = CountStatus(ResolutionStatus::Resolved);
        ImGui::BeginDisabled(resolved == 0U);
        if (ImGui::Button("DB 저장 + Frozen Cohort 생성")) {
            CommitHistoricalCohort();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (g_historicalImport.cohortCommitted) {
            ImGui::TextColored(
                ImVec4(0.35f, 0.95f, 0.65f, 1.0f),
                "확정 완료: %zu종목 / cohort_id=%lld",
                g_state.members.size(),
                g_historicalImport.cohortId);
            ImGui::SameLine();
        }
        if (ImGui::Button("닫기")) ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    void DrawFrozenCohortMembers()
    {
        const bool waiting = HistoricalMembersNeedHydration();
        ImGui::TextDisabled(
            waiting
                ? "Frozen Cohort %zu개 — 실제 분봉 hydration 대기"
                : "Frozen Cohort %zu개 — 실제 분봉 적재 완료",
            g_state.members.size());
        ImGui::TextWrapped(
            waiting
                ? "아래 목록은 DB에서 확정된 포착 종목입니다. 백테스트를 누르면 server32에서 포착시각 이후 실제 1분봉을 적재하고 10분봉으로 집계합니다."
                : "실제 분봉 적재가 완료됐습니다. 매수는 상대순위가 아니라 강도 100 상향돌파에서만 발생합니다.");

        if (!ImGui::BeginTable(
                "##frozen_cohort_members",
                4,
                ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_Resizable,
                ImVec2(0.0f, -1.0f)))
        {
            return;
        }

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("종목", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("코드", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("시장", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("분봉 상태", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableHeadersRow();

        for (std::size_t index = 0U; index < g_state.members.size(); ++index) {
            const MemberSeries& member = g_state.members[index];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool selected =
                static_cast<int>(index) == g_state.selectedMemberIndex;
            const std::string label =
                member.name + "##frozen_" + member.code;
            if (ImGui::Selectable(
                    label.c_str(),
                    selected,
                    ImGuiSelectableFlags_SpanAllColumns))
            {
                g_state.selectedMemberIndex = static_cast<int>(index);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(member.code.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(
                member.market.empty() ? "-" : member.market.c_str());
            ImGui::TableSetColumnIndex(3);
            if (member.bars.empty()) {
                ImGui::TextDisabled("대기");
            }
            else {
                ImGui::Text("%zu개", member.bars.size());
            }
        }
        ImGui::EndTable();
    }
}

bool ImGui::StockPoolButton(
    const char* label,
    const ImVec2& size)
{
    const bool clicked = ImGui::Button(label, size);
    if (!clicked || label == nullptr) return clicked;

    if (std::strcmp(label, "불러오기") == 0 &&
        g_state.sourceMode == SourceMode::Historical1516)
    {
        g_historicalImport.requestOpen = true;
        return false;
    }

    if (std::strcmp(label, "백테스트") == 0 &&
        g_state.sourceMode == SourceMode::Historical1516)
    {
        RunHistoricalBacktest();
        return false;
    }

    return clicked;
}

void ImGui::StockPoolTextDisabled(const char* format, ...)
{
    static constexpr const char* emptyRankingMessage =
        "불러오기 후 재생 또는 백테스트를 실행하십시오.";

    if (format != nullptr &&
        std::strcmp(format, emptyRankingMessage) == 0 &&
        g_state.sourceMode == SourceMode::Historical1516 &&
        !g_state.members.empty())
    {
        DrawFrozenCohortMembers();
        return;
    }

    std::va_list arguments;
    va_start(arguments, format);
    ImGui::TextDisabledV(format, arguments);
    va_end(arguments);
}

void ImGui::StockPoolRender()
{
    Draw1516ImportDialog();
    ImGui::Render();
}
