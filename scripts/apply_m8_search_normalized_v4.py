from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
shell_path = root / "shell_main.cpp"
shell = shell_path.read_text(encoding="utf-8-sig")
old = "callbacks.indexValue = [](\n            const trading::IndexValueTick& tick) {"
new = "callbacks.indexValue = [](const trading::IndexValueTick& tick) {"
if shell.count(old) != 1:
    raise RuntimeError(f"index callback declaration count={shell.count(old)}")
shell_path.write_text(shell.replace(old, new, 1), encoding="utf-8-sig", newline="")

source = subprocess.check_output(
    ["git", "show", "HEAD:scripts/apply_m8_search_normalized.py"],
    cwd=root,
    text=True,
    encoding="utf-8-sig")
source = source.replace(
    "end = t.find('\\n    };', pos)",
    "end = t.find('\\n        };', pos)")
source = source.replace(
    "if (continuation.hasMore && g_runtimeRunner) {",
    "if (continuation.continueYn == \"Y\" && !continuation.nextKey.empty() && g_runtimeRunner) {")
context = {
    "__name__": "__main__",
    "__file__": str(root / "scripts" / "apply_m8_search_normalized.py")
}
exec(compile(source, "apply_m8_search_normalized.py", "exec"), context)
