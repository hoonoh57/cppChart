#include "comparison_manager_ui.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace trading::ui
{
    namespace
    {
        const app::ComparisonDefinition* FindDefinition(
            const std::vector<app::ComparisonDefinition>& definitions,
            const std::string& id) noexcept
        {
            for (const auto& definition : definitions) {
                if (definition.id == id) return &definition;
            }
            return nullptr;
        }

        app::ComparisonDefinition* FindDefinition(
            std::vector<app::ComparisonDefinition>& definitions,
            const std::string& id) noexcept
        {
            for (auto& definition : definitions) {
                if (definition.id == id) return &definition;
            }
            return nullptr;
        }

        const app::ComparisonSeriesSnapshot* FindSnapshot(
            const app::ComparisonModuleSnapshot& snapshot,
            const std::string& id) noexcept
        {
            for (const auto& series : snapshot.series) {
                if (series.definition.id == id) return &series;
            }
            return nullptr;
        }

        void ResetDraft(
            ComparisonManagerUiState& state,
            const app::ComparisonDefinition& definition)
        {
            state.selectedId = definition.id;
            state.draft = definition;
            state.draftValid = true;
            state.dirty = false;
            state.error.clear();
        }

        void EnsureSelection(
            const std::vector<app::ComparisonDefinition>& definitions,
            ComparisonManagerUiState& state)
        {
            const app::ComparisonDefinition* selected =
                FindDefinition(definitions, state.selectedId);
            if (selected == nullptr && !definitions.empty()) {
                selected = &definitions.front();
                state.selectedId = selected->id;
            }
            if (selected != nullptr &&
                (!state.draftValid || state.draft.id != selected->id))
            {
                ResetDraft(state, *selected);
            }
        }

        bool Commit(
            std::vector<app::ComparisonDefinition>& definitions,
            const std::vector<app::ComparisonDefinition>& candidate,
            ComparisonManagerUiState& state,
            ApplyComparisonDefinitions applyDefinitions)
        {
            if (applyDefinitions == nullptr) {
                state.error = "비교 구성 적용 함수가 없습니다.";
                return false;
            }
            std::string error;
            if (!applyDefinitions(candidate, error)) {
                state.error = error.empty()
                    ? "비교 구성을 적용하지 못했습니다."
                    : error;
                return false;
            }
            definitions = candidate;
            state.error.clear();
            return true;
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

        bool EditStyle(
            const char* id,
            render::LineStyle& style)
        {
            const char* styles[] = { "실선", "파선", "점선" };
            int selected = static_cast<int>(style);
            if (!ImGui::Combo(id, &selected, styles, 3)) return false;
            style = static_cast<render::LineStyle>(selected);
            return true;
        }

        void SetAddPreset(
            ComparisonManagerUiState& state,
            const char* code,
            const char* name)
        {
            state.addKind = 1;
            std::snprintf(state.addCode, sizeof(state.addCode), "%s", code);
            std::snprintf(state.addName, sizeof(state.addName), "%s", name);
        }

        app::ComparisonDefinition BuildNewDefinition(
            const std::vector<app::ComparisonDefinition>& definitions,
            const ComparisonManagerUiState& state)
        {
            app::ComparisonDefinition definition;
            definition.kind = state.addKind == 1
                ? app::ComparisonInstrumentKind::Index
                : app::ComparisonInstrumentKind::Stock;
            definition.id = app::NextComparisonId(
                definition.kind,
                definitions);
            definition.code = state.addCode;
            definition.displayName = state.addName;
            if (definition.displayName.empty()) {
                definition.displayName = definition.code;
            }
            definition.placement = state.addPlacement == 1
                ? app::ComparisonPlacement::PriceSecondaryAxis
                : app::ComparisonPlacement::SeparatePane;
            definition.paneId =
                "comparison." + definition.id + ".pane";
            definition.paneTitle = definition.displayName;
            definition.valueDivisor =
                definition.kind == app::ComparisonInstrumentKind::Index
                    ? 100.0
                    : 1.0;
            definition.valueDecimals =
                definition.kind == app::ComparisonInstrumentKind::Index
                    ? 2
                    : 0;

            static constexpr render::ColorRgba palette[] = {
                { 64, 210, 225, 255 },
                { 255, 196, 64, 255 },
                { 235, 92, 188, 255 },
                { 96, 214, 126, 255 },
                { 255, 132, 72, 255 },
                { 122, 152, 255, 255 }
            };
            definition.color = palette[
                definitions.size() %
                (sizeof(palette) / sizeof(palette[0]))];
            return definition;
        }

        void DrawAddPopup(
            std::vector<app::ComparisonDefinition>& definitions,
            ComparisonManagerUiState& state,
            ApplyComparisonDefinitions applyDefinitions,
            RequestComparisonData requestData)
        {
            if (!ImGui::BeginPopupModal(
                    "비교 시계열 추가",
                    nullptr,
                    ImGuiWindowFlags_AlwaysAutoResize))
            {
                return;
            }

            const char* kinds[] = { "종목", "지수/업종" };
            ImGui::Combo("종류", &state.addKind, kinds, 2);
            ImGui::InputText(
                "코드",
                state.addCode,
                sizeof(state.addCode));
            ImGui::InputText(
                "표시명",
                state.addName,
                sizeof(state.addName));
            const char* placements[] = {
                "별도 하단 패널",
                "가격 패널 이중축" };
            ImGui::Combo(
                "삽입 방식",
                &state.addPlacement,
                placements,
                2);

            ImGui::Separator();
            ImGui::TextDisabled("지수 빠른 선택");
            if (ImGui::Button("KOSPI")) {
                SetAddPreset(state, "001", "KOSPI");
            }
            ImGui::SameLine();
            if (ImGui::Button("KOSDAQ")) {
                SetAddPreset(state, "101", "KOSDAQ");
            }

            ImGui::Separator();
            if (ImGui::Button("추가 및 조회", ImVec2(120.0f, 0.0f))) {
                app::ComparisonDefinition definition =
                    BuildNewDefinition(definitions, state);
                std::string validationError;
                if (!app::ComparisonModule::ValidateDefinition(
                        definition,
                        validationError))
                {
                    state.error = validationError;
                }
                else {
                    std::vector<app::ComparisonDefinition> candidate =
                        definitions;
                    candidate.push_back(definition);
                    if (Commit(
                            definitions,
                            candidate,
                            state,
                            applyDefinitions))
                    {
                        ResetDraft(state, definitions.back());
                        std::string requestError;
                        if (requestData == nullptr ||
                            !requestData(definition.id, requestError))
                        {
                            state.error = requestError.empty()
                                ? "비교 데이터 조회를 시작하지 못했습니다."
                                : requestError;
                        }
                        else {
                            state.addCode[0] = '\0';
                            state.addName[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                    }
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

    void SelectComparison(
        ComparisonManagerUiState& state,
        const std::string& comparisonId,
        bool focusWindow)
    {
        state.selectedId = comparisonId;
        state.draftValid = false;
        state.dirty = false;
        state.error.clear();
        state.focusRequested = focusWindow;
    }

    void DrawComparisonManagerWindow(
        std::vector<app::ComparisonDefinition>& definitions,
        const app::ComparisonModuleSnapshot& snapshot,
        ComparisonManagerUiState& state,
        ApplyComparisonDefinitions applyDefinitions,
        RequestComparisonData requestData)
    {
        if (state.focusRequested) ImGui::SetNextWindowFocus();
        ImGui::Begin("비교");
        state.focusRequested = false;
        EnsureSelection(definitions, state);

        const app::ComparisonDefinition* selected =
            FindDefinition(definitions, state.selectedId);
        const std::string preview = selected != nullptr
            ? selected->displayName + " " + selected->code
            : std::string("선택 없음");
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("시계열", preview.c_str())) {
            for (const auto& definition : definitions) {
                const bool isSelected = definition.id == state.selectedId;
                std::string label =
                    definition.displayName + " " + definition.code;
                if (!definition.visible) label += " [숨김]";
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    ResetDraft(state, definition);
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("추가")) {
            state.error.clear();
            ImGui::OpenPopup("비교 시계열 추가");
        }
        ImGui::SameLine();
        const bool noSelection = selected == nullptr;
        const bool selectedVisible =
            selected != nullptr && selected->visible;
        if (noSelection) ImGui::BeginDisabled();
        if (ImGui::Button(selectedVisible ? "감추기" : "표시")) {
            std::vector<app::ComparisonDefinition> candidate = definitions;
            app::ComparisonDefinition* target =
                FindDefinition(candidate, state.selectedId);
            if (target != nullptr) {
                target->visible = !target->visible;
                if (Commit(
                        definitions,
                        candidate,
                        state,
                        applyDefinitions))
                {
                    const auto* updated =
                        FindDefinition(definitions, state.selectedId);
                    if (updated != nullptr) ResetDraft(state, *updated);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("재조회")) {
            std::string requestError;
            if (requestData == nullptr ||
                !requestData(state.selectedId, requestError))
            {
                state.error = requestError;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("삭제")) {
            ImGui::OpenPopup("비교 삭제 확인");
        }
        if (noSelection) ImGui::EndDisabled();

        if (ImGui::BeginPopupModal(
                "비교 삭제 확인",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("선택한 비교 시계열을 삭제합니다.");
            if (ImGui::Button("삭제 실행", ImVec2(110.0f, 0.0f))) {
                std::vector<app::ComparisonDefinition> candidate;
                for (const auto& definition : definitions) {
                    if (definition.id != state.selectedId) {
                        candidate.push_back(definition);
                    }
                }
                if (Commit(
                        definitions,
                        candidate,
                        state,
                        applyDefinitions))
                {
                    state.selectedId = definitions.empty()
                        ? std::string{}
                        : definitions.front().id;
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

        DrawAddPopup(
            definitions,
            state,
            applyDefinitions,
            requestData);

        selected = FindDefinition(definitions, state.selectedId);
        if (selected == nullptr || !state.draftValid) {
            ImGui::Separator();
            ImGui::TextDisabled("종목이나 지수를 추가하십시오.");
            if (!state.error.empty()) {
                ImGui::TextColored(
                    ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
                    "%s",
                    state.error.c_str());
            }
            ImGui::End();
            return;
        }

        const app::ComparisonSeriesSnapshot* series =
            FindSnapshot(snapshot, state.selectedId);
        ImGui::Separator();
        ImGui::Text(
            "%s  %s",
            state.draft.displayName.c_str(),
            state.draft.code.c_str());
        if (series != nullptr) {
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%s / %zu봉",
                app::ComparisonModule::StateName(series->state),
                series->barCount);
            if (!series->error.empty()) {
                ImGui::TextWrapped("%s", series->error.c_str());
            }
        }

        char displayName[128]{};
        std::snprintf(
            displayName,
            sizeof(displayName),
            "%s",
            state.draft.displayName.c_str());
        if (ImGui::InputText(
                "표시명",
                displayName,
                sizeof(displayName)))
        {
            state.draft.displayName = displayName;
            state.dirty = true;
        }
        const char* placements[] = {
            "별도 하단 패널",
            "가격 패널 이중축" };
        int placement =
            state.draft.placement ==
                app::ComparisonPlacement::PriceSecondaryAxis
                ? 1
                : 0;
        if (ImGui::Combo("삽입 방식", &placement, placements, 2)) {
            state.draft.placement = placement == 1
                ? app::ComparisonPlacement::PriceSecondaryAxis
                : app::ComparisonPlacement::SeparatePane;
            state.dirty = true;
        }
        if (placement == 0) {
            char paneId[128]{};
            char paneTitle[128]{};
            std::snprintf(
                paneId,
                sizeof(paneId),
                "%s",
                state.draft.paneId.c_str());
            std::snprintf(
                paneTitle,
                sizeof(paneTitle),
                "%s",
                state.draft.paneTitle.c_str());
            if (ImGui::InputText(
                    "패널 ID",
                    paneId,
                    sizeof(paneId)))
            {
                state.draft.paneId = paneId;
                state.dirty = true;
            }
            if (ImGui::InputText(
                    "패널 제목",
                    paneTitle,
                    sizeof(paneTitle)))
            {
                state.draft.paneTitle = paneTitle;
                state.dirty = true;
            }
            if (ImGui::DragFloat(
                    "패널 높이",
                    &state.draft.paneHeightWeight,
                    0.01f,
                    0.05f,
                    5.0f,
                    "%.2f"))
            {
                state.dirty = true;
            }
        }

        if (EditColor("선 색상", state.draft.color)) {
            state.dirty = true;
        }
        if (ImGui::DragFloat(
                "선 두께",
                &state.draft.width,
                0.1f,
                0.5f,
                8.0f,
                "%.1f"))
        {
            state.dirty = true;
        }
        if (EditStyle("선 스타일", state.draft.style)) {
            state.dirty = true;
        }
        if (ImGui::InputDouble(
                "값 나눗수",
                &state.draft.valueDivisor,
                0.0,
                0.0,
                "%.4f"))
        {
            state.dirty = true;
        }
        if (ImGui::InputInt(
                "축 소수 자릿수",
                &state.draft.valueDecimals))
        {
            state.draft.valueDecimals = (std::max)(
                0,
                (std::min)(8, state.draft.valueDecimals));
            state.dirty = true;
        }

        ImGui::Separator();
        const bool applyDisabled = !state.dirty;
        if (applyDisabled) ImGui::BeginDisabled();
        if (ImGui::Button("적용", ImVec2(90.0f, 0.0f))) {
            std::vector<app::ComparisonDefinition> candidate = definitions;
            app::ComparisonDefinition* target =
                FindDefinition(candidate, state.selectedId);
            if (target != nullptr) {
                *target = state.draft;
                if (Commit(
                        definitions,
                        candidate,
                        state,
                        applyDefinitions))
                {
                    const auto* updated =
                        FindDefinition(definitions, state.selectedId);
                    if (updated != nullptr) ResetDraft(state, *updated);
                }
            }
        }
        if (applyDisabled) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("되돌리기", ImVec2(90.0f, 0.0f))) {
            const auto* current =
                FindDefinition(definitions, state.selectedId);
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
