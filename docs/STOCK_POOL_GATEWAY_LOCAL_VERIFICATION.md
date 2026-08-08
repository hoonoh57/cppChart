# Stock-Pool Gateway Local Verification

## 1. Build and start server32

```powershell
Set-Location "E:\2026\gpt\server32"
git pull --ff-only origin main

if (-not (Test-Path .\.env)) {
    Copy-Item .\.env.example .\.env
}
# Edit only .env and set MYSQL_PASSWORD.

.\build.bat
.\bin\Debug\KiwoomServer.exe
```

Expected build statement:

```text
Managed MySqlConnector was restored through NuGet; no native vcpkg build is used.
```

## 2. Verify symbol resolution in another PowerShell window

```powershell
Set-Location "E:\2026\gpt\server32"
.\scripts\verify_stock_pool_gateway.ps1
```

Expected result:

```text
source=gate3.g3_symbol_master
resolved_count=>0
stock-pool gateway smoke test passed
```

A connection, authentication, database, table, or exact-name failure is returned explicitly. Do not continue to the workbench until this test passes.

## 3. Build the isolated C++ workbench

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git pull --ff-only origin p2/kiwoom-mock-gateway

Get-Process stock_pool_workbench -ErrorAction SilentlyContinue |
    Stop-Process -Force

Remove-Item .\stock_pool_workbench.exe -Force -ErrorAction SilentlyContinue
Remove-Item .\stock_pool_workbench.build.txt -Force -ErrorAction SilentlyContinue

.\build_stock_pool.bat
Get-Content .\stock_pool_workbench.build.txt
```

Required identity:

```text
adapter=server32-http-mysql
executable=stock_pool_workbench.exe
```

No `vcpkg`, `libmysql`, or `mysql.exe` restore/search output is allowed.

## 4. Verify 1516 import

```powershell
.\stock_pool_workbench.exe
```

UI sequence:

```text
1516 과거 포착
-> 불러오기
-> 클립보드 붙여넣기
-> 변환 + 종목코드 조회
-> DB 저장 + Frozen Cohort 생성
```

Success requires both stages:

```text
server32 batch resolve
cohort_id=<positive integer>
```

The in-memory cohort is not committed when the DB transaction fails.

## 5. Optional cleanup of the abandoned native dependency attempt

After the new build succeeds, the old local vcpkg cache is no longer used by this project.

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
Remove-Item .\vcpkg_installed -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item .\.tools\vcpkg -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item .\libmysql.dll -Force -ErrorAction SilentlyContinue
```

Do not delete arbitrary DLLs from the repository root; only remove known abandoned MySQL artifacts.
