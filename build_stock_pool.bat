@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d %~dp0

rem The stock-pool workbench is an isolated executable. Its build must never
rem delete, replace, or relink the existing Trading Shell executable.
tasklist /FI "IMAGENAME eq stock_pool_workbench.exe" 2>NUL | find /I "stock_pool_workbench.exe" >NUL
if not errorlevel 1 (
    echo *** BUILD BLOCKED: stock_pool_workbench.exe is still running ***
    echo Stop the workbench and build again.
    exit /b 2
)

rem Remove every runnable and object artifact first. A failed build must never
rem leave an older mysql.exe/libmysql-based workbench available to execute.
if exist stock_pool_workbench.exe del /F /Q stock_pool_workbench.exe
if exist stock_pool_workbench_tests.exe del /F /Q stock_pool_workbench_tests.exe
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

echo *** BUILDING ISOLATED STOCK-POOL WORKBENCH WITH SERVER32 HTTP GATEWAY ***
cl /nologo /std:c++17 /utf-8 /O2 /W3 /EHsc /MD /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0602 ^
   /I"." /I"imgui" /I"imgui\backends" ^
   stock_pool_workbench_entry.cpp ^
   core\stock_pool_engine.cpp ^
   core\json_lite.cpp ^
   app\stock_pool_evaluator.cpp ^
   app\stock_pool_fixture.cpp ^
   app\stock_pool_1516_import.cpp ^
   platform\stock_pool_gateway_client.cpp ^
   imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp ^
   imgui\backends\imgui_impl_win32.cpp imgui\backends\imgui_impl_dx11.cpp ^
   /Foobj_stock_pool\ /Fe:stock_pool_workbench.exe ^
   /link winhttp.lib d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib dwmapi.lib
if errorlevel 1 (
    if exist stock_pool_workbench.exe del /F /Q stock_pool_workbench.exe
    echo *** BUILD FAILED - NO stock_pool_workbench.exe WAS LEFT TO RUN ***
    exit /b 1
)

if not exist stock_pool_workbench.exe (
    echo *** BUILD FAILED: linker reported success but executable is missing ***
    exit /b 1
)

set "BUILD_HEAD=unknown"
for /f "delims=" %%I in ('git rev-parse HEAD 2^>NUL') do set "BUILD_HEAD=%%I"
>stock_pool_workbench.build.txt echo head=!BUILD_HEAD!
>>stock_pool_workbench.build.txt echo adapter=server32-http-mysql
>>stock_pool_workbench.build.txt echo executable=stock_pool_workbench.exe

echo.
echo *** BUILD OK -^> stock_pool_workbench.exe [1516 server32 gateway] ***
echo Existing shell.exe was not modified.
echo C++ vcpkg, libmysql, and mysql.exe are not used.
echo Build identity: stock_pool_workbench.build.txt
exit /b 0
