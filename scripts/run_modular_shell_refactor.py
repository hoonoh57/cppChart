from __future__ import annotations

from pathlib import Path

# Execute the source migration with a corrected range-replacement helper.
script_path = Path(__file__).with_name("apply_modular_shell_refactor.py")
source = script_path.read_text(encoding="utf-8-sig")
old = "    return text[:first] + replacement + text[last:]\n"
new = (
    "    if replacement.endswith(end):\n"
    "        replacement = replacement[:-len(end)]\n"
    "    return text[:first] + replacement + text[last:]\n"
)
if source.count(old) != 1:
    raise RuntimeError("modular refactor range helper was not found")
source = source.replace(old, new, 1)
namespace = {
    "__name__": "__main__",
    "__file__": str(script_path),
}
exec(compile(source, str(script_path), "exec"), namespace)
