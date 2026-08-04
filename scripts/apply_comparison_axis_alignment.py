from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def write(path: str, content: str) -> None:
    (ROOT / path).write_text(content, encoding="utf-8-sig", newline="")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


# Keep one shared horizontal plot geometry across every pane even when only one
# pane owns left-side secondary axes.
path = "ui/render_document_renderer.cpp"
text = read(path)
text = replace_once(
    text,
    "            bool drawTimeAxis,\n"
    "            ImVec2 size,\n"
    "            RenderSurfaceState& state,\n",
    "            bool drawTimeAxis,\n"
    "            std::size_t sharedLeftAxisColumns,\n"
    "            ImVec2 size,\n"
    "            RenderSurfaceState& state,\n",
    "DrawPane shared axis parameter")
text = replace_once(
    text,
    "            const float leftAxisWidth =\n"
    "                static_cast<float>(leftAxes.size()) * ValueAxisWidth;\n",
    "            const float leftAxisWidth =\n"
    "                static_cast<float>(sharedLeftAxisColumns) *\n"
    "                ValueAxisWidth;\n",
    "shared left axis width")
text = replace_once(
    text,
    "        float totalWeight = 0.0f;\n"
    "        for (const render::Pane& pane : document.panes) {\n"
    "            totalWeight += surfaceState.paneHeightWeights[pane.id];\n"
    "        }\n",
    "        std::size_t sharedLeftAxisColumns = 0U;\n"
    "        for (const render::Pane& pane : document.panes) {\n"
    "            std::size_t paneColumns = 0U;\n"
    "            for (const render::ValueAxis& axis : pane.valueAxes) {\n"
    "                if (axis.visible &&\n"
    "                    axis.side == render::ValueAxisSide::Left)\n"
    "                {\n"
    "                    ++paneColumns;\n"
    "                }\n"
    "            }\n"
    "            sharedLeftAxisColumns = (std::max)(\n"
    "                sharedLeftAxisColumns,\n"
    "                paneColumns);\n"
    "        }\n\n"
    "        float totalWeight = 0.0f;\n"
    "        for (const render::Pane& pane : document.panes) {\n"
    "            totalWeight += surfaceState.paneHeightWeights[pane.id];\n"
    "        }\n",
    "compute shared left axis columns")
text = replace_once(
    text,
    "                surfaceState.defaultVisibleSpan,\n"
    "                index + 1 == document.panes.size(),\n"
    "                ImVec2(size.x, paneHeight),\n",
    "                surfaceState.defaultVisibleSpan,\n"
    "                index + 1 == document.panes.size(),\n"
    "                sharedLeftAxisColumns,\n"
    "                ImVec2(size.x, paneHeight),\n",
    "pass shared left axis columns")
write(path, text)

# Accept either already-scaled integer index values or decimal index values from
# ka20005, while stock prices remain strict integer won values.
path = "core/kiwoom_market_data.cpp"
text = read(path)
helper_anchor = '''        bool TryReadAbsoluteVolume(
            const json_lite::Value* value,
            Volume& out)
'''
helper = '''        bool TryReadAbsoluteIndexValue(
            const json_lite::Value* value,
            PriceWon& out)
        {
            std::string text;
            if (!ScalarToString(value, text)) return false;
            text = Trim(text);
            if (text.empty()) return false;

            std::string normalized;
            normalized.reserve(text.size());
            for (char ch : text) {
                if (ch != ',') normalized.push_back(ch);
            }
            if (normalized.find('.') == std::string::npos) {
                return TryReadAbsolutePrice(value, out);
            }

            try {
                std::size_t consumed = 0;
                const double parsed =
                    std::stod(normalized, &consumed);
                if (consumed != normalized.size() ||
                    !std::isfinite(parsed))
                {
                    return false;
                }
                const double scaled = std::fabs(parsed) * 100.0;
                if (scaled <= 0.0 ||
                    scaled > static_cast<double>(
                        (std::numeric_limits<PriceWon>::max)()))
                {
                    return false;
                }
                out = static_cast<PriceWon>(std::llround(scaled));
                return true;
            }
            catch (...) {
                return false;
            }
        }

'''
text = replace_once(
    text,
    helper_anchor,
    helper + helper_anchor,
    "index value parser")
text = replace_once(
    text,
    "            response.bars.reserve(rows->AsArray().size());\n",
    "            const auto readPrice = [instrument](\n"
    "                const json_lite::Value* value,\n"
    "                PriceWon& output) {\n"
    "                return instrument == MinuteBarInstrument::Index\n"
    "                    ? TryReadAbsoluteIndexValue(value, output)\n"
    "                    : TryReadAbsolutePrice(value, output);\n"
    "            };\n\n"
    "            response.bars.reserve(rows->AsArray().size());\n",
    "instrument-specific price parser")
for field in ("open_pric", "high_pric", "low_pric", "cur_prc"):
    text = replace_once(
        text,
        f'TryReadAbsolutePrice(row.Find("{field}"), bar.',
        f'readPrice(row.Find("{field}"), bar.',
        f"index-aware {field} parser")
write(path, text)

path = "tests/kiwoom_market_data_tests.cpp"
text = read(path)
text = replace_once(
    text,
    '"{\\\"cur_prc\\\":\\\"320000\\\",\\\"trde_qty\\\":\\\"10\\\","\n'
    '                "\\\"cntr_tm\\\":\\\"20260803090100\\\",\\\"open_pric\\\":\\\"319900\\\","\n'
    '                "\\\"high_pric\\\":\\\"320100\\\",\\\"low_pric\\\":\\\"319800\\\"}]}"\n',
    '"{\\\"cur_prc\\\":\\\"3,200.00\\\",\\\"trde_qty\\\":\\\"10\\\","\n'
    '                "\\\"cntr_tm\\\":\\\"20260803090100\\\",\\\"open_pric\\\":\\\"3,199.00\\\","\n'
    '                "\\\"high_pric\\\":\\\"3,201.00\\\",\\\"low_pric\\\":\\\"3,198.00\\\"}]}"\n',
    "decimal index fixture")
text = replace_once(
    text,
    "        Check(indexPage.result.ok, \"valid index minute response must parse\");\n",
    "        Check(indexPage.result.ok, \"valid index minute response must parse\");\n"
    "        Check(indexPage.bars.size() == 1U,\n"
    "              \"index minute bar count mismatch\");\n"
    "        Check(indexPage.bars.front().close == 320000,\n"
    "              \"decimal index close must normalize to x100 integer\");\n",
    "decimal index assertion")
write(path, text)

print("shared dual-axis geometry and index normalization applied")
