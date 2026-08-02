@echo off
setlocal
cd /d %~dp0\..

cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\core_tests.cpp ^
  core\command_bus.cpp ^
  core\fault_policy.cpp ^
  core\json_lite.cpp ^
  core\parameter_store.cpp ^
  core\trading_state.cpp ^
  /Fe:core_tests.exe
if errorlevel 1 exit /b 1
core_tests.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\kiwoom_protocol_tests.cpp ^
  core\json_lite.cpp ^
  core\kiwoom_protocol.cpp ^
  /Fe:kiwoom_protocol_tests.exe
if errorlevel 1 exit /b 1
kiwoom_protocol_tests.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\runtime_config_tests.cpp ^
  core\runtime_config.cpp ^
  /Fe:runtime_config_tests.exe
if errorlevel 1 exit /b 1
runtime_config_tests.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\kiwoom_session_tests.cpp ^
  core\json_lite.cpp ^
  core\kiwoom_protocol.cpp ^
  core\runtime_config.cpp ^
  core\kiwoom_session.cpp ^
  /Fe:kiwoom_session_tests.exe
if errorlevel 1 exit /b 1
kiwoom_session_tests.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\order_coordinator_tests.cpp ^
  core\json_lite.cpp ^
  core\kiwoom_protocol.cpp ^
  core\trading_state.cpp ^
  core\order_coordinator.cpp ^
  /Fe:order_coordinator_tests.exe
if errorlevel 1 exit /b 1
order_coordinator_tests.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc /D_WIN32_WINNT=0x0602 ^
  tests\winhttp_transport_tests.cpp ^
  core\json_lite.cpp ^
  core\kiwoom_protocol.cpp ^
  platform\winhttp_transport.cpp ^
  /Fe:winhttp_transport_tests.exe ^
  /link winhttp.lib
if errorlevel 1 exit /b 1
winhttp_transport_tests.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\kiwoom_events_tests.cpp ^
  core\kiwoom_events.cpp ^
  /Fe:kiwoom_events_tests.exe
if errorlevel 1 exit /b 1
kiwoom_events_tests.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\kiwoom_reconciliation_tests.cpp ^
  core\json_lite.cpp ^
  core\kiwoom_reconciliation.cpp ^
  /Fe:kiwoom_reconciliation_tests.exe
if errorlevel 1 exit /b 1
kiwoom_reconciliation_tests.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\kiwoom_gateway_core_tests.cpp ^
  core\json_lite.cpp ^
  core\kiwoom_protocol.cpp ^
  core\trading_state.cpp ^
  core\order_coordinator.cpp ^
  core\kiwoom_events.cpp ^
  core\kiwoom_gateway_core.cpp ^
  /Fe:kiwoom_gateway_core_tests.exe
if errorlevel 1 exit /b 1
kiwoom_gateway_core_tests.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\safe_liquidation_tests.cpp ^
  core\json_lite.cpp ^
  core\kiwoom_protocol.cpp ^
  core\trading_state.cpp ^
  core\order_coordinator.cpp ^
  core\kiwoom_reconciliation.cpp ^
  core\safe_liquidation.cpp ^
  /Fe:safe_liquidation_tests.exe
if errorlevel 1 exit /b 1
safe_liquidation_tests.exe
if errorlevel 1 exit /b 1

echo.
echo *** ALL HEADLESS TESTS PASSED ***
exit /b 0
