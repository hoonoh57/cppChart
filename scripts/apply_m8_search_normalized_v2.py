from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
source = subprocess.check_output(
    ["git", "show", "HEAD:scripts/apply_m8_search_normalized.py"],
    cwd=root,
    text=True,
    encoding="utf-8-sig")

source = source.replace(
    "marker = 'callbacks.indexValue = [](const trading::IndexValueTick& tick) {'",
    "marker = 'callbacks.indexValue = [](\\n            const trading::IndexValueTick& tick) {'")
source = source.replace(
    "end = t.find('\\n    };', pos)",
    "end = t.find('\\n        };', pos)")
source = source.replace(
    "if (continuation.hasMore && g_runtimeRunner) {",
    "if (continuation.continueYn == \"Y\" && !continuation.nextKey.empty() && g_runtimeRunner) {")

exec(compile(source, "apply_m8_search_normalized.py", "exec"), {"__name__": "__main__"})
