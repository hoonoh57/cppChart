#include <cstring>

#include "imgui.h"

namespace ImGui
{
    void StockPoolTableSetupColumn(
        const char* label,
        ImGuiTableColumnFlags flags = 0,
        float initWidthOrWeight = 0.0f,
        ImGuiID userId = 0);
}

#define TableSetupColumn StockPoolTableSetupColumn
#include "stock_pool_workbench_entry.cpp"
#undef TableSetupColumn

void ImGui::StockPoolTableSetupColumn(
    const char* label,
    ImGuiTableColumnFlags flags,
    float initWidthOrWeight,
    ImGuiID userId)
{
    const char* actual = label;
    if (label != nullptr && std::strcmp(label, "1분%") == 0) {
        actual = "직전봉%";
    }
    else if (label != nullptr && std::strcmp(label, "5분%") == 0) {
        actual = "5봉%";
    }
    else if (label != nullptr && std::strcmp(label, "거래대금") == 0) {
        actual = "거래대금%ile";
    }

    ImGui::TableSetupColumn(
        actual,
        flags,
        initWidthOrWeight,
        userId);
}
