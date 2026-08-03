from __future__ import annotations

from pathlib import Path

path = Path(__file__).with_name("integrate_real_minute_bars.py")
text = path.read_text(encoding="utf-8-sig")

replacements = {
    '    chart_functions + "static void DrawDashboard()",\n':
        '    chart_functions,\n',
    '    dashboard + "static void DrawLogWindow(",\n':
        '    dashboard,\n',
    '    load_case + "        case Cmd::LiquidatePosition: {",\n':
        '    load_case,\n',
}

for old, new in replacements.items():
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"expected one integration boundary match, found {count}: {old!r}")
    text = text.replace(old, new, 1)

path.write_text("\ufeff" + text, encoding="utf-8")
print("Repaired migration end-marker duplication")
