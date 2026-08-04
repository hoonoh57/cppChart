@echo off
setlocal
cd /d "%~dp0\.."

call :build_and_run core_tests.exe "tests\core_tests.cpp core\command_bus.cpp core\fault_policy.cpp core\json_lite.cpp core\parameter_store.cpp core\trading_state.cpp"
if errorlevel 1 exit /b 1

call :build_and_run trading_date_tests.exe "tests\trading_date_tests.cpp"
if errorlevel 1 exit /b 1

call :build_and_run market_trading_date_tests.exe "tests\market_trading_date_tests.cpp core\json_lite.cpp core\kiwoom_market_data.cpp"
if errorlevel 1 exit /b 1

call :build_and_run indicator_engine_tests.exe "tests\indicator_engine_tests.cpp core\json_lite.cpp core\indicator_engine.cpp core\sma_indicator.cpp"
if errorlevel 1 exit /b 1

call :build_and_run standard_indicators_tests.exe "tests\standard_indicators_tests.cpp core\json_lite.cpp core\indicator_engine.cpp core\standard_indicators.cpp"
if errorlevel 1 exit /b 1

call :build_and_run jma_indicator_tests.exe "tests\jma_indicator_tests.cpp core\json_lite.cpp core\indicator_engine.cpp core\jma_indicator.cpp"
if errorlevel 1 exit /b 1

call :build_and_run obv_indicator_tests.exe "tests\obv_indicator_tests.cpp core\json_lite.cpp core\indicator_engine.cpp core\obv_indicator.cpp"
if errorlevel 1 exit /b 1

call :build_and_run adx_indicator_tests.exe "tests\adx_indicator_tests.cpp core\json_lite.cpp core\indicator_engine.cpp core\adx_indicator.cpp"
if errorlevel 1 exit /b 1

call :build_and_run vwap_indicator_tests.exe "tests\vwap_indicator_tests.cpp core\json_lite.cpp core\indicator_engine.cpp core\vwap_indicator.cpp"
if errorlevel 1 exit /b 1

call :build_and_run indicator_module_tests.exe "tests\indicator_module_tests.cpp core\json_lite.cpp core\indicator_engine.cpp core\sma_indicator.cpp core\jma_indicator.cpp core\obv_indicator.cpp core\adx_indicator.cpp core\vwap_indicator.cpp core\standard_indicators.cpp app\indicator_module.cpp"
if errorlevel 1 exit /b 1

call :build_and_run indicator_module_revision_tests.exe "tests\indicator_module_revision_tests.cpp core\json_lite.cpp core\indicator_engine.cpp core\sma_indicator.cpp core\jma_indicator.cpp core\obv_indicator.cpp core\adx_indicator.cpp core\vwap_indicator.cpp core\standard_indicators.cpp app\indicator_module.cpp"
if errorlevel 1 exit /b 1

call :build_and_run indicator_render_adapter_tests.exe "tests\indicator_render_adapter_tests.cpp app\indicator_render_adapter.cpp render\render_document.cpp"
if errorlevel 1 exit /b 1

call :build_and_run indicator_reference_adapter_tests.exe "tests\indicator_reference_adapter_tests.cpp app\indicator_render_adapter.cpp render\render_document.cpp"
if errorlevel 1 exit /b 1

call :build_and_run default_indicator_render_plan_tests.exe "tests\default_indicator_render_plan_tests.cpp app\default_indicator_render_plan.cpp app\indicator_configuration.cpp app\indicator_properties.cpp app\indicator_render_adapter.cpp render\render_document.cpp"
if errorlevel 1 exit /b 1

call :build_and_run indicator_properties_tests.exe "tests\indicator_properties_tests.cpp app\indicator_properties.cpp"
if errorlevel 1 exit /b 1

call :build_and_run indicator_configuration_tests.exe "tests\indicator_configuration_tests.cpp app\indicator_configuration.cpp app\indicator_properties.cpp app\indicator_render_adapter.cpp render\render_document.cpp"
if errorlevel 1 exit /b 1

call :build_and_run kiwoom_index_realtime_tests.exe "tests\kiwoom_index_realtime_tests.cpp core\json_lite.cpp core\kiwoom_protocol.cpp core\kiwoom_index_realtime.cpp"
if errorlevel 1 exit /b 1

call :build_and_run comparison_module_tests.exe "tests\comparison_module_tests.cpp core\json_lite.cpp core\kiwoom_protocol.cpp core\kiwoom_market_data.cpp core\kiwoom_index_realtime.cpp app\comparison_module.cpp"
if errorlevel 1 exit /b 1

call :build_and_run comparison_render_adapter_tests.exe "tests\comparison_render_adapter_tests.cpp app\comparison_render_adapter.cpp render\render_document.cpp"
if errorlevel 1 exit /b 1

call :build_and_run feature_registry_tests.exe "tests\feature_registry_tests.cpp app\feature_registry.cpp"
if errorlevel 1 exit /b 1

call :build_and_run render_document_tests.exe "tests\render_document_tests.cpp render\render_document.cpp"
if errorlevel 1 exit /b 1

call :build_and_run chart_viewport_tests.exe "tests\chart_viewport_tests.cpp render\chart_viewport.cpp"
if errorlevel 1 exit /b 1

call :build_and_run value_grid_tests.exe "tests\value_grid_tests.cpp render\value_grid.cpp"
if errorlevel 1 exit /b 1

call :build_and_run series_geometry_tests.exe "tests\series_geometry_tests.cpp render\series_geometry.cpp"
if errorlevel 1 exit /b 1

call :build_and_run pane_layout_tests.exe "tests\pane_layout_tests.cpp render\pane_layout.cpp"
if errorlevel 1 exit /b 1

call :build_and_run render_style_tests.exe "tests\render_style_tests.cpp render\render_document.cpp"
if errorlevel 1 exit /b 1

call :build_and_run cursor_label_layout_tests.exe "tests\cursor_label_layout_tests.cpp"
if errorlevel 1 exit /b 1

call :build_and_run time_axis_tests.exe "tests\time_axis_tests.cpp render\time_axis.cpp"
if errorlevel 1 exit /b 1

call :build_and_run time_boundaries_tests.exe "tests\time_boundaries_tests.cpp render\time_boundaries.cpp"
if errorlevel 1 exit /b 1

call :build_and_run market_data_module_tests.exe "tests\market_data_module_tests.cpp core\json_lite.cpp core\kiwoom_market_data.cpp app\market_data_module.cpp"
if errorlevel 1 exit /b 1

call :build_and_run chart_workspace_module_tests.exe "tests\chart_workspace_module_tests.cpp render\render_document.cpp render\market_chart_builder.cpp app\indicator_render_adapter.cpp app\comparison_render_adapter.cpp app\chart_workspace_module.cpp"
if errorlevel 1 exit /b 1

call :build_and_run chart_workspace_indicator_tests.exe "tests\chart_workspace_indicator_tests.cpp render\render_document.cpp render\market_chart_builder.cpp app\indicator_render_adapter.cpp app\comparison_render_adapter.cpp app\chart_workspace_module.cpp"
if errorlevel 1 exit /b 1

call :build_and_run kiwoom_realtime_subscription_tests.exe "tests\kiwoom_realtime_subscription_tests.cpp core\json_lite.cpp core\kiwoom_realtime_subscription.cpp"
if errorlevel 1 exit /b 1

call :build_and_run kiwoom_protocol_tests.exe "tests\kiwoom_protocol_tests.cpp core\json_lite.cpp core\kiwoom_protocol.cpp"
if errorlevel 1 exit /b 1

call :build_and_run kiwoom_market_data_tests.exe "tests\kiwoom_market_data_tests.cpp core\json_lite.cpp core\kiwoom_market_data.cpp"
if errorlevel 1 exit /b 1

call :build_and_run runtime_config_tests.exe "tests\runtime_config_tests.cpp core\runtime_config.cpp"
if errorlevel 1 exit /b 1

call :build_and_run kiwoom_session_tests.exe "tests\kiwoom_session_tests.cpp core\json_lite.cpp core\kiwoom_protocol.cpp core\runtime_config.cpp core\kiwoom_session.cpp"
if errorlevel 1 exit /b 1

call :build_and_run order_coordinator_tests.exe "tests\order_coordinator_tests.cpp core\json_lite.cpp core\kiwoom_protocol.cpp core\trading_state.cpp core\order_coordinator.cpp"
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

call :build_and_run kiwoom_events_tests.exe "tests\kiwoom_events_tests.cpp core\kiwoom_events.cpp"
if errorlevel 1 exit /b 1

call :build_and_run kiwoom_reconciliation_tests.exe "tests\kiwoom_reconciliation_tests.cpp core\json_lite.cpp core\kiwoom_reconciliation.cpp"
if errorlevel 1 exit /b 1

call :build_and_run kiwoom_gateway_core_tests.exe "tests\kiwoom_gateway_core_tests.cpp core\json_lite.cpp core\kiwoom_protocol.cpp core\trading_state.cpp core\order_coordinator.cpp core\kiwoom_events.cpp core\kiwoom_gateway_core.cpp"
if errorlevel 1 exit /b 1

call :build_and_run safe_liquidation_tests.exe "tests\safe_liquidation_tests.cpp core\json_lite.cpp core\kiwoom_protocol.cpp core\trading_state.cpp core\order_coordinator.cpp core\kiwoom_reconciliation.cpp core\safe_liquidation.cpp"
if errorlevel 1 exit /b 1

call :build_and_run kiwoom_runtime_engine_tests.exe "tests\kiwoom_runtime_engine_tests.cpp core\json_lite.cpp core\kiwoom_protocol.cpp core\kiwoom_market_data.cpp core\runtime_config.cpp core\kiwoom_session.cpp core\trading_state.cpp core\order_coordinator.cpp core\kiwoom_events.cpp core\kiwoom_gateway_core.cpp core\kiwoom_reconciliation.cpp core\safe_liquidation.cpp core\kiwoom_runtime_engine.cpp"
if errorlevel 1 exit /b 1

call :build_and_run kiwoom_runtime_runner_tests.exe "tests\kiwoom_runtime_runner_tests.cpp core\json_lite.cpp core\kiwoom_protocol.cpp core\kiwoom_market_data.cpp core\runtime_config.cpp core\kiwoom_session.cpp core\trading_state.cpp core\order_coordinator.cpp core\kiwoom_events.cpp core\kiwoom_gateway_core.cpp core\kiwoom_reconciliation.cpp core\safe_liquidation.cpp core\kiwoom_runtime_engine.cpp core\kiwoom_realtime_subscription.cpp core\kiwoom_index_realtime.cpp platform\kiwoom_runtime_runner.cpp"
if errorlevel 1 exit /b 1

echo.
echo *** ALL HEADLESS TESTS PASSED ***
exit /b 0

:build_and_run
set "testExe=%~1"
set "testSources=%~2"
cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc %testSources% /Fe:%testExe%
if errorlevel 1 exit /b 1
%testExe%
exit /b %errorlevel%
