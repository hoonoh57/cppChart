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


path = "ui/indicator_manager_ui.cpp"
text = read(path)
text = replace_once(
    text,
    "        const bool noSelection = selected == nullptr;\n"
    "        if (noSelection) ImGui::BeginDisabled();\n"
    "        if (ImGui::Button(\"복제\")) {\n"
    "            const std::string newId = app::NextIndicatorInstanceId(\n"
    "                selected->spec.type,\n"
    "                definitions);\n",
    "        const bool noSelection = selected == nullptr;\n"
    "        const std::string selectedType = selected != nullptr\n"
    "            ? selected->spec.type\n"
    "            : std::string{};\n"
    "        const bool selectedVisible =\n"
    "            selected != nullptr && selected->visible;\n"
    "        const app::IndicatorInstanceDefinition selectedCopy =\n"
    "            selected != nullptr\n"
    "                ? *selected\n"
    "                : app::IndicatorInstanceDefinition{};\n"
    "        if (noSelection) ImGui::BeginDisabled();\n"
    "        if (ImGui::Button(\"복제\")) {\n"
    "            const std::string newId = app::NextIndicatorInstanceId(\n"
    "                selectedType,\n"
    "                definitions);\n",
    "stable selected instance snapshot")
text = text.replace(
    "                if (definition.spec.type == selected->spec.type) {",
    "                if (definition.spec.type == selectedType) {")
text = replace_once(
    text,
    "            if (app::DuplicateIndicatorDefinition(\n"
    "                    *selected,\n",
    "            if (app::DuplicateIndicatorDefinition(\n"
    "                    selectedCopy,\n",
    "duplicate stable instance")
text = replace_once(
    text,
    "                selected != nullptr && selected->visible\n"
    "                    ? \"감추기\"\n",
    "                selectedVisible\n"
    "                    ? \"감추기\"\n",
    "stable visibility label")
write(path, text)

path = "ui/render_document_renderer.h"
text = read(path)
text = replace_once(
    text,
    "        std::map<std::string, float> paneHeightWeights;\n",
    "        std::map<std::string, float> paneHeightWeights;\n"
    "        std::map<std::string, float> paneDefaultHeightWeights;\n",
    "pane default height state")
write(path, text)

path = "ui/render_document_renderer.cpp"
text = read(path)
old = (
    "        for (const render::Pane& pane : document.panes) {\n"
    "            auto found = surfaceState.paneHeightWeights.find(pane.id);\n"
    "            if (found == surfaceState.paneHeightWeights.end() ||\n"
    "                !std::isfinite(found->second) || found->second <= 0.0f)\n"
    "            {\n"
    "                surfaceState.paneHeightWeights[pane.id] =\n"
    "                    (std::max)(0.01f, pane.heightWeight);\n"
    "            }\n"
    "        }\n")
new = (
    "        for (const render::Pane& pane : document.panes) {\n"
    "            const float documentWeight =\n"
    "                (std::max)(0.01f, pane.heightWeight);\n"
    "            const auto current =\n"
    "                surfaceState.paneHeightWeights.find(pane.id);\n"
    "            const auto previousDefault =\n"
    "                surfaceState.paneDefaultHeightWeights.find(pane.id);\n"
    "            const bool missing =\n"
    "                current == surfaceState.paneHeightWeights.end() ||\n"
    "                previousDefault ==\n"
    "                    surfaceState.paneDefaultHeightWeights.end();\n"
    "            const bool invalid =\n"
    "                !missing &&\n"
    "                (!std::isfinite(current->second) ||\n"
    "                 current->second <= 0.0f);\n"
    "            const bool configuredWeightChanged =\n"
    "                !missing &&\n"
    "                std::fabs(\n"
    "                    previousDefault->second -\n"
    "                    documentWeight) > 0.0001f;\n"
    "            if (missing || invalid || configuredWeightChanged) {\n"
    "                surfaceState.paneHeightWeights[pane.id] =\n"
    "                    documentWeight;\n"
    "            }\n"
    "            surfaceState.paneDefaultHeightWeights[pane.id] =\n"
    "                documentWeight;\n"
    "        }\n")
text = replace_once(text, old, new, "configured pane weight synchronization")
write(path, text)

path = "scripts/verify_modular_architecture.ps1"
text = read(path)
text = replace_once(
    text,
    "    'paneHeightWeights',\n"
    "    'DrawStyledLine(')) {\n",
    "    'paneHeightWeights',\n"
    "    'paneDefaultHeightWeights',\n"
    "    'configuredWeightChanged',\n"
    "    'DrawStyledLine(')) {\n",
    "pane configured-weight gate")
text = replace_once(
    text,
    "    'EditPaneSelection(')) {\n",
    "    'EditPaneSelection(',\n"
    "    'const app::IndicatorInstanceDefinition selectedCopy',\n"
    "    'const bool selectedVisible')) {\n",
    "stable manager selection gate")
write(path, text)

print("indicator manager runtime hardening applied")
