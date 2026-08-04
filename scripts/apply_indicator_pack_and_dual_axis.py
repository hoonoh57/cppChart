from pathlib import Path
import runpy
import subprocess

root = Path(__file__).resolve().parents[1]
scripts = root / "scripts"
source = subprocess.check_output(
    [
        "git", "show",
        "4ec58b01349e33f88fa27319cbacdc452dbd0a2d:scripts/apply_indicator_pack_and_dual_axis.py"
    ],
    cwd=root,
    text=True,
    encoding="utf-8-sig")
old = '''text = replace_once(
    text,
    "            for (const render::LineSeries& series : pane.lines) {\\n"
    "                if (!series.visible) continue;\\n",
    "            for (const render::LineSeries& series : pane.lines) {\\n"
    "                if (!series.visible || !series.axisId.empty()) continue;\\n",
    "exclude secondary lines from primary range")'''
new = '''old_primary_line_loop = (
    "            for (const render::LineSeries& series : pane.lines) {\\n"
    "                if (!series.visible) continue;\\n")
if text.count(old_primary_line_loop) < 1:
    raise RuntimeError("primary line range loop was not found")
text = text.replace(
    old_primary_line_loop,
    "            for (const render::LineSeries& series : pane.lines) {\\n"
    "                if (!series.visible || !series.axisId.empty()) continue;\\n",
    1)'''
if old not in source:
    raise RuntimeError("dual-axis migration block was not found")
implementation = scripts / "_indicator_axis_impl.py"
implementation.write_text(
    source.replace(old, new, 1),
    encoding="utf-8-sig",
    newline="")
try:
    runpy.run_path(str(implementation), run_name="__main__")
finally:
    implementation.unlink(missing_ok=True)
    (scripts / "fix_comparison_migration.py").unlink(missing_ok=True)
    (scripts / "sitecustomize.py").unlink(missing_ok=True)
