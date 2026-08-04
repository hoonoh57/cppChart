from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding='utf-8-sig')


def write(path, text):
    (ROOT / path).write_text(text, encoding='utf-8-sig', newline='')


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected 1 match, found {count}')
    return text.replace(old, new, 1)

# UI header
p = 'ui/comparison_manager_ui.h'
t = read(p)
t = replace_once(t, '#include "../app/comparison_module.h"\n', '#include "../app/comparison_module.h"\n#include "../core/kiwoom_symbol_catalog.h"\n', 'ui catalog include')
t = replace_once(t, '        int addPlacement = 0;\n', '        int addPlacement = 0;\n        int addValueMode = 0;\n        char searchQuery[96]{};\n', 'ui add mode state')
t = replace_once(t, '    using RequestComparisonData = bool(*)(\n        const std::string&,\n        std::string&);\n', '    using RequestComparisonData = bool(*)(\n        const std::string&,\n        std::string&);\n\n    using RefreshSymbolCatalog = bool(*)(std::string&);\n', 'ui refresh type')
t = replace_once(t, '        const app::ComparisonModuleSnapshot& snapshot,\n        ComparisonManagerUiState& state,\n        ApplyComparisonDefinitions applyDefinitions,\n        RequestComparisonData requestData);\n', '        const app::ComparisonModuleSnapshot& snapshot,\n        const std::vector<SymbolCatalogEntry>& symbolCatalog,\n        ComparisonManagerUiState& state,\n        ApplyComparisonDefinitions applyDefinitions,\n        RequestComparisonData requestData,\n        RefreshSymbolCatalog refreshCatalog);\n', 'ui draw signature')
write(p, t)

# UI implementation
p = 'ui/comparison_manager_ui.cpp'
t = read(p)
t = replace_once(t, '            definition.placement = state.addPlacement == 1\n                ? app::ComparisonPlacement::PriceSecondaryAxis\n                : app::ComparisonPlacement::SeparatePane;\n', '            definition.placement = state.addPlacement == 1\n                ? app::ComparisonPlacement::PriceSecondaryAxis\n                : app::ComparisonPlacement::SeparatePane;\n            definition.valueMode = static_cast<app::ComparisonValueMode>(\n                (std::max)(0, (std::min)(3, state.addValueMode)));\n', 'new definition value mode')
t = replace_once(t, '        void DrawAddPopup(\n            std::vector<app::ComparisonDefinition>& definitions,\n            ComparisonManagerUiState& state,\n            ApplyComparisonDefinitions applyDefinitions,\n            RequestComparisonData requestData)\n', '        void DrawAddPopup(\n            std::vector<app::ComparisonDefinition>& definitions,\n            const std::vector<SymbolCatalogEntry>& symbolCatalog,\n            ComparisonManagerUiState& state,\n            ApplyComparisonDefinitions applyDefinitions,\n            RequestComparisonData requestData,\n            RefreshSymbolCatalog refreshCatalog)\n', 'add popup signature')
t = replace_once(t, '            const char* kinds[] = { "종목", "지수/업종" };\n            ImGui::Combo("종류", &state.addKind, kinds, 2);\n            ImGui::InputText(\n                "코드",\n                state.addCode,\n                sizeof(state.addCode));\n            ImGui::InputText(\n                "표시명",\n                state.addName,\n                sizeof(state.addName));\n', '            const char* kinds[] = { "종목", "지수/업종" };\n            ImGui::Combo("종류", &state.addKind, kinds, 2);\n            if (state.addKind == 0) {\n                ImGui::InputTextWithHint(\n                    "종목 검색",\n                    "코드 또는 한글 종목명",\n                    state.searchQuery,\n                    sizeof(state.searchQuery));\n                if (ImGui::Button("종목 목록 새로고침")) {\n                    std::string refreshError;\n                    if (refreshCatalog == nullptr || !refreshCatalog(refreshError)) {\n                        state.error = refreshError.empty()\n                            ? "종목 목록 조회를 시작하지 못했습니다."\n                            : refreshError;\n                    }\n                }\n                ImGui::SameLine();\n                ImGui::TextDisabled("%zu종목", symbolCatalog.size());\n                const std::vector<SymbolCatalogEntry> matches =\n                    SearchSymbolCatalog(symbolCatalog, state.searchQuery, 12U);\n                if (!matches.empty() && ImGui::BeginListBox(\n                        "##symbol_matches", ImVec2(-1.0f, 150.0f)))\n                {\n                    for (const SymbolCatalogEntry& entry : matches) {\n                        const std::string label = entry.code + "  " +\n                            entry.name + "  [" + entry.market + "]";\n                        if (ImGui::Selectable(label.c_str())) {\n                            std::snprintf(state.addCode, sizeof(state.addCode),\n                                "%s", entry.code.c_str());\n                            std::snprintf(state.addName, sizeof(state.addName),\n                                "%s", entry.name.c_str());\n                            std::snprintf(state.searchQuery, sizeof(state.searchQuery),\n                                "%s", entry.name.c_str());\n                        }\n                    }\n                    ImGui::EndListBox();\n                }\n            }\n            ImGui::InputText("코드", state.addCode, sizeof(state.addCode));\n            ImGui::InputText("표시명", state.addName, sizeof(state.addName));\n', 'symbol search controls')
t = replace_once(t, '            ImGui::Combo(\n                "삽입 방식",\n                &state.addPlacement,\n                placements,\n                2);\n', '            ImGui::Combo(\n                "삽입 방식",\n                &state.addPlacement,\n                placements,\n                2);\n            const char* valueModes[] = {\n                "원시 종가", "기준값 100",\n                "누적 수익률 %", "주 종목 대비 상대강도" };\n            ImGui::Combo("표시 모드", &state.addValueMode, valueModes, 4);\n', 'add value mode combo')
t = replace_once(t, '        const app::ComparisonModuleSnapshot& snapshot,\n        ComparisonManagerUiState& state,\n        ApplyComparisonDefinitions applyDefinitions,\n        RequestComparisonData requestData)\n', '        const app::ComparisonModuleSnapshot& snapshot,\n        const std::vector<SymbolCatalogEntry>& symbolCatalog,\n        ComparisonManagerUiState& state,\n        ApplyComparisonDefinitions applyDefinitions,\n        RequestComparisonData requestData,\n        RefreshSymbolCatalog refreshCatalog)\n', 'draw signature implementation')
t = replace_once(t, '        DrawAddPopup(\n            definitions,\n            state,\n            applyDefinitions,\n            requestData);\n', '        DrawAddPopup(\n            definitions,\n            symbolCatalog,\n            state,\n            applyDefinitions,\n            requestData,\n            refreshCatalog);\n', 'draw add popup call')
t = replace_once(t, '        if (ImGui::Combo("삽입 방식", &placement, placements, 2)) {\n            state.draft.placement = placement == 1\n                ? app::ComparisonPlacement::PriceSecondaryAxis\n                : app::ComparisonPlacement::SeparatePane;\n            state.dirty = true;\n        }\n', '        if (ImGui::Combo("삽입 방식", &placement, placements, 2)) {\n            state.draft.placement = placement == 1\n                ? app::ComparisonPlacement::PriceSecondaryAxis\n                : app::ComparisonPlacement::SeparatePane;\n            state.dirty = true;\n        }\n        const char* valueModes[] = {\n            "원시 종가", "기준값 100",\n            "누적 수익률 %", "주 종목 대비 상대강도" };\n        int valueMode = static_cast<int>(state.draft.valueMode);\n        if (ImGui::Combo("표시 모드", &valueMode, valueModes, 4)) {\n            state.draft.valueMode =\n                static_cast<app::ComparisonValueMode>(valueMode);\n            state.dirty = true;\n        }\n', 'edit value mode combo')
write(p, t)

# Runtime engine header
p = 'core/kiwoom_runtime_engine.h'
t = read(p)
t = replace_once(t, '#include "kiwoom_market_data.h"\n', '#include "kiwoom_market_data.h"\n#include "kiwoom_symbol_catalog.h"\n', 'engine catalog include')
t = replace_once(t, '        RequestIndexMinuteBars,\n', '        RequestIndexMinuteBars,\n        RequestSymbolCatalog,\n', 'engine action enum')
t = replace_once(t, '        std::vector<KiwoomRuntimeAction> RequestIndexMinuteBars(\n            const std::string& indexCode,\n            int minuteUnit,\n            const Continuation& continuation,\n            std::string& error);\n', '        std::vector<KiwoomRuntimeAction> RequestIndexMinuteBars(\n            const std::string& indexCode,\n            int minuteUnit,\n            const Continuation& continuation,\n            std::string& error);\n\n        std::vector<KiwoomRuntimeAction> RequestSymbolCatalog(\n            const std::string& marketType,\n            const Continuation& continuation,\n            std::string& error);\n', 'engine catalog method')
write(p, t)

# Runtime engine implementation
p = 'core/kiwoom_runtime_engine.cpp'
t = read(p)
needle = '''    std::vector<KiwoomRuntimeAction>\n    KiwoomRuntimeEngine::SubmitOrder(\n'''
method = '''    std::vector<KiwoomRuntimeAction>\n    KiwoomRuntimeEngine::RequestSymbolCatalog(\n        const std::string& marketType,\n        const Continuation& continuation,\n        std::string& error)\n    {\n        std::lock_guard<std::mutex> lock(mutex_);\n        if (accessToken_.empty()) {\n            error = "access token is not available";\n            return {};\n        }\n        RestRequest request = BuildSymbolCatalogRestRequest(\n            marketType, accessToken_, continuation, error);\n        if (!error.empty()) return {};\n        KiwoomRuntimeAction action = MakeRestActionLocked(\n            KiwoomRuntimeActionType::RequestSymbolCatalog,\n            std::move(request));\n        action.marketCode = marketType;\n        return { std::move(action) };\n    }\n\n'''
t = replace_once(t, needle, method + needle, 'engine catalog implementation')
write(p, t)

# Runner header
p = 'platform/kiwoom_runtime_runner.h'
t = read(p)
t = replace_once(t, '#include "../core/kiwoom_index_realtime.h"\n', '#include "../core/kiwoom_index_realtime.h"\n#include "../core/kiwoom_symbol_catalog.h"\n', 'runner catalog include')
t = replace_once(t, '        std::function<void(\n            const IndexValueTick& tick)> indexValue;\n', '        std::function<void(\n            const IndexValueTick& tick)> indexValue;\n        std::function<void(\n            const std::string& marketType,\n            const SymbolCatalogPage& page,\n            const Continuation& continuation)> symbolCatalog;\n', 'runner catalog callback')
t = replace_once(t, '        bool RequestIndexMinuteBars(\n            const std::string& indexCode,\n            int minuteUnit,\n            const Continuation& continuation,\n            std::string& error);\n', '        bool RequestIndexMinuteBars(\n            const std::string& indexCode,\n            int minuteUnit,\n            const Continuation& continuation,\n            std::string& error);\n\n        bool RequestSymbolCatalog(\n            const std::string& marketType,\n            const Continuation& continuation,\n            std::string& error);\n', 'runner catalog method')
write(p, t)

# Runner implementation
p = 'platform/kiwoom_runtime_runner.cpp'
t = read(p)
t = replace_once(t, '            case KiwoomRuntimeActionType::RequestIndexMinuteBars:\n                return "index-minute-bars";\n', '            case KiwoomRuntimeActionType::RequestIndexMinuteBars:\n                return "index-minute-bars";\n            case KiwoomRuntimeActionType::RequestSymbolCatalog:\n                return "symbol-catalog";\n', 'runner action name')
needle = '''    bool KiwoomRuntimeRunner::SubscribeStockTrades(\n'''
method = '''    bool KiwoomRuntimeRunner::RequestSymbolCatalog(\n        const std::string& marketType,\n        const Continuation& continuation,\n        std::string& error)\n    {\n        if (!running_.load(std::memory_order_acquire)) {\n            error = "Kiwoom runtime is not running";\n            return false;\n        }\n        std::vector<KiwoomRuntimeAction> actions =\n            engine_.RequestSymbolCatalog(marketType, continuation, error);\n        if (actions.empty()) return false;\n        Enqueue(std::move(actions));\n        return true;\n    }\n\n'''
t = replace_once(t, needle, method + needle, 'runner catalog implementation')
t = replace_once(t, '        case KiwoomRuntimeActionType::RequestIndexMinuteBars:\n        case KiwoomRuntimeActionType::SubmitOrderHttp: {\n', '        case KiwoomRuntimeActionType::RequestIndexMinuteBars:\n        case KiwoomRuntimeActionType::RequestSymbolCatalog:\n        case KiwoomRuntimeActionType::SubmitOrderHttp: {\n', 'runner rest action group')
t = replace_once(t, '            if (\n                action.type == KiwoomRuntimeActionType::RequestStockMinuteBars ||\n                action.type == KiwoomRuntimeActionType::RequestIndexMinuteBars)\n            {\n                DeliverMinuteBars(action, response, continuation);\n            }\n            else if (action.type == KiwoomRuntimeActionType::RequestOpenOrders) {\n', '            if (\n                action.type == KiwoomRuntimeActionType::RequestStockMinuteBars ||\n                action.type == KiwoomRuntimeActionType::RequestIndexMinuteBars)\n            {\n                DeliverMinuteBars(action, response, continuation);\n            }\n            else if (action.type == KiwoomRuntimeActionType::RequestSymbolCatalog) {\n                SymbolCatalogPage page;\n                if (!response.transportOk) {\n                    page.result.error = response.error;\n                }\n                else if (response.statusCode < 200 || response.statusCode >= 300) {\n                    page.result.error = "symbol catalog HTTP failure";\n                }\n                else {\n                    page = ParseSymbolCatalogResponse(action.marketCode, response.body);\n                }\n                if (callbacks_.symbolCatalog) {\n                    callbacks_.symbolCatalog(action.marketCode, page, continuation);\n                }\n                Log(page.result.ok ? "DATA" : "FAULT",\n                    page.result.ok\n                        ? "symbol catalog received: " + action.marketCode +\n                            " rows=" + std::to_string(page.entries.size())\n                        : (page.result.error.empty()\n                            ? "symbol catalog rejected"\n                            : page.result.error));\n            }\n            else if (action.type == KiwoomRuntimeActionType::RequestOpenOrders) {\n', 'runner catalog delivery')
write(p, t)

# shell integration
p = 'shell_main.cpp'
t = read(p)
t = replace_once(t, '#include "core/kiwoom_runtime_engine.h"\n', '#include "core/kiwoom_runtime_engine.h"\n#include "core/kiwoom_symbol_catalog.h"\n', 'shell catalog include')
t = replace_once(t, 'static trading::ui::ComparisonManagerUiState g_comparisonManagerUi;\n', 'static trading::ui::ComparisonManagerUiState g_comparisonManagerUi;\nstatic std::mutex g_symbolCatalogMutex;\nstatic std::vector<trading::SymbolCatalogEntry> g_symbolCatalog;\n', 'shell catalog globals')
# Add refresh function before MinuteUnitFromSelection
needle = 'static int MinuteUnitFromSelection(int selection) noexcept\n'
method = '''static bool RefreshSymbolCatalog(std::string& error)\n{\n    if (!g_runtimeRunner || !g_runtimeRunner->IsRunning()) {\n        error = "키움 런타임이 실행 중이 아닙니다.";\n        return false;\n    }\n    {\n        std::lock_guard<std::mutex> lock(g_symbolCatalogMutex);\n        g_symbolCatalog.clear();\n    }\n    trading::Continuation empty;\n    std::string firstError;\n    std::string secondError;\n    const bool kospi = g_runtimeRunner->RequestSymbolCatalog("0", empty, firstError);\n    const bool kosdaq = g_runtimeRunner->RequestSymbolCatalog("10", empty, secondError);\n    if (!kospi || !kosdaq) {\n        error = !firstError.empty() ? firstError : secondError;\n        return false;\n    }\n    error.clear();\n    return true;\n}\n\n'''
t = replace_once(t, needle, method + needle, 'shell refresh function')
# callback assignment adjacent to indexValue callback
marker = '    callbacks.indexValue = [](const trading::IndexValueTick& tick) {'
pos = t.find(marker)
if pos < 0:
    raise RuntimeError('shell index callback not found')
# find end of lambda assignment by first '\n    };' after pos
end = t.find('\n    };', pos)
if end < 0:
    raise RuntimeError('shell index callback end not found')
end += len('\n    };')
callback = '''\n    callbacks.symbolCatalog = [](\n        const std::string& marketType,\n        const trading::SymbolCatalogPage& page,\n        const trading::Continuation& continuation)\n    {\n        if (page.result.ok) {\n            std::lock_guard<std::mutex> lock(g_symbolCatalogMutex);\n            for (const trading::SymbolCatalogEntry& entry : page.entries) {\n                const auto found = std::find_if(\n                    g_symbolCatalog.begin(), g_symbolCatalog.end(),\n                    [&](const trading::SymbolCatalogEntry& existing) {\n                        return existing.code == entry.code;\n                    });\n                if (found == g_symbolCatalog.end()) g_symbolCatalog.push_back(entry);\n            }\n        }\n        if (continuation.hasMore && g_runtimeRunner) {\n            std::string nextError;\n            g_runtimeRunner->RequestSymbolCatalog(\n                marketType, continuation, nextError);\n            if (!nextError.empty()) g_log.Add("FAULT", "%s", nextError.c_str());\n        }\n        WakeFrames(6);\n    };'''
t = t[:end] + callback + t[end:]
# draw call signature: insert snapshot local and args
old = '''        trading::ui::DrawComparisonManagerWindow(\n            g_comparisonDefinitions,\n            g_comparisonModule.Snapshot(),\n            g_comparisonManagerUi,\n            ApplyComparisonConfiguration,\n            RequestComparisonData);'''
new = '''        std::vector<trading::SymbolCatalogEntry> symbolCatalog;\n        {\n            std::lock_guard<std::mutex> lock(g_symbolCatalogMutex);\n            symbolCatalog = g_symbolCatalog;\n        }\n        trading::ui::DrawComparisonManagerWindow(\n            g_comparisonDefinitions,\n            g_comparisonModule.Snapshot(),\n            symbolCatalog,\n            g_comparisonManagerUi,\n            ApplyComparisonConfiguration,\n            RequestComparisonData,\n            RefreshSymbolCatalog);'''
t = replace_once(t, old, new, 'shell comparison draw call')
write(p, t)

# build catalog source
p = 'build.bat'
t = read(p)
t = replace_once(t, '   core\\kiwoom_index_realtime.cpp ^\n', '   core\\kiwoom_index_realtime.cpp ^\n   core\\kiwoom_symbol_catalog.cpp ^\n', 'build catalog source')
write(p, t)

# tests run_all
p = 'tests/run_all.bat'
t = read(p)
t = replace_once(t, 'call :build_and_run comparison_module_tests.exe "tests\\comparison_module_tests.cpp core\\json_lite.cpp core\\kiwoom_protocol.cpp core\\kiwoom_market_data.cpp core\\kiwoom_index_realtime.cpp app\\comparison_module.cpp"\n', 'call :build_and_run comparison_transform_tests.exe "tests\\comparison_transform_tests.cpp app\\comparison_transform.cpp"\nif errorlevel 1 exit /b 1\n\ncall :build_and_run comparison_module_tests.exe "tests\\comparison_module_tests.cpp core\\json_lite.cpp core\\kiwoom_protocol.cpp core\\kiwoom_market_data.cpp core\\kiwoom_index_realtime.cpp app\\comparison_transform.cpp app\\comparison_module.cpp"\n', 'run comparison transform test')
t = replace_once(t, 'call :build_and_run comparison_render_adapter_tests.exe "tests\\comparison_render_adapter_tests.cpp app\\comparison_render_adapter.cpp render\\render_document.cpp"\n', 'call :build_and_run comparison_render_adapter_tests.exe "tests\\comparison_render_adapter_tests.cpp app\\comparison_transform.cpp app\\comparison_render_adapter.cpp render\\render_document.cpp"\n', 'adapter transform source')
t = t.replace('app\\comparison_render_adapter.cpp app\\chart_workspace_module.cpp', 'app\\comparison_transform.cpp app\\comparison_render_adapter.cpp app\\chart_workspace_module.cpp')
t = replace_once(t, 'call :build_and_run kiwoom_market_data_tests.exe "tests\\kiwoom_market_data_tests.cpp core\\json_lite.cpp core\\kiwoom_market_data.cpp"\n', 'call :build_and_run kiwoom_market_data_tests.exe "tests\\kiwoom_market_data_tests.cpp core\\json_lite.cpp core\\kiwoom_market_data.cpp"\nif errorlevel 1 exit /b 1\n\ncall :build_and_run kiwoom_symbol_catalog_tests.exe "tests\\kiwoom_symbol_catalog_tests.cpp core\\json_lite.cpp core\\kiwoom_symbol_catalog.cpp"\n', 'catalog tests entry')
t = t.replace('core\\kiwoom_market_data.cpp core\\runtime_config.cpp', 'core\\kiwoom_market_data.cpp core\\kiwoom_symbol_catalog.cpp core\\runtime_config.cpp')
write(p, t)

print('M8 search and normalized comparison wiring applied')
