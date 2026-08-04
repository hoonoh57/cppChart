#include "indicator_manager_ui.h"

#include "../app/indicator_properties.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace trading::ui
{
    namespace
    {
        struct PaneChoice final
        {
            std::string id;
            std::string title;
        };

        const app::IndicatorInstanceDefinition* SelectedDefinition(
            const std::vector<app::IndicatorInstanceDefinition>& definitions,
            const IndicatorManagerUiState& state) noexcept
        {
            return app::FindIndicatorDefinition(
                definitions,
                state.selectedIndicatorId);
        }

        void ResetDraft(
            IndicatorManagerUiState& state,
            const app::IndicatorInstanceDefinition& definition)
        {
            state.selectedIndicatorId = definition.spec.id;
            state.draft = definition;
            state.draftValid = true;
            state.dirty = false;
            state.error.clear();
        }

        void EnsureSelection(
            const std::vector<app::IndicatorInstanceDefinition>& definitions,
            IndicatorManagerUiState& state)
        {
            const app::IndicatorInstanceDefinition* selected =
                SelectedDefinition(definitions, state);
            if (selected == nullptr && !definitions.empty()) {
                state.selectedIndicatorId = definitions.front().spec.id;
                selected = &definitions.front();
            }
            if (selected != nullptr &&
                (!state.draftValid ||
                 state.draft.spec.id != selected->spec.id))
            {
                ResetDraft(state, *selected);
            }
        }

        std::string DefinitionLabel(
            const app::IndicatorInstanceDefinition& definition)
        {
            std::string label =
                app::IndicatorLegendLabel(definition.spec);
            if (!definition.visible) label += " [숨김]";
            return label;
        }

        bool CommitDefinitions(
            std::vector<app::IndicatorInstanceDefinition>& definitions,
            const std::vector<app::IndicatorInstanceDefinition>& candidate,
            IndicatorManagerUiState& state,
            ApplyIndicatorDefinitions applyDefinitions)
        {
            if (applyDefinitions == nullptr) {
                state.error = "지표 구성 적용 함수가 없습니다.";
                return false;
            }

            std::string error;
            if (!applyDefinitions(candidate, error)) {
                state.error = error.empty()
                    ? "지표 구성을 적용하지 못했습니다."
                    : error;
                return false;
            }

            definitions = candidate;
            state.error.clear();
            return true;
        }

        std::vector<PaneChoice> CollectPaneChoices(
            const std::vector<app::IndicatorInstanceDefinition>& definitions,
            const app::IndicatorInstanceDefinition* draft = nullptr)
        {
            std::vector<PaneChoice> choices;
            std::set<std::string> ids;
            auto add = [&](const std::string& id, const std::string& title) {
                if (id.empty() || !ids.insert(id).second) return;
                PaneChoice choice;
                choice.id = id;
                choice.title = title.empty() ? id : title;
                choices.push_back(std::move(choice));
            };

            add("price", "가격 패널");
            for (const app::IndicatorInstanceDefinition& definition :
                 definitions)
            {
                for (const app::IndicatorOutputBinding& output :
                     definition.outputs)
                {
                    add(output.paneId, output.paneTitle);
                }
                for (const app::IndicatorReferenceBinding& reference :
                     definition.references)
                {
                    add(reference.paneId, reference.paneTitle);
                }
            }
            if (draft != nullptr) {
                for (const app::IndicatorOutputBinding& output :
                     draft->outputs)
                {
                    add(output.paneId, output.paneTitle);
                }
                for (const app::IndicatorReferenceBinding& reference :
                     draft->references)
                {
                    add(reference.paneId, reference.paneTitle);
                }
            }
            return choices;
        }

        const PaneChoice* FindPaneChoice(
            const std::vector<PaneChoice>& choices,
            const std::string& id) noexcept
        {
            for (const PaneChoice& choice : choices) {
                if (choice.id == id) return &choice;
            }
            return nullptr;
        }

        bool EditPaneSelection(
            const char* id,
            std::string& paneId,
            std::string& paneTitle,
            const std::vector<PaneChoice>& choices,
            const std::string& newPaneId,
            const std::string& newPaneTitle)
        {
            const PaneChoice* current = FindPaneChoice(choices, paneId);
            const std::string preview = current != nullptr
                ? current->title
                : (paneTitle.empty() ? paneId : paneTitle);
            bool changed = false;

            ImGui::SetNextItemWidth(145.0f);
            if (ImGui::BeginCombo(id, preview.c_str())) {
                for (const PaneChoice& choice : choices) {
                    const bool selected = choice.id == paneId;
                    if (ImGui::Selectable(
                            choice.title.c_str(),
                            selected))
                    {
                        paneId = choice.id;
                        paneTitle = choice.title;
                        changed = true;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::Separator();
                if (ImGui::Selectable("새 하단 패널")) {
                    paneId = newPaneId;
                    paneTitle = newPaneTitle;
                    changed = true;
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        std::array<float, 4> ToFloatColor(
            const render::ColorRgba& color) noexcept
        {
            return {
                static_cast<float>(color.red) / 255.0f,
                static_cast<float>(color.green) / 255.0f,
                static_cast<float>(color.blue) / 255.0f,
                static_cast<float>(color.alpha) / 255.0f
            };
        }

        render::ColorRgba FromFloatColor(
            const std::array<float, 4>& color) noexcept
        {
            auto channel = [](float value) {
                value = (std::max)(0.0f, (std::min)(1.0f, value));
                return static_cast<std::uint8_t>(
                    std::lround(value * 255.0f));
            };
            return {
                channel(color[0]),
                channel(color[1]),
                channel(color[2]),
                channel(color[3])
            };
        }

        bool EditColor(
            const char* id,
            render::ColorRgba& color)
        {
            std::array<float, 4> value = ToFloatColor(color);
            if (!ImGui::ColorEdit4(
                    id,
                    value.data(),
                    ImGuiColorEditFlags_NoInputs |
                        ImGuiColorEditFlags_AlphaBar))
            {
                return false;
            }
            color = FromFloatColor(value);
            return true;
        }

        const char* LineStyleName(render::LineStyle style) noexcept
        {
            switch (style) {
            case render::LineStyle::Dashed:
                return "파선";
            case render::LineStyle::Dotted:
                return "점선";
            default:
                return "실선";
            }
        }

        bool EditLineStyle(
            const char* id,
            render::LineStyle& style)
        {
            const char* items[] = { "실선", "파선", "점선" };
            int selected = static_cast<int>(style);
            ImGui::SetNextItemWidth(75.0f);
            if (!ImGui::Combo(id, &selected, items, 3)) return false;
            style = static_cast<render::LineStyle>(selected);
            return true;
        }

        void SetPaneContract(
            app::IndicatorInstanceDefinition& definition,
            const std::string& paneId,
            float heightWeight,
            render::PaneValueScale valueScale,
            double fixedMinimum,
            double fixedMaximum,
            int valueDecimals)
        {
            for (app::IndicatorOutputBinding& output : definition.outputs) {
                if (output.paneId != paneId) continue;
                output.paneHeightWeight = heightWeight;
                output.paneValueScale = valueScale;
                output.fixedMinimum = fixedMinimum;
                output.fixedMaximum = fixedMaximum;
                output.valueDecimals = valueDecimals;
            }
            for (app::IndicatorReferenceBinding& reference :
                 definition.references)
            {
                if (reference.paneId != paneId) continue;
                reference.paneHeightWeight = heightWeight;
                reference.paneValueScale = valueScale;
                reference.fixedMinimum = fixedMinimum;
                reference.fixedMaximum = fixedMaximum;
                reference.valueDecimals = valueDecimals;
            }
        }

        bool ReadPaneContract(
            const app::IndicatorInstanceDefinition& definition,
            const std::string& paneId,
            float& heightWeight,
            render::PaneValueScale& valueScale,
            double& fixedMinimum,
            double& fixedMaximum,
            int& valueDecimals)
        {
            for (const app::IndicatorOutputBinding& output :
                 definition.outputs)
            {
                if (output.paneId != paneId) continue;
                heightWeight = output.paneHeightWeight;
                valueScale = output.paneValueScale;
                fixedMinimum = output.fixedMinimum;
                fixedMaximum = output.fixedMaximum;
                valueDecimals = output.valueDecimals;
                return true;
            }
            for (const app::IndicatorReferenceBinding& reference :
                 definition.references)
            {
                if (reference.paneId != paneId) continue;
                heightWeight = reference.paneHeightWeight;
                valueScale = reference.paneValueScale;
                fixedMinimum = reference.fixedMinimum;
                fixedMaximum = reference.fixedMaximum;
                valueDecimals = reference.valueDecimals;
                return true;
            }
            return false;
        }

        void DrawPaneSettings(
            app::IndicatorInstanceDefinition& draft,
            bool& dirty)
        {
            std::vector<PaneChoice> panes = CollectPaneChoices({}, &draft);
            if (panes.empty()) return;

            if (!ImGui::CollapsingHeader(
                    "패널/축 설정",
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }

            for (const PaneChoice& pane : panes) {
                float heightWeight = 0.3f;
                render::PaneValueScale valueScale =
                    render::PaneValueScale::Auto;
                double fixedMinimum = 0.0;
                double fixedMaximum = 100.0;
                int valueDecimals = 2;
                if (!ReadPaneContract(
                        draft,
                        pane.id,
                        heightWeight,
                        valueScale,
                        fixedMinimum,
                        fixedMaximum,
                        valueDecimals))
                {
                    continue;
                }

                ImGui::PushID(pane.id.c_str());
                const std::string heading =
                    pane.title + "  [" + pane.id + "]";
                if (ImGui::TreeNode(heading.c_str())) {
                    bool changed = false;
                    if (pane.id == "price") {
                        ImGui::TextDisabled(
                            "가격 패널의 높이와 축은 시장 차트 계약을 사용합니다.");
                    }
                    else {
                        if (ImGui::DragFloat(
                                "높이 비중",
                                &heightWeight,
                                0.01f,
                                0.05f,
                                5.0f,
                                "%.2f"))
                        {
                            changed = true;
                        }

                        const char* scales[] = {
                            "자동", "고정", "대칭" };
                        int scale = static_cast<int>(valueScale);
                        if (ImGui::Combo(
                                "축 범위",
                                &scale,
                                scales,
                                3))
                        {
                            valueScale =
                                static_cast<render::PaneValueScale>(scale);
                            changed = true;
                        }
                        if (valueScale == render::PaneValueScale::Fixed) {
                            changed = ImGui::InputDouble(
                                "최소값",
                                &fixedMinimum) || changed;
                            changed = ImGui::InputDouble(
                                "최대값",
                                &fixedMaximum) || changed;
                        }
                        changed = ImGui::InputInt(
                            "소수 자릿수",
                            &valueDecimals) || changed;
                        valueDecimals = (std::max)(
                            0,
                            (std::min)(8, valueDecimals));
                    }

                    if (changed) {
                        SetPaneContract(
                            draft,
                            pane.id,
                            heightWeight,
                            valueScale,
                            fixedMinimum,
                            fixedMaximum,
                            valueDecimals);
                        dirty = true;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

        void CopyPaneContractToReference(
            const app::IndicatorInstanceDefinition& definition,
            const std::string& paneId,
            app::IndicatorReferenceBinding& reference)
        {
            float heightWeight = 0.3f;
            render::PaneValueScale scale = render::PaneValueScale::Auto;
            double minimum = 0.0;
            double maximum = 100.0;
            int decimals = 2;
            if (ReadPaneContract(
                    definition,
                    paneId,
                    heightWeight,
                    scale,
                    minimum,
                    maximum,
                    decimals))
            {
                reference.paneHeightWeight = heightWeight;
                reference.paneValueScale = scale;
                reference.fixedMinimum = minimum;
                reference.fixedMaximum = maximum;
                reference.valueDecimals = decimals;
            }
        }

        std::string DefaultReferencePane(
            const app::IndicatorInstanceDefinition& definition)
        {
            for (const app::IndicatorOutputBinding& output :
                 definition.outputs)
            {
                if (output.paneId != "price") return output.paneId;
            }
            if (!definition.outputs.empty()) {
                return definition.outputs.front().paneId;
            }
            return "price";
        }

        void AddReference(
            app::IndicatorInstanceDefinition& definition,
            const std::string& label,
            double value,
            render::ColorRgba color)
        {
            std::size_t index = definition.references.size() + 1U;
            std::string id;
            for (;;) {
                id =
                    "indicator." + definition.spec.id +
                    ".reference.user." + std::to_string(index);
                bool exists = false;
                for (const app::IndicatorReferenceBinding& reference :
                     definition.references)
                {
                    if (reference.referenceId == id) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) break;
                ++index;
            }

            app::IndicatorReferenceBinding reference;
            reference.indicatorId = definition.spec.id;
            reference.referenceId = id;
            reference.label = label;
            reference.value = value;
            reference.color = color;
            reference.width = 1.0f;
            reference.style = render::LineStyle::Dashed;
            reference.paneId = DefaultReferencePane(definition);
            reference.paneTitle = reference.paneId;
            for (const app::IndicatorOutputBinding& output :
                 definition.outputs)
            {
                if (output.paneId == reference.paneId) {
                    reference.paneTitle = output.paneTitle;
                    break;
                }
            }
            CopyPaneContractToReference(
                definition,
                reference.paneId,
                reference);
            definition.references.push_back(std::move(reference));
        }

        void DrawOutputStyles(
            const std::vector<app::IndicatorInstanceDefinition>& definitions,
            IndicatorManagerUiState& state)
        {
            if (!ImGui::CollapsingHeader(
                    "출력 라인/히스토그램",
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }

            const std::vector<PaneChoice> panes =
                CollectPaneChoices(definitions, &state.draft);
            if (!ImGui::BeginTable(
                    "indicator_output_styles",
                    7,
                    ImGuiTableFlags_Borders |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingFixedFit |
                        ImGuiTableFlags_ScrollX))
            {
                return;
            }

            const char* headers[] = {
                "표시", "출력", "패널", "색상", "보조색",
                "두께", "스타일" };
            for (const char* header : headers) {
                ImGui::TableSetupColumn(header);
            }
            ImGui::TableHeadersRow();

            for (std::size_t index = 0;
                 index < state.draft.outputs.size();
                 ++index)
            {
                app::IndicatorOutputBinding& output =
                    state.draft.outputs[index];
                ImGui::TableNextRow();
                ImGui::PushID(output.seriesId.c_str());

                ImGui::TableNextColumn();
                if (ImGui::Checkbox("##visible", &output.visible)) {
                    state.dirty = true;
                }

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(output.label.c_str());

                ImGui::TableNextColumn();
                const std::string newPaneId =
                    "indicator." + state.draft.spec.id +
                    ".pane.output." + std::to_string(index + 1U);
                if (EditPaneSelection(
                        "##pane",
                        output.paneId,
                        output.paneTitle,
                        panes,
                        newPaneId,
                        state.draft.spec.type))
                {
                    state.dirty = true;
                }

                ImGui::TableNextColumn();
                if (EditColor("##primary", output.primaryColor)) {
                    state.dirty = true;
                }

                ImGui::TableNextColumn();
                if (output.kind == app::IndicatorRenderKind::Histogram) {
                    if (EditColor("##secondary", output.secondaryColor)) {
                        state.dirty = true;
                    }
                }
                else {
                    ImGui::TextDisabled("-");
                }

                ImGui::TableNextColumn();
                if (output.kind == app::IndicatorRenderKind::Line) {
                    ImGui::SetNextItemWidth(65.0f);
                    if (ImGui::DragFloat(
                            "##width",
                            &output.width,
                            0.1f,
                            0.5f,
                            8.0f,
                            "%.1f"))
                    {
                        state.dirty = true;
                    }
                }
                else {
                    ImGui::TextDisabled("-");
                }

                ImGui::TableNextColumn();
                if (output.kind == app::IndicatorRenderKind::Line) {
                    if (EditLineStyle("##style", output.style)) {
                        state.dirty = true;
                    }
                }
                else {
                    ImGui::TextDisabled("막대");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        void DrawReferences(
            const std::vector<app::IndicatorInstanceDefinition>& definitions,
            IndicatorManagerUiState& state)
        {
            if (!ImGui::CollapsingHeader(
                    "기준선 / 과매수 / 과매도",
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }

            const std::vector<PaneChoice> panes =
                CollectPaneChoices(definitions, &state.draft);
            std::size_t deleteIndex =
                (std::numeric_limits<std::size_t>::max)();

            if (ImGui::BeginTable(
                    "indicator_references",
                    8,
                    ImGuiTableFlags_Borders |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingFixedFit |
                        ImGuiTableFlags_ScrollX))
            {
                const char* headers[] = {
                    "표시", "이름", "값", "패널", "색상",
                    "두께", "스타일", "삭제" };
                for (const char* header : headers) {
                    ImGui::TableSetupColumn(header);
                }
                ImGui::TableHeadersRow();

                for (std::size_t index = 0;
                     index < state.draft.references.size();
                     ++index)
                {
                    app::IndicatorReferenceBinding& reference =
                        state.draft.references[index];
                    ImGui::TableNextRow();
                    ImGui::PushID(reference.referenceId.c_str());

                    ImGui::TableNextColumn();
                    if (ImGui::Checkbox(
                            "##visible",
                            &reference.visible))
                    {
                        state.dirty = true;
                    }

                    ImGui::TableNextColumn();
                    char label[64]{};
                    std::snprintf(
                        label,
                        sizeof(label),
                        "%s",
                        reference.label.c_str());
                    ImGui::SetNextItemWidth(100.0f);
                    if (ImGui::InputText(
                            "##label",
                            label,
                            sizeof(label)))
                    {
                        reference.label = label;
                        state.dirty = true;
                    }

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(85.0f);
                    if (ImGui::InputDouble(
                            "##value",
                            &reference.value,
                            0.0,
                            0.0,
                            "%.4f"))
                    {
                        state.dirty = true;
                    }

                    ImGui::TableNextColumn();
                    const std::string newPaneId =
                        "indicator." + state.draft.spec.id +
                        ".pane.reference." + std::to_string(index + 1U);
                    if (EditPaneSelection(
                            "##pane",
                            reference.paneId,
                            reference.paneTitle,
                            panes,
                            newPaneId,
                            state.draft.spec.type))
                    {
                        CopyPaneContractToReference(
                            state.draft,
                            reference.paneId,
                            reference);
                        state.dirty = true;
                    }

                    ImGui::TableNextColumn();
                    if (EditColor("##color", reference.color)) {
                        state.dirty = true;
                    }

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(65.0f);
                    if (ImGui::DragFloat(
                            "##width",
                            &reference.width,
                            0.1f,
                            0.5f,
                            8.0f,
                            "%.1f"))
                    {
                        state.dirty = true;
                    }

                    ImGui::TableNextColumn();
                    if (EditLineStyle("##style", reference.style)) {
                        state.dirty = true;
                    }

                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton("삭제")) {
                        deleteIndex = index;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            if (deleteIndex < state.draft.references.size()) {
                state.draft.references.erase(
                    state.draft.references.begin() +
                    static_cast<std::ptrdiff_t>(deleteIndex));
                state.dirty = true;
            }

            if (ImGui::Button("기준선 추가")) {
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
            std::vector<app::IndicatorInstanceDefinition>& definitions,
            IndicatorManagerUiState& state,
            ApplyIndicatorDefinitions applyDefinitions)
        {
            if (!ImGui::BeginPopupModal(
                    "지표 추가",
                    nullptr,
                    ImGuiWindowFlags_AlwaysAutoResize))
            {
                return;
            }

            const std::vector<app::IndicatorCatalogEntry>& catalog =
                app::IndicatorCatalog();
            state.addTypeIndex = (std::max)(
                0,
                (std::min)(
                    state.addTypeIndex,
                    static_cast<int>(catalog.size()) - 1));

            if (!catalog.empty()) {
                const char* preview =
                    catalog[static_cast<std::size_t>(state.addTypeIndex)]
                        .displayName.c_str();
                if (ImGui::BeginCombo("지표", preview)) {
                    for (std::size_t index = 0;
                         index < catalog.size();
                         ++index)
                    {
                        const bool selected =
                            static_cast<int>(index) == state.addTypeIndex;
                        if (ImGui::Selectable(
                                catalog[index].displayName.c_str(),
                                selected))
                        {
                            state.addTypeIndex =
                                static_cast<int>(index);
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            const std::vector<PaneChoice> panes =
                CollectPaneChoices(definitions);
            std::vector<PaneChoice> targets;
            targets.push_back({ "__default__", "기본 배치" });
            targets.push_back({ "price", "가격 패널" });
            targets.push_back({ "__new__", "새 하단 패널" });
            for (const PaneChoice& pane : panes) {
                if (pane.id != "price") targets.push_back(pane);
            }
            state.addPaneIndex = (std::max)(
                0,
                (std::min)(
                    state.addPaneIndex,
                    static_cast<int>(targets.size()) - 1));
            if (!targets.empty()) {
                const char* preview =
                    targets[static_cast<std::size_t>(state.addPaneIndex)]
                        .title.c_str();
                if (ImGui::BeginCombo("삽입 패널", preview)) {
                    for (std::size_t index = 0;
                         index < targets.size();
                         ++index)
                    {
                        const bool selected =
                            static_cast<int>(index) == state.addPaneIndex;
                        if (ImGui::Selectable(
                                targets[index].title.c_str(),
                                selected))
                        {
                            state.addPaneIndex =
                                static_cast<int>(index);
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::Separator();
            if (ImGui::Button("추가", ImVec2(100.0f, 0.0f)) &&
                !catalog.empty())
            {
                const app::IndicatorCatalogEntry& selectedType =
                    catalog[static_cast<std::size_t>(state.addTypeIndex)];
                const std::string id = app::NextIndicatorInstanceId(
                    selectedType.type,
                    definitions);
                app::IndicatorInstanceDefinition definition;
                std::string error;
                if (app::CreateDefaultIndicatorDefinition(
                        selectedType.type,
                        id,
                        definition,
                        error))
                {
                    std::size_t sameTypeCount = 0;
                    for (const auto& existing : definitions) {
                        if (existing.spec.type == selectedType.type) {
                            ++sameTypeCount;
                        }
                    }
                    app::ApplyIndicatorColorVariant(
                        definition,
                        sameTypeCount);

                    const PaneChoice& target =
                        targets[static_cast<std::size_t>(state.addPaneIndex)];
                    if (target.id == "price") {
                        app::MoveIndicatorToPane(
                            definition,
                            "price",
                            "Price",
                            error);
                    }
                    else if (target.id == "__new__") {
                        app::MoveIndicatorToPane(
                            definition,
                            "indicator." + id + ".pane",
                            selectedType.displayName,
                            error);
                    }
                    else if (target.id != "__default__") {
                        app::MoveIndicatorToPane(
                            definition,
                            target.id,
                            target.title,
                            error);
                    }

                    std::vector<app::IndicatorInstanceDefinition> candidate =
                        definitions;
                    candidate.push_back(definition);
                    if (error.empty() && CommitDefinitions(
                            definitions,
                            candidate,
                            state,
                            applyDefinitions))
                    {
                        ResetDraft(state, definitions.back());
                        ImGui::CloseCurrentPopup();
                    }
                    else if (!error.empty()) {
                        state.error = error;
                    }
                }
                else {
                    state.error = error;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("취소", ImVec2(100.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }

            if (!state.error.empty()) {
                ImGui::Separator();
                ImGui::TextColored(
                    ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
                    "%s",
                    state.error.c_str());
            }
            ImGui::EndPopup();
        }
    }

    void SelectIndicator(
        IndicatorManagerUiState& state,
        const std::string& indicatorId,
        bool focusProperties)
    {
        state.selectedIndicatorId = indicatorId;
        state.draftValid = false;
        state.dirty = false;
        state.error.clear();
        state.focusRequested = focusProperties;
    }

    void DrawIndicatorManagerWindow(
        std::vector<app::IndicatorInstanceDefinition>& definitions,
        IndicatorManagerUiState& state,
        ApplyIndicatorDefinitions applyDefinitions)
    {
        if (state.focusRequested) ImGui::SetNextWindowFocus();
        ImGui::Begin("프로퍼티");
        state.focusRequested = false;

        EnsureSelection(definitions, state);
        const app::IndicatorInstanceDefinition* selected =
            SelectedDefinition(definitions, state);

        const std::string preview = selected != nullptr
            ? DefinitionLabel(*selected)
            : std::string("선택된 지표 없음");
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("지표", preview.c_str())) {
            for (const app::IndicatorInstanceDefinition& definition :
                 definitions)
            {
                const bool isSelected =
                    definition.spec.id == state.selectedIndicatorId;
                const std::string label = DefinitionLabel(definition);
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    ResetDraft(state, definition);
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("지표 추가")) {
            state.error.clear();
            ImGui::OpenPopup("지표 추가");
        }
        ImGui::SameLine();

        const bool noSelection = selected == nullptr;
        if (noSelection) ImGui::BeginDisabled();
        if (ImGui::Button("복제")) {
            const std::string newId = app::NextIndicatorInstanceId(
                selected->spec.type,
                definitions);
            std::size_t sameTypeCount = 0;
            for (const auto& definition : definitions) {
                if (definition.spec.type == selected->spec.type) {
                    ++sameTypeCount;
                }
            }
            app::IndicatorInstanceDefinition duplicate;
            std::string error;
            if (app::DuplicateIndicatorDefinition(
                    *selected,
                    newId,
                    sameTypeCount,
                    duplicate,
                    error))
            {
                duplicate.visible = true;
                std::vector<app::IndicatorInstanceDefinition> candidate =
                    definitions;
                candidate.push_back(duplicate);
                if (CommitDefinitions(
                        definitions,
                        candidate,
                        state,
                        applyDefinitions))
                {
                    ResetDraft(state, definitions.back());
                }
            }
            else {
                state.error = error;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(
                selected != nullptr && selected->visible
                    ? "감추기"
                    : "표시"))
        {
            std::vector<app::IndicatorInstanceDefinition> candidate =
                definitions;
            app::IndicatorInstanceDefinition* target =
                app::FindIndicatorDefinition(
                    candidate,
                    state.selectedIndicatorId);
            if (target != nullptr) {
                target->visible = !target->visible;
                if (CommitDefinitions(
                        definitions,
                        candidate,
                        state,
                        applyDefinitions))
                {
                    const app::IndicatorInstanceDefinition* updated =
                        SelectedDefinition(definitions, state);
                    if (updated != nullptr) ResetDraft(state, *updated);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("삭제")) {
            ImGui::OpenPopup("지표 삭제 확인");
        }
        if (noSelection) ImGui::EndDisabled();

        if (ImGui::BeginPopupModal(
                "지표 삭제 확인",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("선택한 지표 인스턴스를 삭제합니다.");
            ImGui::TextDisabled("파라미터와 라인/기준선 설정도 함께 제거됩니다.");
            ImGui::Separator();
            if (ImGui::Button("삭제 실행", ImVec2(110.0f, 0.0f))) {
                std::vector<app::IndicatorInstanceDefinition> candidate;
                candidate.reserve(definitions.size());
                for (const auto& definition : definitions) {
                    if (definition.spec.id != state.selectedIndicatorId) {
                        candidate.push_back(definition);
                    }
                }
                if (CommitDefinitions(
                        definitions,
                        candidate,
                        state,
                        applyDefinitions))
                {
                    state.selectedIndicatorId = definitions.empty()
                        ? std::string{}
                        : definitions.front().spec.id;
                    state.draftValid = false;
                    state.dirty = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("취소", ImVec2(110.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        DrawAddIndicatorPopup(
            definitions,
            state,
            applyDefinitions);

        selected = SelectedDefinition(definitions, state);
        if (selected == nullptr || !state.draftValid) {
            ImGui::Separator();
            ImGui::TextDisabled(
                "지표를 추가하거나 범례/목록에서 선택하십시오.");
            if (!state.error.empty()) {
                ImGui::TextColored(
                    ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
                    "%s",
                    state.error.c_str());
            }
            ImGui::End();
            return;
        }

        ImGui::Separator();
        ImGui::Text(
            "%s",
            app::IndicatorLegendLabel(state.draft.spec).c_str());
        ImGui::TextDisabled(
            "ID: %s  Type: %s",
            state.draft.spec.id.c_str(),
            state.draft.spec.type.c_str());
        if (ImGui::Checkbox("인스턴스 표시", &state.draft.visible)) {
            state.dirty = true;
        }

        app::IndicatorPropertySnapshot properties;
        std::string descriptionError;
        if (app::DescribeIndicatorProperties(
                state.draft.spec,
                properties,
                descriptionError))
        {
            if (ImGui::CollapsingHeader(
                    "계산 파라미터",
                    ImGuiTreeNodeFlags_DefaultOpen) &&
                ImGui::BeginTable(
                    "indicator_parameters",
                    2,
                    ImGuiTableFlags_Borders |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("속성");
                ImGui::TableSetupColumn("값");
                ImGui::TableHeadersRow();
                for (const app::IndicatorParameterDescriptor& descriptor :
                     properties.parameters)
                {
                    ImGui::TableNextRow();
                    ImGui::PushID(descriptor.key.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(descriptor.displayName.c_str());
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1.0f);
                    double& draftValue =
                        state.draft.spec.parameters[descriptor.key];
                    bool changed = false;
                    if (descriptor.kind ==
                        app::IndicatorParameterKind::Integer)
                    {
                        int value = static_cast<int>(
                            std::llround(draftValue));
                        const int step = static_cast<int>(
                            std::llround(descriptor.step));
                        const int fast = static_cast<int>(
                            std::llround(descriptor.fastStep));
                        if (ImGui::InputInt(
                                "##value",
                                &value,
                                step,
                                fast))
                        {
                            draftValue = static_cast<double>(value);
                            changed = true;
                        }
                    }
                    else {
                        double value = draftValue;
                        if (ImGui::InputDouble(
                                "##value",
                                &value,
                                descriptor.step,
                                descriptor.fastStep,
                                "%.4f"))
                        {
                            draftValue = value;
                            changed = true;
                        }
                    }
                    if (changed) state.dirty = true;
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        else {
            state.error = descriptionError;
        }

        DrawOutputStyles(definitions, state);
        DrawPaneSettings(state.draft, state.dirty);
        DrawReferences(definitions, state);

        ImGui::Separator();
        const bool applyDisabled = !state.dirty;
        if (applyDisabled) ImGui::BeginDisabled();
        if (ImGui::Button("적용", ImVec2(90.0f, 0.0f))) {
            std::vector<app::IndicatorInstanceDefinition> candidate =
                definitions;
            app::IndicatorInstanceDefinition* target =
                app::FindIndicatorDefinition(
                    candidate,
                    state.selectedIndicatorId);
            if (target == nullptr) {
                state.error = "선택한 지표 인스턴스가 없습니다.";
            }
            else {
                *target = state.draft;
                if (CommitDefinitions(
                        definitions,
                        candidate,
                        state,
                        applyDefinitions))
                {
                    const app::IndicatorInstanceDefinition* updated =
                        SelectedDefinition(definitions, state);
                    if (updated != nullptr) ResetDraft(state, *updated);
                }
            }
        }
        if (applyDisabled) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("되돌리기", ImVec2(90.0f, 0.0f))) {
            const app::IndicatorInstanceDefinition* current =
                SelectedDefinition(definitions, state);
            if (current != nullptr) ResetDraft(state, *current);
        }

        if (!state.error.empty()) {
            ImGui::Separator();
            ImGui::TextColored(
                ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
                "%s",
                state.error.c_str());
        }
        ImGui::End();
    }
}
