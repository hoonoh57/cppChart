@echo off
setlocal
cd /d %~dp0
if not exist obj mkdir obj
cl /nologo /std:c++17 /utf-8 /O2 /W3 /EHsc /MD /DUNICODE /D_UNICODE ^
   /I"imgui" /I"imgui\backends" ^
   shell_main.cpp core\command_bus.cpp core\fault_policy.cpp core\runtime_config.cpp core\trading_state.cpp imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp imgui\backends\imgui_impl_win32.cpp imgui\backends\imgui_impl_dx11.cpp ^
   /Foobj\ /Fe:shell.exe ^
   /link d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib dwmapi.lib
if errorlevel 1 ( echo. & echo *** BUILD FAILED *** & exit /b 1 )
echo. & echo *** BUILD OK -^> shell.exe ***
