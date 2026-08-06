#include <windows.h>

#include <algorithm>

#include "imgui.h"
#include "imgui_impl_win32.h"

// Dear ImGui's Win32 backend callback lives in the global namespace.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window,
    UINT message,
    WPARAM wordParameter,
    LPARAM longParameter);

namespace
{
    double Clamp(double value, double minimum, double maximum)
    {
        return (std::max)(minimum, (std::min)(maximum, value));
    }

    // The workbench implementation deliberately keeps its process state in an
    // unnamed namespace. Give its local callback declaration a unique name so
    // argument-dependent lookup cannot see both the local and global ImGui
    // declarations and report an ambiguous overload.
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

#define ImGui_ImplWin32_WndProcHandler StockPoolImGuiWin32WndProcHandler
#include "stock_pool_workbench_main.cpp"
#undef ImGui_ImplWin32_WndProcHandler
