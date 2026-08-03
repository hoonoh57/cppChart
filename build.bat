@echo off
setlocal
cd /d %~dp0
if not exist obj mkdir obj
cl /nologo /std:c++17 /utf-8 /O2 /W3 /EHsc /MD /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0602 ^
   /I"imgui" /I"imgui\backends" ^
   shell_main.cpp ^
   core\command_bus.cpp ^
   core\fault_policy.cpp ^
   core\json_lite.cpp ^
   core\kiwoom_protocol.cpp ^
   core\kiwoom_market_data.cpp ^
   core\runtime_config.cpp ^
   core\kiwoom_session.cpp ^
   core\trading_state.cpp ^
   core\order_coordinator.cpp ^
   core\kiwoom_events.cpp ^
   core\kiwoom_gateway_core.cpp ^
   core\kiwoom_reconciliation.cpp ^
   core\safe_liquidation.cpp ^
   core\kiwoom_runtime_engine.cpp ^
   platform\kiwoom_runtime_runner.cpp ^
   platform\winhttp_transport.cpp ^
   platform\winhttp_kiwoom_transport.cpp ^
   imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp ^
   imgui\backends\imgui_impl_win32.cpp imgui\backends\imgui_impl_dx11.cpp ^
   /Foobj\ /Fe:shell.exe ^
   /link d3d11.lib dxgi.lib d3dcompiler.lib winhttp.lib user32.lib gdi32.lib dwmapi.lib
if errorlevel 1 ( echo. & echo *** BUILD FAILED *** & exit /b 1 )
echo. & echo *** BUILD OK -^> shell.exe ***
