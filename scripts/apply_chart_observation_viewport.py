from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
path = ROOT / "ui/render_document_renderer.cpp"
text = path.read_text(encoding="utf-8-sig")


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    text = text.replace(old, new, 1)


replace_once(
    "            const render::Pane& pane,\n"
    "            ValueRange& values,\n",
    "            const render::Pane& pane,\n"
    "            const ValueRange& automaticValues,\n"
    "            ValueRange& values,\n",
    "interaction automatic range parameter")
replace_once(
    "                        valueViewport,\n"
    "                        values.minimum,\n"
    "                        values.maximum,\n",
    "                        valueViewport,\n"
    "                        automaticValues.minimum,\n"
    "                        automaticValues.maximum,\n",
    "axis reset uses automatic range")
replace_once(
    "                    valueViewport = {};\n"
    "                    state.dirty = true;\n",
    "                    const double topPadding =\n"
    "                        pane.valueScale == render::PaneValueScale::Fixed\n"
    "                            ? 0.0\n"
    "                            : AutomaticTopPaddingFraction;\n"
    "                    const double bottomPadding =\n"
    "                        pane.valueScale == render::PaneValueScale::Fixed\n"
    "                            ? 0.0\n"
    "                            : AutomaticBottomPaddingFraction;\n"
    "                    render::ResetValueViewport(\n"
    "                        valueViewport,\n"
    "                        automaticValues.minimum,\n"
    "                        automaticValues.maximum,\n"
    "                        topPadding,\n"
    "                        bottomPadding);\n"
    "                    values = ValueRangeFromViewport(valueViewport);\n"
    "                    state.dirty = true;\n",
    "plot reset restores automatic value range")
replace_once(
    "                pane,\n"
    "                values,\n"
    "                valueViewport,\n",
    "                pane,\n"
    "                automaticValues,\n"
    "                values,\n"
    "                valueViewport,\n",
    "pass automatic value range")

path.write_text(text, encoding="utf-8-sig", newline="")
print("automatic value reset correction applied")
