#include "imgui.h"

namespace ImGui
{
    bool M86BeginListBox(
        const char* label,
        const ImVec2& sizeArg);

    void M86EndListBox();
}

#define BeginListBox M86BeginListBox
#define EndListBox M86EndListBox
#include "shell_main_m85.cpp"
#undef EndListBox
#undef BeginListBox

namespace
{
    bool g_m86SymbolOverlayBegun = false;
}

bool ImGui::M86BeginListBox(
    const char*,
    const ImVec2& sizeArg)
{
    const ImVec2 anchor = ImGui::GetCursorScreenPos();
    const ImGuiViewport* viewport = ImGui::GetWindowViewport();

    ImVec2 size = sizeArg;
    if (size.x <= 0.0f) size.x = 300.0f;
    if (size.y <= 0.0f) size.y = 180.0f;

    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    if (viewport != nullptr) {
        ImGui::SetNextWindowViewport(viewport->ID);
    }
    ImGui::SetNextWindowBgAlpha(0.99f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("##m86_toolbar_symbol_overlay", nullptr, flags);
    g_m86SymbolOverlayBegun = true;
    return true;
}

void ImGui::M86EndListBox()
{
    if (!g_m86SymbolOverlayBegun) return;

    const bool hovered = ImGui::IsWindowHovered(
        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::End();
    ImGui::PopStyleVar();
    g_m86SymbolOverlayBegun = false;

    const ImGuiID symbolInputId = ImGui::GetID("##symbol");
    if (!hovered &&
        ImGui::GetActiveID() != symbolInputId &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        g_m82ToolbarPopupOpen = false;
        g_m82ToolbarHighlight = -1;
    }
}
