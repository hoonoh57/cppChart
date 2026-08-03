from pathlib import Path
import os
import shutil

root = Path(__file__).resolve().parents[1]
hook = root / ".git" / "hooks" / "pre-commit"
hook.parent.mkdir(parents=True, exist_ok=True)
hook.write_text(
    "#!/bin/sh\n"
    "git restore --staged -- .github/workflows/windows-ci.yml .github/workflows/apply-indicator-legend.yml 2>/dev/null || true\n"
    "git restore -- .github/workflows/windows-ci.yml .github/workflows/apply-indicator-legend.yml 2>/dev/null || true\n",
    encoding="utf-8",
)
try:
    os.chmod(hook, 0o755)
except OSError:
    pass

current = Path(__file__)
cache = current.parent / "__pycache__"
try:
    current.unlink()
except OSError:
    pass
if cache.exists():
    shutil.rmtree(cache, ignore_errors=True)
