@echo off
setlocal
cd /d %~dp0
if not exist obj mkdir obj
cl /nologo /std:c++17 /utf-8 /O2 /W3 /EHsc /MD /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0602 ^
   /I"imgui" /I"imgui\backends" ^
   shell_main_m86.cpp ^
   core\command_bus.cpp ^
   core\fault_policy.cpp ^
   core\json_lite.cpp ^
   core\indicator_engine.cpp ^
   core\sma_indicator.cpp ^
   core\standard_indicators.cpp ^
   core\jma_indicator.cpp ^
   core\obv_indicator.cpp ^
   core\adx_indicator.cpp ^
   core\vwap_indicator.cpp ^
   core\kiwoom_protocol.cpp ^
   core\kiwoom_realtime_subscription.cpp ^
   core\kiwoom_market_data.cpp ^
   core\kiwoom_index_realtime.cpp ^
   core\kiwoom_symbol_catalog.cpp ^
   core\runtime_config.cpp ^
   core\kiwoom_session.cpp ^
   core\trading_state.cpp ^
   core\order_coordinator.cpp ^
   core\kiwoom_events.cpp ^
   core\kiwoom_gateway_core.cpp ^
   core\kiwoom_reconciliation.cpp ^
   core\safe_liquidation.cpp ^
   core\kiwoom_runtime_engine.cpp ^
   app\feature_registry.cpp ^
   app\market_data_module.cpp ^
   app\chart_workspace_module.cpp ^
   app\indicator_module.cpp ^
   app\indicator_render_adapter.cpp ^
   app\indicator_configuration.cpp ^
   app\default_indicator_render_plan.cpp ^
   app\indicator_properties.cpp ^
   app\indicator_workspace_coordinator.cpp ^
   app\comparison_transform.cpp ^
   app\comparison_module.cpp ^
   app\comparison_render_adapter.cpp ^
   app\symbol_master_cache.cpp ^
   app\symbol_master_cache_compat.cpp ^
   render\chart_viewport.cpp ^
   render\time_axis.cpp ^
   render\value_grid.cpp ^
   render\value_viewport.cpp ^
   render\series_geometry.cpp ^
   render\pane_layout.cpp ^
   render\time_boundaries.cpp ^
   render\render_document.cpp ^
   render\market_chart_builder.cpp ^
   ui\render_document_renderer.cpp ^
   ui\indicator_manager_ui.cpp ^
   ui\symbol_search_ui.cpp ^
   ui\comparison_manager_ui.cpp ^
   platform\kiwoom_runtime_runner.cpp ^
   platform\winhttp_transport.cpp ^
   platform\winhttp_kiwoom_transport.cpp ^
   imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp ^
   imgui\backends\imgui_impl_win32.cpp imgui\backends\imgui_impl_dx11.cpp ^
   /Foobj\ /Fe:shell.exe ^
   /link d3d11.lib dxgi.lib d3dcompiler.lib winhttp.lib user32.lib gdi32.lib dwmapi.lib
if errorlevel 1 ( echo. & echo *** BUILD FAILED *** & exit /b 1 )
echo. & echo *** BUILD OK -^> shell.exe ***
