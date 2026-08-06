#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "app/stock_pool_1516_import.h"
#include "platform/stock_pool_mysql_symbol_master.h"

// Dear ImGui's Win32 backend callback lives in the global namespace.
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

// Intercept only the workbench's generic Load button and final Render call.
// The existing workbench implementation remains unchanged.
#define Button StockPoolButton
#define Render StockPoolRender
#define ImGui_ImplWin32_WndProcHandler StockPoolImGuiWin32WndProcHandler
#include "stock_pool_workbench_main.cpp"
#undef ImGui_ImplWin32_WndProcHandler
#undef Render
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
        std::array<char, 262144> clipboardText{};
        ParseResult parsed;
        std::string databaseStatus;
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
        g_historicalImport.conversionStatus =
            "클립보드 내용을 붙였습니다. 변환을 누르십시오.";
    }

    void Convert1516Clipboard()
    {
        g_historicalImport.parsed =
            trading::stock_pool::import1516::ParseClipboardText(
                g_historicalImport.clipboardText.data());
        g_historicalImport.databaseStatus.clear();
        g_historicalImport.converted = true;
        g_historicalImport.cohortCommitted = false;

        if (g_historicalImport.parsed.rows.empty()) {
            g_historicalImport.conversionStatus =
                "변환 가능한 1516 종목 행이 없습니다.";
            return;
        }

        const auto master =
            trading::stock_pool::platform::LoadGate3SymbolMaster(".env");
        if (!master.ok) {
            g_historicalImport.databaseStatus = master.error;
            g_historicalImport.conversionStatus =
                "텍스트 변환은 완료했지만 종목코드 조회에 실패했습니다.";
            return;
        }

        trading::stock_pool::import1516::ResolveExactSymbolNames(
            g_historicalImport.parsed.rows,
            master.entries);
        g_historicalImport.databaseStatus =
            master.source + " | master " +
            std::to_string(master.entries.size()) + "건";

        const std::size_t resolved = CountStatus(ResolutionStatus::Resolved);
        const std::size_t rejected =
            g_historicalImport.parsed.rows.size() - resolved;
        g_historicalImport.conversionStatus =
            "변환 완료: 코드 확정 " + std::to_string(resolved) +
            "개, 폐기 예정 " + std::to_string(rejected) + "개";
    }

    void CommitHistoricalCohort()
    {
        std::vector<MemberSeries> members;
        for (const ImportedRow& row : g_historicalImport.parsed.rows) {
            if (row.status != ResolutionStatus::Resolved) continue;
            MemberSeries member;
            member.code = row.code;
            member.name = row.name;
            member.market = row.market;
            members.push_back(std::move(member));
        }
        if (members.empty()) {
            g_historicalImport.conversionStatus =
                "확정 가능한 종목코드가 없어 cohort를 만들지 않았습니다.";
            return;
        }

        g_state.rawMembers = std::move(members);
        ApplyTimeframe();
        g_historicalImport.cohortCommitted = true;
        g_state.status =
            "1516 Frozen Cohort 확정: " +
            std::to_string(g_state.members.size()) +
            "개 | 다음 단계: 포착시각 이후 실제 분봉 hydration";
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
            "키움 1516 성과검증 우측 검색 종목 목록에서 컨텍스트 메뉴의 복사(Z)를 실행한 뒤 붙여넣으십시오. 7시간·최고수익률은 사후 label로만 보존되며 장중 상대강도 계산에는 사용하지 않습니다.");
        ImGui::Separator();

        if (ImGui::Button("클립보드 붙여넣기")) Paste1516Clipboard();
        ImGui::SameLine();
        if (ImGui::Button("텍스트 지우기")) {
            g_historicalImport.clipboardText.fill('\0');
            g_historicalImport.parsed = {};
            g_historicalImport.converted = false;
            g_historicalImport.cohortCommitted = false;
            g_historicalImport.databaseStatus.clear();
            g_historicalImport.conversionStatus.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("변환 + 종목코드 조회")) {
            Convert1516Clipboard();
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "DB: gate3.g3_symbol_master / exact name / delisted=0");

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
        if (!g_historicalImport.databaseStatus.empty()) {
            ImGui::TextWrapped(
                "종목마스터: %s",
                g_historicalImport.databaseStatus.c_str());
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
        if (ImGui::Button("코드 확정 종목으로 Frozen Cohort 생성")) {
            CommitHistoricalCohort();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (g_historicalImport.cohortCommitted) {
            ImGui::TextColored(
                ImVec4(0.35f, 0.95f, 0.65f, 1.0f),
                "확정 완료: %zu종목",
                g_state.members.size());
            ImGui::SameLine();
        }
        if (ImGui::Button("닫기")) ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

bool ImGui::StockPoolButton(
    const char* label,
    const ImVec2& size)
{
    const bool clicked = ImGui::Button(label, size);
    if (clicked && label != nullptr &&
        std::strcmp(label, "불러오기") == 0 &&
        g_state.sourceMode == SourceMode::Historical1516)
    {
        g_historicalImport.requestOpen = true;
        return false;
    }
    return clicked;
}

void ImGui::StockPoolRender()
{
    Draw1516ImportDialog();
    ImGui::Render();
}
