#include "imgui.h"

#include <cstdarg>
#include <cstring>

namespace ImGui
{
    bool M90SmallButton(const char* label);
}

#define SmallButton M90SmallButton
#include "shell_main_m89.cpp"
#undef SmallButton

bool ImGui::M90SmallButton(const char* label)
{
    if (label == nullptr) return ImGui::SmallButton(label);

    if (std::strcmp(label, "이동##date_m87") == 0) {
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

    return ImGui::SmallButton(label);
}
