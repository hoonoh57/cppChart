@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d %~dp0

call :ensure_msvc
if errorlevel 1 exit /b 1

tasklist /FI "IMAGENAME eq stock_pool_workbench.exe" 2>NUL | find /I "stock_pool_workbench.exe" >NUL
if not errorlevel 1 (
    echo *** BUILD BLOCKED: stock_pool_workbench.exe is still running ***
    echo Stop the workbench and build again.
    exit /b 2
)

if exist stock_pool_workbench.exe del /F /Q stock_pool_workbench.exe
if exist stock_pool_workbench_tests.exe del /F /Q stock_pool_workbench_tests.exe
if exist stock_pool_strength_cross_tests.exe del /F /Q stock_pool_strength_cross_tests.exe
if exist intuitive_strength_engine_tests.exe del /F /Q intuitive_strength_engine_tests.exe
if exist intuitive_strength_warmup_tests.exe del /F /Q intuitive_strength_warmup_tests.exe
if exist intuitive_strength_trade_evaluator_tests.exe del /F /Q intuitive_strength_trade_evaluator_tests.exe
if exist stock_pool_workbench.build.txt del /F /Q stock_pool_workbench.build.txt
if exist obj_stock_pool rmdir /S /Q obj_stock_pool
mkdir obj_stock_pool

echo *** VERIFYING STOCK-POOL CAUSAL ENGINE AND 1516 IMPORTER ***
cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc /MD ^
   /I"." ^
   tests\stock_pool_engine_tests.cpp ^
   core\stock_pool_engine.cpp ^
   app\stock_pool_evaluator.cpp ^
   app\stock_pool_fixture.cpp ^
   app\stock_pool_1516_import.cpp ^
   /Foobj_stock_pool\ /Fe:stock_pool_workbench_tests.exe
if errorlevel 1 (
    echo *** BUILD FAILED: stock-pool tests did not compile ***
    exit /b 1
)
stock_pool_workbench_tests.exe
if errorlevel 1 (
    del /F /Q stock_pool_workbench_tests.exe 2>NUL
    echo *** BUILD FAILED: stock-pool tests failed ***
    exit /b 1
)
del /F /Q stock_pool_workbench_tests.exe 2>NUL

echo *** VERIFYING LEGACY STRICT 10-MINUTE STRENGTH-CROSS STRATEGY ***
cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc /MD ^
   /I"." ^
   tests\stock_pool_strength_cross_tests.cpp ^
   app\stock_pool_strength_cross.cpp ^
   core\stock_pool_engine.cpp ^
   /Foobj_stock_pool\ /Fe:stock_pool_strength_cross_tests.exe
if errorlevel 1 (
    echo *** BUILD FAILED: legacy strength-cross tests did not compile ***
    exit /b 1
)
stock_pool_strength_cross_tests.exe
if errorlevel 1 (
    del /F /Q stock_pool_strength_cross_tests.exe 2>NUL
    echo *** BUILD FAILED: legacy strength-cross tests failed ***
    exit /b 1
)
del /F /Q stock_pool_strength_cross_tests.exe 2>NUL

echo *** VERIFYING WARM-START WYSIWYG JMA STRENGTH ENGINE ***
cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc /MD ^
   /I"." ^
   tests\intuitive_strength_engine_tests.cpp ^
   core\intuitive_strength_engine.cpp ^
   core\intuitive_strength_snapshot.cpp ^
   /Foobj_stock_pool\ /Fe:intuitive_strength_engine_tests.exe
if errorlevel 1 (
    echo *** BUILD FAILED: intuitive-strength tests did not compile ***
    exit /b 1
)
intuitive_strength_engine_tests.exe
if errorlevel 1 (
    del /F /Q intuitive_strength_engine_tests.exe 2>NUL
    echo *** BUILD FAILED: intuitive-strength tests failed ***
    exit /b 1
)
del /F /Q intuitive_strength_engine_tests.exe 2>NUL

echo *** VERIFYING PRIOR-SESSION INDICATOR WARM-UP AND 09:03 GATE ***
cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc /MD ^
   /I"." ^
   tests\intuitive_strength_warmup_tests.cpp ^
   core\intuitive_strength_engine.cpp ^
   core\intuitive_strength_snapshot.cpp ^
   /Foobj_stock_pool\ /Fe:intuitive_strength_warmup_tests.exe
if errorlevel 1 (
    echo *** BUILD FAILED: warmup tests did not compile ***
    exit /b 1
)
intuitive_strength_warmup_tests.exe
if errorlevel 1 (
    del /F /Q intuitive_strength_warmup_tests.exe 2>NUL
    echo *** BUILD FAILED: prior-session warmup / 09:03 gate regression ***
    exit /b 1
)
del /F /Q intuitive_strength_warmup_tests.exe 2>NUL

echo *** VERIFYING CAUSAL JMA TRADE EVALUATOR / CROSS-WAVE VS EXECUTION PNL ***
cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc /MD ^
   /I"." ^
   tests\intuitive_strength_trade_evaluator_tests.cpp ^
   core\intuitive_strength_trade_evaluator.cpp ^
   /Foobj_stock_pool\ /Fe:intuitive_strength_trade_evaluator_tests.exe
if errorlevel 1 (
    echo *** BUILD FAILED: causal JMA trade evaluator tests did not compile ***
    exit /b 1
)
intuitive_strength_trade_evaluator_tests.exe
if errorlevel 1 (
    del /F /Q intuitive_strength_trade_evaluator_tests.exe 2>NUL
    echo *** BUILD FAILED: causal JMA trade evaluator regression ***
    exit /b 1
)
del /F /Q intuitive_strength_trade_evaluator_tests.exe 2>NUL

rem Large WinHTTP/JSON adapters are deliberately compiled without optimizer.
rem This isolates the known MSVC C1001 class from calculation/rendering TUs.
echo *** COMPILING SERVER32 GATEWAY ADAPTER WITH MSVC ICE GUARD (/Od /Ob0) ***
cl /nologo /std:c++17 /utf-8 /Od /Ob0 /W3 /EHsc /MD ^
   /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0602 ^
   /I"." ^
   /c platform\stock_pool_gateway_client.cpp ^
   /Foobj_stock_pool\stock_pool_gateway_client.obj
if errorlevel 1 (
    echo *** BUILD FAILED: stock_pool_gateway_client.cpp did not compile ***
    exit /b 1
)
if not exist obj_stock_pool\stock_pool_gateway_client.obj (
    echo *** BUILD FAILED: gateway object is missing ***
    exit /b 1
)

echo *** COMPILING CYBOS TICK-CANDLE RESAMPLING ADAPTER WITH MSVC ICE GUARD (/Od /Ob0) ***
cl /nologo /std:c++17 /utf-8 /Od /Ob0 /W3 /EHsc /MD ^
   /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0602 ^
   /I"." ^
   /c platform\stock_pool_tick_resampling_client.cpp ^
   /Foobj_stock_pool\stock_pool_tick_client.obj
if errorlevel 1 (
    echo *** BUILD FAILED: stock_pool_tick_resampling_client.cpp did not compile ***
    exit /b 1
)
if not exist obj_stock_pool\stock_pool_tick_client.obj (
    echo *** BUILD FAILED: tick client object is missing ***
    exit /b 1
)

echo *** BUILDING PRIOR-SESSION WARM-UP / OPENING TICK WYSIWYG WORKBENCH ***
cl /nologo /std:c++17 /utf-8 /O2 /W3 /EHsc /MD /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0602 ^
   /I"." /I"imgui" /I"imgui\backends" ^
   stock_pool_workbench_tick_entry.cpp ^
   ui\stock_pool_tick_trade_detail_ui.cpp ^
   core\stock_pool_engine.cpp ^
   core\intuitive_strength_engine.cpp ^
   core\intuitive_strength_snapshot.cpp ^
   core\intuitive_strength_trade_evaluator.cpp ^
   core\json_lite.cpp ^
   app\stock_pool_evaluator.cpp ^
   app\stock_pool_fixture.cpp ^
   app\stock_pool_1516_import.cpp ^
   app\stock_pool_strength_cross.cpp ^
   platform\stock_pool_minute_client.cpp ^
   imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp ^
   imgui\backends\imgui_impl_win32.cpp imgui\backends\imgui_impl_dx11.cpp ^
   obj_stock_pool\stock_pool_gateway_client.obj ^
   obj_stock_pool\stock_pool_tick_client.obj ^
   /Foobj_stock_pool\ /Fe:stock_pool_workbench.exe ^
   /link winhttp.lib d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib dwmapi.lib
if errorlevel 1 (
    if exist stock_pool_workbench.exe del /F /Q stock_pool_workbench.exe
    echo *** BUILD FAILED: tick-strength workbench compile/link returned an error ***
    exit /b 1
)
if not exist stock_pool_workbench.exe (
    echo *** BUILD FAILED: executable is missing ***
    exit /b 1
)

set "BUILD_HEAD=unknown"
for /f "delims=" %%I in ('git rev-parse HEAD 2^>NUL') do set "BUILD_HEAD=%%I"
>stock_pool_workbench.build.txt echo head=!BUILD_HEAD!
>>stock_pool_workbench.build.txt echo adapter=server32-http-mysql
>>stock_pool_workbench.build.txt echo market_data=server32-cybos-real-Tn-candles
>>stock_pool_workbench.build.txt echo analysis_date=user-selected-trading-date
>>stock_pool_workbench.build.txt echo native_tick_limit=120
>>stock_pool_workbench.build.txt echo derived_tick_sizes=T180:T60x3,T360:T120x3,T720:T120x6
>>stock_pool_workbench.build.txt echo tick_sizes=60,120,180,360,720
>>stock_pool_workbench.build.txt echo historical_tick_timestamp=HHmm-minute-resolution
>>stock_pool_workbench.build.txt echo within_minute_render=cybos-order-preserved-even-spacing
>>stock_pool_workbench.build.txt echo indicator_warmup=latest-prior-session-tail
>>stock_pool_workbench.build.txt echo indicator_reset_at_0900=false
>>stock_pool_workbench.build.txt echo wave_state_reset_at_0900=true
>>stock_pool_workbench.build.txt echo evaluation_window=09:03:00-10:00:00
>>stock_pool_workbench.build.txt echo snapshot_alignment=minute-close-latest-completed-Tn-candle
>>stock_pool_workbench.build.txt echo chart_x_axis=real-minute-plus-order-preserving-within-minute-layout
>>stock_pool_workbench.build.txt echo tick_participation=completed-Tn-bars-per-minute-times-n
>>stock_pool_workbench.build.txt echo trend_strength=jma7-50-2_vs_jma20-50-2
>>stock_pool_workbench.build.txt echo detail_drilldown=double-click-time-axis-crosshair-jma-gate
>>stock_pool_workbench.build.txt echo trade_evaluation=cross-wave-vs-next-Tn-open-causal-execution
>>stock_pool_workbench.build.txt echo trade_metrics=session,cross-wave,gross,net,mfe,mae
>>stock_pool_workbench.build.txt echo trade_cost_defaults=fee0.015pct-each-side,selltax0.15pct,slippage2bps
>>stock_pool_workbench.build.txt echo legacy_relative_strength=available-by-checkbox
>>stock_pool_workbench.build.txt echo executable=stock_pool_workbench.exe
>>stock_pool_workbench.build.txt echo compiler=!CL_PATH!

echo.
echo *** BUILD OK -^> stock_pool_workbench.exe [warm-start opening tick WYSIWYG] ***
echo Existing shell.exe was not modified.
echo User-selected trading date is authoritative for target-session candles.
echo Prior-session bars warm indicators but never create today's buy state.
echo Buy priority is emitted only from 09:03 through 10:00.
echo CYBOS native tick request never exceeds T120; larger sizes use completed real base candles.
echo Tick detail drill-down uses the same bars and intuitive-strength series as the overview.
echo Trade evaluation separates session return, cross-wave capture, causal gross/net PnL, MFE and MAE.
echo Causal BUY/SELL fills occur at the next completed Tn bar open after the confirmed signal; open positions are MTM only.
echo Historical Tn timestamps are minute-resolution; no fake seconds are claimed.
echo Tick density is completed Tn bars per minute times n, never inferred from volume.
echo Build identity: stock_pool_workbench.build.txt
exit /b 0

:ensure_msvc
where cl >NUL 2>&1
if not errorlevel 1 goto :msvc_ready

echo *** INITIALIZING MSVC BUILD ENVIRONMENT ***
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INSTALL="
set "VSDEV="

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>NUL`) do set "VS_INSTALL=%%I"
)

if defined VS_INSTALL (
    if exist "!VS_INSTALL!\Common7\Tools\VsDevCmd.bat" set "VSDEV=!VS_INSTALL!\Common7\Tools\VsDevCmd.bat"
)

if not defined VSDEV if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" set "VSDEV=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
if not defined VSDEV if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" set "VSDEV=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
if not defined VSDEV if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" set "VSDEV=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not defined VSDEV if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" set "VSDEV=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if not defined VSDEV if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\Common7\Tools\VsDevCmd.bat" set "VSDEV=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\Common7\Tools\VsDevCmd.bat"
if not defined VSDEV if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\Common7\Tools\VsDevCmd.bat" set "VSDEV=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\Common7\Tools\VsDevCmd.bat"
if not defined VSDEV if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat" set "VSDEV=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat"
if not defined VSDEV if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat" set "VSDEV=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat"

if not defined VSDEV (
    echo *** BUILD FAILED: MSVC C++ build tools were not found ***
    exit /b 1
)

echo MSVC environment script: !VSDEV!
set "VSCMD_SKIP_SENDTELEMETRY=1"
call "!VSDEV!" -no_logo -arch=x64 -host_arch=x64
where cl >NUL 2>&1
if errorlevel 1 (
    echo *** BUILD FAILED: VsDevCmd completed but cl.exe is still unavailable ***
    exit /b 1
)

:msvc_ready
set "CL_PATH="
for /f "delims=" %%I in ('where cl 2^>NUL') do if not defined CL_PATH set "CL_PATH=%%I"
echo MSVC compiler: !CL_PATH!
exit /b 0