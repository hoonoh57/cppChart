#include <windows.h>

#include "imgui.h"
#include "imgui_impl_win32.h"

// imgui_impl_win32 exposes this callback in the global namespace.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window,
    UINT message,
    WPARAM wordParameter,
    LPARAM longParameter);

// stock_pool_workbench_main.cpp intentionally keeps all process state in an
// unnamed namespace. Reopen that same translation-unit namespace and provide
// the local forwarding definition expected by its Win32 window procedure.
namespace
{
    LRESULT ImGui_ImplWin32_WndProcHandler(
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

#include "stock_pool_workbench_main.cpp"
