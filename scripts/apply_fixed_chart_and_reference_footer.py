from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_text(relative: str):
    path = ROOT / relative
    raw = path.read_bytes()
    bom = raw.startswith(b"\xef\xbb\xbf")
    return path, raw.decode("utf-8-sig"), bom


def write_text(path: Path, text: str, bom: bool):
    payload = text.encode("utf-8")
    if bom:
        payload = b"\xef\xbb\xbf" + payload
    path.write_bytes(payload)


def replace_once(relative: str, old: str, new: str):
    path, text, bom = read_text(relative)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"{relative}: expected exactly one occurrence, found {count}: {old[:120]!r}"
        )
    write_text(path, text.replace(old, new, 1), bom)


# 1. Reference quick actions belong to the fixed footer, not the scrolling editor.
old_reference_footer = r'''            if (ImGui::Button("기준선 추가")) {
                AddReference(
                    state.draft,
                    "기준선",
                    0.0,
                    { 190, 194, 208, 210 });
                state.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("과매수 추가")) {
                AddReference(
                    state.draft,
                    "과매수",
                    70.0,
                    { 235, 92, 92, 220 });
                state.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("과매도 추가")) {
                AddReference(
                    state.draft,
                    "과매도",
                    30.0,
                    { 72, 154, 235, 220 });
                state.dirty = true;
            }
        }

        void DrawAddIndicatorPopup(
'''
new_reference_footer = r'''        }

        void DrawReferenceQuickActions(
            IndicatorManagerUiState& state)
        {
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float available = ImGui::GetContentRegionAvail().x;
            const float width = (std::max)(
                1.0f,
                (available - spacing * 2.0f) / 3.0f);

            if (ImGui::Button(
                    "기준선 추가",
                    ImVec2(width, 0.0f)))
            {
                AddReference(
                    state.draft,
                    "기준선",
                    0.0,
                    { 190, 194, 208, 210 });
                state.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(
                    "과매수 추가",
                    ImVec2(width, 0.0f)))
            {
                AddReference(
                    state.draft,
                    "과매수",
                    70.0,
                    { 235, 92, 92, 220 });
                state.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(
                    "과매도 추가",
                    ImVec2(width, 0.0f)))
            {
                AddReference(
                    state.draft,
                    "과매도",
                    30.0,
                    { 72, 154, 235, 220 });
                state.dirty = true;
            }
        }

        void DrawAddIndicatorPopup(
'''
replace_once(
    "ui/indicator_manager_ui.cpp",
    old_reference_footer,
    new_reference_footer,
)

replace_once(
    "ui/indicator_manager_ui.cpp",
    r'''        const float editorFooterHeight =
            ImGui::GetFrameHeightWithSpacing() * 2.0f +
            ImGui::GetStyle().ItemSpacing.y;
''',
    r'''        const float editorFooterHeight =
            ImGui::GetFrameHeightWithSpacing() * 3.0f +
            ImGui::GetStyle().ItemSpacing.y * 2.0f;
''',
)

replace_once(
    "ui/indicator_manager_ui.cpp",
    r'''        ImGui::EndChild();
        ImGui::Separator();
        const bool applyDisabled = !state.dirty;
''',
    r'''        ImGui::EndChild();
        ImGui::Separator();
        DrawReferenceQuickActions(state);
        ImGui::Separator();
        const bool applyDisabled = !state.dirty;
''',
)


# 2. The chart viewport must never become an internally scrollable document.
replace_once(
    "shell_main.cpp",
    r'''    ImGui::Begin("실제 시세");
''',
    r'''    ImGui::Begin(
        "실제 시세",
        nullptr,
        ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);
''',
)


# 3. Pane and splitter items consume the exact supplied height. ImGui's default
#    vertical ItemSpacing previously accumulated between every pane and pushed
#    the final time axis below the visible chart region.
replace_once(
    "ui/render_document_renderer.cpp",
    r'''            ImGui::PushID(("splitter." + upperPane.id + "." + lowerPane.id).c_str());
            ImGui::InvisibleButton(
                "##pane_splitter",
                ImVec2(width, PaneSplitterHeight),
                ImGuiButtonFlags_MouseButtonLeft);
            const bool hovered = ImGui::IsItemHovered();
''',
    r'''            ImGui::PushID(("splitter." + upperPane.id + "." + lowerPane.id).c_str());
            const ImVec2 itemSpacing = ImGui::GetStyle().ItemSpacing;
            ImGui::PushStyleVar(
                ImGuiStyleVar_ItemSpacing,
                ImVec2(itemSpacing.x, 0.0f));
            ImGui::InvisibleButton(
                "##pane_splitter",
                ImVec2(width, PaneSplitterHeight),
                ImGuiButtonFlags_MouseButtonLeft);
            ImGui::PopStyleVar();
            const bool hovered = ImGui::IsItemHovered();
''',
)

replace_once(
    "ui/render_document_renderer.cpp",
    r'''            ImGui::PushID(pane.id.c_str());
            ImGui::InvisibleButton(
                "##surface",
                size,
                ImGuiButtonFlags_MouseButtonLeft |
                    ImGuiButtonFlags_MouseButtonRight);
            const ImVec2 surfaceOrigin = ImGui::GetItemRectMin();
''',
    r'''            ImGui::PushID(pane.id.c_str());
            const ImVec2 itemSpacing = ImGui::GetStyle().ItemSpacing;
            ImGui::PushStyleVar(
                ImGuiStyleVar_ItemSpacing,
                ImVec2(itemSpacing.x, 0.0f));
            ImGui::InvisibleButton(
                "##surface",
                size,
                ImGuiButtonFlags_MouseButtonLeft |
                    ImGuiButtonFlags_MouseButtonRight);
            ImGui::PopStyleVar();
            const ImVec2 surfaceOrigin = ImGui::GetItemRectMin();
''',
)

print("Applied fixed chart viewport and indicator reference footer migration")
