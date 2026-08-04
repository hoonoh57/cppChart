from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def write(path: str, content: str) -> None:
    (ROOT / path).write_text(content, encoding="utf-8-sig", newline="")


header_path = "core/kiwoom_symbol_catalog.h"
header = read(header_path)
old_include = '#include "kiwoom_protocol.h"\n'
new_include = '#include "kiwoom_protocol.h"\n#include "kiwoom_reconciliation.h"\n'
if header.count(old_include) != 1:
    raise RuntimeError("symbol catalog include anchor mismatch")
write(header_path, header.replace(old_include, new_include, 1))

shell_path = "shell_main.cpp"
shell = read(shell_path)
start_marker = '        callbacks.indexValue = [](const trading::IndexValueTick& tick) {'
end_marker = '\n\n        g_runtimeRunner ='
start = shell.find(start_marker)
end = shell.find(end_marker, start)
if start < 0 or end < 0:
    raise RuntimeError("callback block anchors not found")
replacement = '''        callbacks.indexValue = [](\n            const trading::IndexValueTick& tick) {\n            const trading::app::ComparisonApplyResult applied =\n                g_comparisonModule.ApplyIndexValueTick(tick);\n            if (applied.applied) {\n                WakeFrames(2);\n            }\n            else if (!applied.stale && !applied.error.empty()) {\n                g_log.Add(\"FAULT\", \"0J 지수 병합 실패: %s\", applied.error.c_str());\n            }\n        };\n        callbacks.symbolCatalog = [](\n            const std::string& marketType,\n            const trading::SymbolCatalogPage& page,\n            const trading::Continuation& continuation)\n        {\n            if (page.result.ok) {\n                std::lock_guard<std::mutex> lock(g_symbolCatalogMutex);\n                for (const trading::SymbolCatalogEntry& entry : page.entries) {\n                    const auto found = std::find_if(\n                        g_symbolCatalog.begin(),\n                        g_symbolCatalog.end(),\n                        [&](const trading::SymbolCatalogEntry& existing) {\n                            return existing.code == entry.code;\n                        });\n                    if (found == g_symbolCatalog.end()) {\n                        g_symbolCatalog.push_back(entry);\n                    }\n                }\n            }\n            if (continuation.continueYn == \"Y\" &&\n                !continuation.nextKey.empty() &&\n                g_runtimeRunner)\n            {\n                std::string nextError;\n                g_runtimeRunner->RequestSymbolCatalog(\n                    marketType,\n                    continuation,\n                    nextError);\n                if (!nextError.empty()) {\n                    g_log.Add(\"FAULT\", \"%s\", nextError.c_str());\n                }\n            }\n            WakeFrames(6);\n        };'''
write(shell_path, shell[:start] + replacement + shell[end:])
print("M8 build fixes applied")
