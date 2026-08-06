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

rem Remove every runnable or diagnostic artifact before dependency restore.
rem A failed vcpkg restore must never leave an older mysql.exe-based binary
rem available for accidental execution.
if exist stock_pool_workbench.exe del /F /Q stock_pool_workbench.exe
if exist stock_pool_workbench_tests.exe del /F /Q stock_pool_workbench_tests.exe
if exist stock_pool_workbench.build.txt del /F /Q stock_pool_workbench.build.txt
if exist obj_stock_pool rmdir /S /Q obj_stock_pool
mkdir obj_stock_pool

set "VCPKG_DISABLE_METRICS=1"
set "VCPKG_EXE="

if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\vcpkg.exe" set "VCPKG_EXE=%VCPKG_ROOT%\vcpkg.exe"
if not defined VCPKG_EXE (
    for /f "delims=" %%I in ('where vcpkg.exe 2^>NUL') do if not defined VCPKG_EXE set "VCPKG_EXE=%%I"
)

if not defined VCPKG_EXE (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq delims=" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
        if defined VS_INSTALL if exist "!VS_INSTALL!\VC\vcpkg\vcpkg.exe" set "VCPKG_EXE=!VS_INSTALL!\VC\vcpkg\vcpkg.exe"
    )
)

if not defined VCPKG_EXE (
    set "LOCAL_VCPKG=%CD%\.tools\vcpkg"
    if not exist "!LOCAL_VCPKG!\vcpkg.exe" (
        echo *** VCPKG NOT FOUND - BOOTSTRAPPING PROJECT-LOCAL COPY ***
        if not exist "%CD%\.tools" mkdir "%CD%\.tools"
        if not exist "!LOCAL_VCPKG!\.git" (
            git clone --depth 1 https://github.com/microsoft/vcpkg.git "!LOCAL_VCPKG!"
            if errorlevel 1 (
                echo *** BUILD FAILED: vcpkg clone failed ***
                exit /b 1
            )
        )
        call "!LOCAL_VCPKG!\bootstrap-vcpkg.bat" -disableMetrics
        if errorlevel 1 (
            echo *** BUILD FAILED: vcpkg bootstrap failed ***
            exit /b 1
        )
    )
    set "VCPKG_EXE=!LOCAL_VCPKG!\vcpkg.exe"
)

if not exist "%VCPKG_EXE%" (
    echo *** BUILD FAILED: vcpkg.exe could not be resolved ***
    exit /b 1
)

echo *** RESTORING NATIVE MYSQL CLIENT WITH VCPKG ***
echo vcpkg=%VCPKG_EXE%
set "VCPKG_INSTALLED=%CD%\vcpkg_installed"
"%VCPKG_EXE%" install --triplet x64-windows --x-manifest-root="%CD%" --x-install-root="%VCPKG_INSTALLED%" --disable-metrics
if errorlevel 1 (
    echo *** BUILD FAILED: vcpkg libmysql restore failed ***
    exit /b 1
)

set "VCPKG_TRIPLET_ROOT=%VCPKG_INSTALLED%\x64-windows"
if not exist "%VCPKG_TRIPLET_ROOT%\include" (
    echo *** BUILD FAILED: vcpkg include directory is missing ***
    exit /b 1
)

set "MYSQL_LIBRARY="
if exist "%VCPKG_TRIPLET_ROOT%\lib\libmysql.lib" set "MYSQL_LIBRARY=libmysql.lib"
if not defined MYSQL_LIBRARY if exist "%VCPKG_TRIPLET_ROOT%\lib\mysqlclient.lib" set "MYSQL_LIBRARY=mysqlclient.lib"
if not defined MYSQL_LIBRARY (
    echo *** BUILD FAILED: libmysql import library is missing ***
    dir /B "%VCPKG_TRIPLET_ROOT%\lib\*.lib" 2>NUL
    exit /b 1
)

echo *** MYSQL CLIENT LIBRARY: %MYSQL_LIBRARY% ***

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

echo *** BUILDING ISOLATED STOCK-POOL WORKBENCH WITH NATIVE LIBMYSQL ***
cl /nologo /std:c++17 /utf-8 /O2 /W3 /EHsc /MD /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0602 ^
   /I"." /I"imgui" /I"imgui\backends" ^
   /I"%VCPKG_TRIPLET_ROOT%\include" /I"%VCPKG_TRIPLET_ROOT%\include\mysql" ^
   stock_pool_workbench_entry.cpp ^
   core\stock_pool_engine.cpp ^
   app\stock_pool_evaluator.cpp ^
   app\stock_pool_fixture.cpp ^
   app\stock_pool_1516_import.cpp ^
   platform\stock_pool_mysql_symbol_master.cpp ^
   imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp ^
   imgui\backends\imgui_impl_win32.cpp imgui\backends\imgui_impl_dx11.cpp ^
   /Foobj_stock_pool\ /Fe:stock_pool_workbench.exe ^
   /link /LIBPATH:"%VCPKG_TRIPLET_ROOT%\lib" %MYSQL_LIBRARY% ^
   d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib dwmapi.lib
if errorlevel 1 (
    if exist stock_pool_workbench.exe del /F /Q stock_pool_workbench.exe
    echo *** BUILD FAILED - NO stock_pool_workbench.exe WAS LEFT TO RUN ***
    exit /b 1
)

if not exist stock_pool_workbench.exe (
    echo *** BUILD FAILED: linker reported success but executable is missing ***
    exit /b 1
)

rem x64-windows is a dynamic triplet. App-local the native client and all of
rem its vcpkg runtime dependencies beside the isolated workbench executable.
if exist "%VCPKG_TRIPLET_ROOT%\bin\*.dll" (
    for %%F in ("%VCPKG_TRIPLET_ROOT%\bin\*.dll") do copy /Y "%%~fF" "%CD%\" >NUL
)

set "BUILD_HEAD=unknown"
for /f "delims=" %%I in ('git rev-parse HEAD 2^>NUL') do set "BUILD_HEAD=%%I"
>stock_pool_workbench.build.txt echo head=!BUILD_HEAD!
>>stock_pool_workbench.build.txt echo adapter=native-libmysql
>>stock_pool_workbench.build.txt echo executable=stock_pool_workbench.exe

echo.
echo *** BUILD OK -^> stock_pool_workbench.exe [1516 native libmysql import] ***
echo Existing shell.exe was not modified.
echo MySQL server installation path and mysql.exe are not required.
echo Build identity: stock_pool_workbench.build.txt
exit /b 0
