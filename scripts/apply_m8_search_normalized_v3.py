from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
source = subprocess.check_output(
    ["git", "show", "HEAD:scripts/apply_m8_search_normalized.py"],
    cwd=root,
    text=True,
    encoding="utf-8-sig")

start = source.index("# callback assignment adjacent to indexValue callback")
end = source.index("# draw call signature", start)
robust = r'''# callback assignment adjacent to indexValue callback
match = re.search(
    r'        callbacks\.indexValue = \[\]\(\n'
    r'            const trading::IndexValueTick& tick\) \{.*?\n'
    r'        \};',
    t,
    flags=re.S)
if match is None:
    raise RuntimeError('shell index callback block not found')
callback = '''\n    callbacks.symbolCatalog = [](\n        const std::string& marketType,\n        const trading::SymbolCatalogPage& page,\n        const trading::Continuation& continuation)\n    {\n        if (page.result.ok) {\n            std::lock_guard<std::mutex> lock(g_symbolCatalogMutex);\n            for (const trading::SymbolCatalogEntry& entry : page.entries) {\n                const auto found = std::find_if(\n                    g_symbolCatalog.begin(), g_symbolCatalog.end(),\n                    [&](const trading::SymbolCatalogEntry& existing) {\n                        return existing.code == entry.code;\n                    });\n                if (found == g_symbolCatalog.end()) g_symbolCatalog.push_back(entry);\n            }\n        }\n        if (continuation.continueYn == "Y" &&\n            !continuation.nextKey.empty() && g_runtimeRunner)\n        {\n            std::string nextError;\n            g_runtimeRunner->RequestSymbolCatalog(\n                marketType, continuation, nextError);\n            if (!nextError.empty()) g_log.Add("FAULT", "%s", nextError.c_str());\n        }\n        WakeFrames(6);\n    };'''
t = t[:match.end()] + callback + t[match.end():]
'''
source = source[:start] + robust + source[end:]
source = source.replace(
    "if (continuation.hasMore && g_runtimeRunner) {",
    "if (continuation.continueYn == \"Y\" && !continuation.nextKey.empty() && g_runtimeRunner) {")

context = {
    "__name__": "__main__",
    "__file__": str(root / "scripts" / "apply_m8_search_normalized.py")
}
exec(compile(source, "apply_m8_search_normalized.py", "exec"), context)
