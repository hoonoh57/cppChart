@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d %~dp0

rem The stock-pool workbench must build from an ordinary Command Prompt or
rem PowerShell session. Do not require the caller to open a VS Developer Prompt.
call :ensure_msvc
if errorlevel 1 exit /b 1

rem The stock-pool workbench is an isolated executable. Its build must never
rem delete, replace, or relink the existing Trading Shell executable.
tasklist /FI "IMAGENAME eq stock_pool_workbench.exe" 2>NUL | find /I "stock_pool_workbench.exe" >NUL
if not errorlevel 1 (
    echo *** BUILD BLOCKED: stock_pool_workbench.exe is still running ***
    echo Stop the workbench and build again.
    exit /b 2
)

rem Remove every runnable and object artifact first. A failed build must never
rem leave an older workbench available to execute.
if exist stock_pool_workbench.exe del /F /Q stock_pool_workbench.exe
if exist stock_pool_workbench_tests.exe del /F /Q stock_pool_workbench_tests.exe
if exist stock_pool_strength_cross_tests.exe del /F /Q stock_pool_strength_cross_tests.exe
if exist intuitive_strength_engine_tests.exe del /F /Q intuitive_strength_engine_tests.exe
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

echo *** VERIFYING WYSIWYG INTUITIVE JMA STRENGTH ENGINE ***
cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc /MD ^
   /I"." ^
   tests\intuitive_strength_engine_tests.cpp ^
   core\intuitive_strength_engine.cpp ^
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

rem MSVC 19.x has reproduced C1001 in the optimizer for this WinHTTP/JSON
rem translation unit. Compile only this adapter with optimization disabled and
rem link the verified object into the otherwise /O2 workbench.
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
    echo *** BUILD FAILED: gateway compiler returned success but object is missing ***
    exit /b 1
)

echo *** BUILDING WYSIWYG INTUITIVE-STRENGTH STOCK-POOL WORKBENCH ***
cl /nologo /std:c++17 /utf-8 /O2 /W3 /EHsc /MD /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0602 ^
   /I"." /I"imgui" /I"imgui\backends" ^
   stock_pool_workbench_intuitive_entry.cpp ^
   core\stock_pool_engine.cpp ^
   core\intuitive_strength_engine.cpp ^
   core\json_lite.cpp ^
   app\stock_pool_evaluator.cpp ^
   app\stock_pool_fixture.cpp ^
   app\stock_pool_1516_import.cpp ^
   app\stock_pool_strength_cross.cpp ^
   platform\stock_pool_minute_client.cpp ^
   imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp ^
   imgui\backends\imgui_impl_win32.cpp imgui\backends\imgui_impl_dx11.cpp ^
   obj_stock_pool\stock_pool_gateway_client.obj ^
   /Foobj_stock_pool\ /Fe:stock_pool_workbench.exe ^
   /link winhttp.lib d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib dwmapi.lib
if errorlevel 1 (
    if exist stock_pool_workbench.exe del /F /Q stock_pool_workbench.exe
    echo *** BUILD FAILED: intuitive workbench compile/link command returned an error ***
    exit /b 1
)

if not exist stock_pool_workbench.exe (
    echo *** BUILD FAILED: compile/link command returned success but executable is missing ***
    exit /b 1
)

set "BUILD_HEAD=unknown"
for /f "delims=" %%I in ('git rev-parse HEAD 2^>NUL') do set "BUILD_HEAD=%%I"
>stock_pool_workbench.build.txt echo head=!BUILD_HEAD!
>>stock_pool_workbench.build.txt echo adapter=server32-http-mysql
>>stock_pool_workbench.build.txt echo adapter_compile=msvc-ice-guard-od-ob0
>>stock_pool_workbench.build.txt echo market_data=server32-cybos-minute
>>stock_pool_workbench.build.txt echo default_view=wysiwyg-intuitive-strength
>>stock_pool_workbench.build.txt echo trend_strength=jma7-50-2_vs_jma20-50-2
>>stock_pool_workbench.build.txt echo buy_priority=fresh-cross-jma-slope
>>stock_pool_workbench.build.txt echo tick_participation=NA-until-real-execution-adapter
>>stock_pool_workbench.build.txt echo legacy_relative_strength=available-by-checkbox
>>stock_pool_workbench.build.txt echo executable=stock_pool_workbench.exe
>>stock_pool_workbench.build.txt echo compiler=!CL_PATH!

echo.
echo *** BUILD OK -^> stock_pool_workbench.exe [WYSIWYG intuitive strength] ***
echo Existing shell.exe was not modified.
echo Relative-strength engine remains available only as Legacy fallback.
echo Tick participation is NOT synthesized from volume.
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
    if exist "!VS_INSTALL!\Common7\Tools\VsDevCmd.bat" (
        set "VSDEV=!VS_INSTALL!\Common7\Tools\VsDevCmd.bat"
    )
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
    echo Install Visual Studio Build Tools with the Desktop development with C++ workload.
    echo Required component: Microsoft.VisualStudio.Component.VC.Tools.x86.x64
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
