from __future__ import annotations

from pathlib import Path

script_path = Path(__file__).with_name("connect_realtime_stock_bars.py")
source = script_path.read_text(encoding="utf-8-sig")
marker = (
    "# ---------------------------------------------------------------------------\n"
    "# .github/workflows/windows-ci.yml\n"
)

if marker not in source:
    raise RuntimeError("real-time migration CI section marker is missing")

source_only = source.split(marker, 1)[0]
namespace = {
    "__name__": "__main__",
    "__file__": str(script_path),
}
exec(compile(source_only, str(script_path), "exec"), namespace)
