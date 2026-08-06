@echo off
setlocal
cd /d %~dp0

rem The stock-pool workbench is an isolated executable. Its build must never
rem delete, replace, or relink the existing Trading Shell executable.
tasklist /FI "IMAGENAME eq stock_pool_workbench.exe" 2>NUL | find /I "stock_pool_workbench.exe" >NUL
if not errorlevel 1 (
    echo *** BUILD BLOCKED: stock_pool_workbench.exe is still running ***
    echo Stop the workbench and build again.
    exit /b 2
)

rem 1516 clipboard import resolves names through gate3.g3_symbol_master.
rem Do not require the user to manually locate mysql.exe when it is installed
rem in a standard MySQL, MariaDB, or XAMPP directory. Never overwrite an
rem existing active MYSQL_EXE setting; only append one when it is absent.
if exist .env (
    findstr /R /C:"^[ ]*MYSQL_EXE[ ]*=" .env >NUL 2>NUL
    if errorlevel 1 (
        set "STOCK_POOL_MYSQL_EXE="
        for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$c = [System.Collections.Generic.List[string]]::new(); $cmd = Get-Command mysql.exe -ErrorAction SilentlyContinue; if ($cmd) { $c.Add($cmd.Source) }; $roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramW6432) ^| Where-Object { $_ }; foreach ($r in $roots) { foreach ($vendor in @('MySQL','MariaDB')) { $p = Join-Path $r $vendor; if (Test-Path $p) { Get-ChildItem $p -Filter mysql.exe -File -Recurse -ErrorAction SilentlyContinue ^| ForEach-Object { $c.Add($_.FullName) } } } }; foreach ($p in @((Join-Path $env:SystemDrive 'xampp\mysql\bin\mysql.exe'), (Join-Path $env:SystemDrive 'laragon\bin\mysql'))) { if (Test-Path $p -PathType Leaf) { $c.Add($p) } elseif (Test-Path $p -PathType Container) { Get-ChildItem $p -Filter mysql.exe -File -Recurse -ErrorAction SilentlyContinue ^| ForEach-Object { $c.Add($_.FullName) } } }; $c ^| Where-Object { $_ -and (Test-Path $_ -PathType Leaf) } ^| Select-Object -First 1"`) do set "STOCK_POOL_MYSQL_EXE=%%I"
        if defined STOCK_POOL_MYSQL_EXE (
            >>.env echo.
            >>.env echo # Auto-detected by build_stock_pool.bat for 1516 symbol resolution.
            >>.env echo MYSQL_EXE=%STOCK_POOL_MYSQL_EXE%
            echo *** MYSQL CLIENT AUTO-DETECTED: %STOCK_POOL_MYSQL_EXE% ***
        ) else (
            echo *** MYSQL CLIENT NOT FOUND ***
            echo 1516 text parsing will work, but symbol-code lookup requires mysql.exe.
            echo Install MySQL client or set MYSQL_EXE in .env.
        )
    ) else (
        echo *** MYSQL CLIENT: using active MYSQL_EXE from .env ***
    )
) else (
    echo *** WARNING: .env not found - 1516 MySQL symbol lookup will fail closed ***
)

if exist stock_pool_workbench.exe del /F /Q stock_pool_workbench.exe
if exist stock_pool_workbench_tests.exe del /F /Q stock_pool_workbench_tests.exe
if not exist obj_stock_pool mkdir obj_stock_pool

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

echo *** BUILDING ISOLATED STOCK-POOL WORKBENCH ***
cl /nologo /std:c++17 /utf-8 /O2 /W3 /EHsc /MD /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0602 ^
   /I"." /I"imgui" /I"imgui\backends" ^
   stock_pool_workbench_entry.cpp ^
   core\stock_pool_engine.cpp ^
   app\stock_pool_evaluator.cpp ^
   app\stock_pool_fixture.cpp ^
   app\stock_pool_1516_import.cpp ^
   platform\stock_pool_mysql_symbol_master.cpp ^
   imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp ^
   imgui\backends\imgui_impl_win32.cpp imgui\backends\imgui_impl_dx11.cpp ^
   /Foobj_stock_pool\ /Fe:stock_pool_workbench.exe ^
   /link d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib dwmapi.lib
if errorlevel 1 (
    if exist stock_pool_workbench.exe del /F /Q stock_pool_workbench.exe
    echo *** BUILD FAILED - NO stock_pool_workbench.exe WAS LEFT TO RUN ***
    exit /b 1
)

if not exist stock_pool_workbench.exe (
    echo *** BUILD FAILED: linker reported success but executable is missing ***
    exit /b 1
)

echo.
echo *** BUILD OK -^> stock_pool_workbench.exe [1516 clipboard import] ***
echo Existing shell.exe was not modified.
exit /b 0
