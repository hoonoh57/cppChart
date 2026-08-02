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

echo.
echo *** ALL HEADLESS TESTS PASSED ***
exit /b 0
