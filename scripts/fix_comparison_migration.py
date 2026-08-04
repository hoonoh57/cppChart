from pathlib import Path

path = Path(__file__).with_name("apply_indicator_pack_and_dual_axis.py")
text = path.read_text(encoding="utf-8-sig")
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
if old not in text:
    raise RuntimeError("migration block to narrow was not found")
path.write_text(text.replace(old, new, 1), encoding="utf-8-sig", newline="")
print("dual-axis migration match narrowed")
