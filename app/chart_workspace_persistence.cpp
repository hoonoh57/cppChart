#include "chart_workspace_persistence.h"

#include "../core/json_lite.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace trading::app
{
    namespace
    {
        constexpr int SchemaVersion = 1;

        void AppendQuoted(std::ostringstream& stream, const std::string& value)
        {
            stream << '"' << json_lite::EscapeString(value) << '"';
        }

        void AppendColor(
            std::ostringstream& stream,
            const render::ColorRgba& color)
        {
            stream << '['
                   << static_cast<int>(color.red) << ','
                   << static_cast<int>(color.green) << ','
                   << static_cast<int>(color.blue) << ','
                   << static_cast<int>(color.alpha) << ']';
        }

        void AppendValueGrid(
            std::ostringstream& stream,
            const render::ValueGrid& grid)
        {
            stream << "{\"enabled\":"
                   << (grid.enabled ? "true" : "false")
                   << ",\"fallback_step\":" << grid.fallbackStep
                   << ",\"bands\":[";
            for (std::size_t index = 0; index < grid.bands.size(); ++index) {
                if (index > 0U) stream << ',';
                stream << "{\"upper_exclusive\":"
                       << grid.bands[index].upperExclusive
                       << ",\"step\":" << grid.bands[index].step << '}';
            }
            stream << "]}";
        }

        void AppendOutput(
            std::ostringstream& stream,
            const IndicatorOutputBinding& output)
        {
            stream << '{';
            stream << "\"output_index\":" << output.outputIndex;
            stream << ",\"kind\":" << static_cast<int>(output.kind);
            stream << ",\"pane_id\":"; AppendQuoted(stream, output.paneId);
            stream << ",\"pane_title\":"; AppendQuoted(stream, output.paneTitle);
            stream << ",\"pane_height_weight\":" << output.paneHeightWeight;
            stream << ",\"pane_value_scale\":"
                   << static_cast<int>(output.paneValueScale);
            stream << ",\"fixed_minimum\":" << output.fixedMinimum;
            stream << ",\"fixed_maximum\":" << output.fixedMaximum;
            stream << ",\"cursor_grid\":"; AppendValueGrid(stream, output.cursorGrid);
            stream << ",\"value_decimals\":" << output.valueDecimals;
            stream << ",\"series_id\":"; AppendQuoted(stream, output.seriesId);
            stream << ",\"label\":"; AppendQuoted(stream, output.label);
            stream << ",\"primary_color\":"; AppendColor(stream, output.primaryColor);
            stream << ",\"secondary_color\":"; AppendColor(stream, output.secondaryColor);
            stream << ",\"width\":" << output.width;
            stream << ",\"style\":" << static_cast<int>(output.style);
            stream << ",\"visible\":" << (output.visible ? "true" : "false");
            stream << ",\"legend_role\":"; AppendQuoted(stream, output.legendRole);
            stream << ",\"legend_label\":"; AppendQuoted(stream, output.legendLabel);
            stream << '}';
        }

        void AppendReference(
            std::ostringstream& stream,
            const IndicatorReferenceBinding& reference)
        {
            stream << '{';
            stream << "\"pane_id\":"; AppendQuoted(stream, reference.paneId);
            stream << ",\"pane_title\":"; AppendQuoted(stream, reference.paneTitle);
            stream << ",\"pane_height_weight\":" << reference.paneHeightWeight;
            stream << ",\"pane_value_scale\":"
                   << static_cast<int>(reference.paneValueScale);
            stream << ",\"fixed_minimum\":" << reference.fixedMinimum;
            stream << ",\"fixed_maximum\":" << reference.fixedMaximum;
            stream << ",\"cursor_grid\":"; AppendValueGrid(stream, reference.cursorGrid);
            stream << ",\"value_decimals\":" << reference.valueDecimals;
            stream << ",\"reference_id\":"; AppendQuoted(stream, reference.referenceId);
            stream << ",\"label\":"; AppendQuoted(stream, reference.label);
            stream << ",\"value\":" << reference.value;
            stream << ",\"color\":"; AppendColor(stream, reference.color);
            stream << ",\"width\":" << reference.width;
            stream << ",\"style\":" << static_cast<int>(reference.style);
            stream << ",\"visible\":" << (reference.visible ? "true" : "false");
            stream << '}';
        }

        bool ReadTextFile(
            const std::filesystem::path& path,
            std::string& text,
            std::string& error)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                error = "작업공간 파일을 열지 못했습니다: " + path.string();
                return false;
            }
            std::ostringstream buffer;
            buffer << input.rdbuf();
            if (!input.good() && !input.eof()) {
                error = "작업공간 파일을 읽지 못했습니다: " + path.string();
                return false;
            }
            text = buffer.str();
            if (text.size() >= 3U &&
                static_cast<unsigned char>(text[0]) == 0xEFU &&
                static_cast<unsigned char>(text[1]) == 0xBBU &&
                static_cast<unsigned char>(text[2]) == 0xBFU)
            {
                text.erase(0U, 3U);
            }
            error.clear();
            return true;
        }

        const json_lite::Value* Find(
            const json_lite::Value& object,
            const char* key)
        {
            return object.IsObject() ? object.Find(key) : nullptr;
        }

        std::string StringValue(
            const json_lite::Value& object,
            const char* key,
            const std::string& fallback = {})
        {
            const json_lite::Value* value = Find(object, key);
            return value != nullptr && value->IsString()
                ? value->AsString()
                : fallback;
        }

        bool BooleanValue(
            const json_lite::Value& object,
            const char* key,
            bool fallback)
        {
            const json_lite::Value* value = Find(object, key);
            return value != nullptr && value->IsBoolean()
                ? value->AsBoolean(fallback)
                : fallback;
        }

        double NumberValue(
            const json_lite::Value& object,
            const char* key,
            double fallback)
        {
            const json_lite::Value* value = Find(object, key);
            return value != nullptr && value->IsNumber()
                ? value->AsNumber(fallback)
                : fallback;
        }

        int IntegerValue(
            const json_lite::Value& object,
            const char* key,
            int fallback)
        {
            const json_lite::Value* value = Find(object, key);
            return value != nullptr && value->IsNumber()
                ? value->AsInt(fallback)
                : fallback;
        }

        render::ColorRgba ParseColor(
            const json_lite::Value* value,
            render::ColorRgba fallback)
        {
            if (value == nullptr || !value->IsArray() ||
                value->AsArray().size() != 4U)
            {
                return fallback;
            }
            const auto& values = value->AsArray();
            auto channel = [&](std::size_t index, std::uint8_t current) {
                if (!values[index].IsNumber()) return current;
                return static_cast<std::uint8_t>((std::max)(
                    0,
                    (std::min)(255, values[index].AsInt(current))));
            };
            fallback.red = channel(0U, fallback.red);
            fallback.green = channel(1U, fallback.green);
            fallback.blue = channel(2U, fallback.blue);
            fallback.alpha = channel(3U, fallback.alpha);
            return fallback;
        }

        render::ValueGrid ParseValueGrid(
            const json_lite::Value* value,
            const render::ValueGrid& fallback)
        {
            if (value == nullptr || !value->IsObject()) return fallback;
            render::ValueGrid grid = fallback;
            grid.enabled = BooleanValue(*value, "enabled", grid.enabled);
            grid.fallbackStep = NumberValue(
                *value,
                "fallback_step",
                grid.fallbackStep);
            const json_lite::Value* bands = Find(*value, "bands");
            if (bands != nullptr && bands->IsArray()) {
                grid.bands.clear();
                for (const json_lite::Value& item : bands->AsArray()) {
                    if (!item.IsObject()) continue;
                    render::ValueGridBand band;
                    band.upperExclusive = NumberValue(
                        item,
                        "upper_exclusive",
                        0.0);
                    band.step = NumberValue(item, "step", 0.0);
                    grid.bands.push_back(band);
                }
            }
            const char* validationError = nullptr;
            return render::ValidateValueGrid(grid, &validationError)
                ? grid
                : fallback;
        }

        render::PaneValueScale ParsePaneScale(int value)
        {
            if (value < static_cast<int>(render::PaneValueScale::Auto) ||
                value > static_cast<int>(render::PaneValueScale::Symmetric))
            {
                return render::PaneValueScale::Auto;
            }
            return static_cast<render::PaneValueScale>(value);
        }

        render::LineStyle ParseLineStyle(int value)
        {
            if (value < static_cast<int>(render::LineStyle::Solid) ||
                value > static_cast<int>(render::LineStyle::Dotted))
            {
                return render::LineStyle::Solid;
            }
            return static_cast<render::LineStyle>(value);
        }

        IndicatorRenderKind ParseRenderKind(int value)
        {
            return value == static_cast<int>(IndicatorRenderKind::Histogram)
                ? IndicatorRenderKind::Histogram
                : IndicatorRenderKind::Line;
        }

        bool ParseOutput(
            const json_lite::Value& value,
            const indicators::IndicatorSpec& spec,
            const IndicatorOutputBinding* fallback,
            IndicatorOutputBinding& output,
            std::string& error)
        {
            if (!value.IsObject()) {
                error = "지표 출력 설정이 JSON 객체가 아닙니다.";
                return false;
            }
            output = fallback != nullptr ? *fallback : IndicatorOutputBinding{};
            output.indicatorId = spec.id;
            output.outputIndex = static_cast<std::size_t>((std::max)(
                0,
                IntegerValue(value, "output_index", static_cast<int>(output.outputIndex))));
            output.kind = ParseRenderKind(IntegerValue(
                value,
                "kind",
                static_cast<int>(output.kind)));
            output.paneId = StringValue(value, "pane_id", output.paneId);
            output.paneTitle = StringValue(value, "pane_title", output.paneTitle);
            output.paneHeightWeight = static_cast<float>(NumberValue(
                value,
                "pane_height_weight",
                output.paneHeightWeight));
            output.paneValueScale = ParsePaneScale(IntegerValue(
                value,
                "pane_value_scale",
                static_cast<int>(output.paneValueScale)));
            output.fixedMinimum = NumberValue(
                value,
                "fixed_minimum",
                output.fixedMinimum);
            output.fixedMaximum = NumberValue(
                value,
                "fixed_maximum",
                output.fixedMaximum);
            output.cursorGrid = ParseValueGrid(
                Find(value, "cursor_grid"),
                output.cursorGrid);
            output.valueDecimals = IntegerValue(
                value,
                "value_decimals",
                output.valueDecimals);
            output.seriesId = StringValue(value, "series_id", output.seriesId);
            output.label = StringValue(value, "label", output.label);
            output.primaryColor = ParseColor(
                Find(value, "primary_color"),
                output.primaryColor);
            output.secondaryColor = ParseColor(
                Find(value, "secondary_color"),
                output.secondaryColor);
            output.width = static_cast<float>(NumberValue(
                value,
                "width",
                output.width));
            output.style = ParseLineStyle(IntegerValue(
                value,
                "style",
                static_cast<int>(output.style)));
            output.visible = BooleanValue(value, "visible", output.visible);
            output.legendRole = StringValue(
                value,
                "legend_role",
                output.legendRole);
            output.legendLabel = StringValue(
                value,
                "legend_label",
                output.legendLabel);
            if (output.paneId.empty() || output.seriesId.empty() ||
                !std::isfinite(output.paneHeightWeight) ||
                output.paneHeightWeight <= 0.0f ||
                !std::isfinite(output.width) || output.width <= 0.0f)
            {
                error = "지표 출력 설정에 잘못된 pane/series/크기 값이 있습니다.";
                return false;
            }
            return true;
        }

        bool ParseReference(
            const json_lite::Value& value,
            const indicators::IndicatorSpec& spec,
            const IndicatorReferenceBinding* fallback,
            IndicatorReferenceBinding& reference,
            std::string& error)
        {
            if (!value.IsObject()) {
                error = "지표 기준선 설정이 JSON 객체가 아닙니다.";
                return false;
            }
            reference = fallback != nullptr
                ? *fallback
                : IndicatorReferenceBinding{};
            reference.indicatorId = spec.id;
            reference.paneId = StringValue(value, "pane_id", reference.paneId);
            reference.paneTitle = StringValue(
                value,
                "pane_title",
                reference.paneTitle);
            reference.paneHeightWeight = static_cast<float>(NumberValue(
                value,
                "pane_height_weight",
                reference.paneHeightWeight));
            reference.paneValueScale = ParsePaneScale(IntegerValue(
                value,
                "pane_value_scale",
                static_cast<int>(reference.paneValueScale)));
            reference.fixedMinimum = NumberValue(
                value,
                "fixed_minimum",
                reference.fixedMinimum);
            reference.fixedMaximum = NumberValue(
                value,
                "fixed_maximum",
                reference.fixedMaximum);
            reference.cursorGrid = ParseValueGrid(
                Find(value, "cursor_grid"),
                reference.cursorGrid);
            reference.valueDecimals = IntegerValue(
                value,
                "value_decimals",
                reference.valueDecimals);
            reference.referenceId = StringValue(
                value,
                "reference_id",
                reference.referenceId);
            reference.label = StringValue(value, "label", reference.label);
            reference.value = NumberValue(value, "value", reference.value);
            reference.color = ParseColor(Find(value, "color"), reference.color);
            reference.width = static_cast<float>(NumberValue(
                value,
                "width",
                reference.width));
            reference.style = ParseLineStyle(IntegerValue(
                value,
                "style",
                static_cast<int>(reference.style)));
            reference.visible = BooleanValue(
                value,
                "visible",
                reference.visible);
            if (reference.paneId.empty() || reference.referenceId.empty() ||
                !std::isfinite(reference.paneHeightWeight) ||
                reference.paneHeightWeight <= 0.0f ||
                !std::isfinite(reference.value) ||
                !std::isfinite(reference.width) || reference.width <= 0.0f)
            {
                error = "지표 기준선 설정에 잘못된 pane/id/값이 있습니다.";
                return false;
            }
            return true;
        }

        bool ParseStateText(
            const std::string& text,
            ChartWorkspacePersistenceState& state,
            std::string& error)
        {
            const json_lite::ParseResult parsed = json_lite::Parse(text);
            if (!parsed.ok || !parsed.value.IsObject()) {
                error = parsed.ok
                    ? "작업공간 JSON 루트가 객체가 아닙니다."
                    : "작업공간 JSON 파싱 실패: " + parsed.error;
                return false;
            }
            return ParseChartWorkspaceState(text, state, error);
        }
    }

    bool SerializeChartWorkspaceState(
        const ChartWorkspacePersistenceState& state,
        std::string& json,
        std::string& error)
    {
        IndicatorRenderPlan plan;
        if (!BuildIndicatorRenderPlan(state.indicators, plan, error)) {
            return false;
        }

        std::ostringstream stream;
        stream << std::setprecision(17);
        stream << "{\"schema_version\":" << SchemaVersion;
        stream << ",\"indicators\":[";
        for (std::size_t index = 0; index < state.indicators.size(); ++index) {
            if (index > 0U) stream << ',';
            const IndicatorInstanceDefinition& definition =
                state.indicators[index];
            stream << '{';
            stream << "\"id\":"; AppendQuoted(stream, definition.spec.id);
            stream << ",\"type\":"; AppendQuoted(stream, definition.spec.type);
            stream << ",\"visible\":"
                   << (definition.visible ? "true" : "false");
            stream << ",\"parameters\":{";
            bool firstParameter = true;
            for (const auto& parameter : definition.spec.parameters) {
                if (!firstParameter) stream << ',';
                firstParameter = false;
                AppendQuoted(stream, parameter.first);
                stream << ':' << parameter.second;
            }
            stream << "},\"outputs\":[";
            for (std::size_t outputIndex = 0;
                 outputIndex < definition.outputs.size();
                 ++outputIndex)
            {
                if (outputIndex > 0U) stream << ',';
                AppendOutput(stream, definition.outputs[outputIndex]);
            }
            stream << "],\"references\":[";
            for (std::size_t referenceIndex = 0;
                 referenceIndex < definition.references.size();
                 ++referenceIndex)
            {
                if (referenceIndex > 0U) stream << ',';
                AppendReference(stream, definition.references[referenceIndex]);
            }
            stream << "]}";
        }
        stream << "],\"pane_height_weights\":{";
        bool firstPane = true;
        for (const auto& pane : state.paneHeightWeights) {
            if (pane.first.empty() || !std::isfinite(pane.second) ||
                pane.second <= 0.0f)
            {
                continue;
            }
            if (!firstPane) stream << ',';
            firstPane = false;
            AppendQuoted(stream, pane.first);
            stream << ':' << pane.second;
        }
        stream << "}}";

        json = stream.str();
        error.clear();
        return true;
    }

    bool ParseChartWorkspaceState(
        const std::string& json,
        ChartWorkspacePersistenceState& state,
        std::string& error)
    {
        const json_lite::ParseResult parsed = json_lite::Parse(json);
        if (!parsed.ok || !parsed.value.IsObject()) {
            error = parsed.ok
                ? "작업공간 JSON 루트가 객체가 아닙니다."
                : "작업공간 JSON 파싱 실패: " + parsed.error;
            return false;
        }
        const int schemaVersion = IntegerValue(
            parsed.value,
            "schema_version",
            0);
        if (schemaVersion != SchemaVersion) {
            error = "지원하지 않는 작업공간 JSON 버전입니다.";
            return false;
        }

        const json_lite::Value* indicatorsValue =
            Find(parsed.value, "indicators");
        if (indicatorsValue == nullptr || !indicatorsValue->IsArray()) {
            error = "작업공간 JSON에 indicators 배열이 없습니다.";
            return false;
        }

        ChartWorkspacePersistenceState candidate;
        for (const json_lite::Value& item : indicatorsValue->AsArray()) {
            if (!item.IsObject()) {
                error = "지표 설정 항목이 객체가 아닙니다.";
                return false;
            }
            indicators::IndicatorSpec spec;
            spec.id = StringValue(item, "id");
            spec.type = StringValue(item, "type");
            const json_lite::Value* parameters = Find(item, "parameters");
            if (parameters != nullptr && parameters->IsObject()) {
                for (const auto& parameter : parameters->AsObject()) {
                    if (parameter.second.IsNumber()) {
                        spec.parameters[parameter.first] =
                            parameter.second.AsNumber();
                    }
                }
            }
            if (spec.id.empty() || spec.type.empty()) {
                error = "지표 id 또는 type이 비어 있습니다.";
                return false;
            }

            IndicatorInstanceDefinition definition;
            if (!CreateIndicatorDefinition(spec, definition, error)) {
                error = "저장된 지표 복원 실패 " + spec.id + ": " + error;
                return false;
            }
            definition.visible = BooleanValue(
                item,
                "visible",
                definition.visible);

            const json_lite::Value* outputs = Find(item, "outputs");
            if (outputs != nullptr && outputs->IsArray()) {
                std::vector<IndicatorOutputBinding> restored;
                restored.reserve(outputs->AsArray().size());
                for (std::size_t index = 0;
                     index < outputs->AsArray().size();
                     ++index)
                {
                    const IndicatorOutputBinding* fallback =
                        index < definition.outputs.size()
                            ? &definition.outputs[index]
                            : nullptr;
                    IndicatorOutputBinding output;
                    if (!ParseOutput(
                            outputs->AsArray()[index],
                            spec,
                            fallback,
                            output,
                            error))
                    {
                        return false;
                    }
                    restored.push_back(std::move(output));
                }
                definition.outputs = std::move(restored);
            }

            const json_lite::Value* references = Find(item, "references");
            if (references != nullptr && references->IsArray()) {
                std::vector<IndicatorReferenceBinding> restored;
                restored.reserve(references->AsArray().size());
                for (std::size_t index = 0;
                     index < references->AsArray().size();
                     ++index)
                {
                    const IndicatorReferenceBinding* fallback =
                        index < definition.references.size()
                            ? &definition.references[index]
                            : nullptr;
                    IndicatorReferenceBinding reference;
                    if (!ParseReference(
                            references->AsArray()[index],
                            spec,
                            fallback,
                            reference,
                            error))
                    {
                        return false;
                    }
                    restored.push_back(std::move(reference));
                }
                definition.references = std::move(restored);
            }
            candidate.indicators.push_back(std::move(definition));
        }

        const json_lite::Value* paneWeights =
            Find(parsed.value, "pane_height_weights");
        if (paneWeights != nullptr && paneWeights->IsObject()) {
            for (const auto& item : paneWeights->AsObject()) {
                if (!item.second.IsNumber()) continue;
                const double weight = item.second.AsNumber();
                if (!item.first.empty() && std::isfinite(weight) &&
                    weight > 0.0 && weight <= 100.0)
                {
                    candidate.paneHeightWeights[item.first] =
                        static_cast<float>(weight);
                }
            }
        }

        IndicatorRenderPlan plan;
        if (!BuildIndicatorRenderPlan(candidate.indicators, plan, error)) {
            error = "저장된 지표 렌더 계획 검증 실패: " + error;
            return false;
        }

        state = std::move(candidate);
        error.clear();
        return true;
    }

    bool LoadChartWorkspaceState(
        const std::string& path,
        ChartWorkspacePersistenceState& state,
        bool& found,
        std::string& error)
    {
        found = false;
        const std::filesystem::path target(path);
        const std::filesystem::path backup = target.string() + ".bak";
        std::error_code existsError;
        const bool targetExists = std::filesystem::exists(target, existsError);
        const bool backupExists = std::filesystem::exists(backup, existsError);
        if (!targetExists && !backupExists) {
            error.clear();
            return true;
        }

        std::string text;
        std::string firstError;
        if (targetExists && ReadTextFile(target, text, firstError)) {
            ChartWorkspacePersistenceState candidate;
            if (ParseStateText(text, candidate, firstError)) {
                state = std::move(candidate);
                found = true;
                error.clear();
                return true;
            }
        }

        if (backupExists) {
            std::string backupText;
            std::string backupError;
            if (ReadTextFile(backup, backupText, backupError)) {
                ChartWorkspacePersistenceState candidate;
                if (ParseStateText(backupText, candidate, backupError)) {
                    state = std::move(candidate);
                    found = true;
                    error.clear();
                    return true;
                }
            }
        }

        error = !firstError.empty()
            ? firstError
            : "작업공간 JSON을 읽거나 해석하지 못했습니다.";
        return false;
    }

    bool SaveChartWorkspaceState(
        const std::string& path,
        const ChartWorkspacePersistenceState& state,
        std::string& error)
    {
        std::string json;
        if (!SerializeChartWorkspaceState(state, json, error)) return false;

        const std::filesystem::path target(path);
        const std::filesystem::path temporary = target.string() + ".tmp";
        const std::filesystem::path backup = target.string() + ".bak";
        std::error_code fileError;
        if (!target.parent_path().empty()) {
            std::filesystem::create_directories(
                target.parent_path(),
                fileError);
            if (fileError) {
                error = "작업공간 폴더 생성 실패: " + fileError.message();
                return false;
            }
        }

        {
            std::ofstream output(
                temporary,
                std::ios::binary | std::ios::trunc);
            if (!output) {
                error = "작업공간 임시 파일을 만들지 못했습니다.";
                return false;
            }
            output.write(json.data(), static_cast<std::streamsize>(json.size()));
            output.flush();
            if (!output) {
                error = "작업공간 임시 파일 저장에 실패했습니다.";
                return false;
            }
        }

        std::filesystem::remove(backup, fileError);
        fileError.clear();
        const bool hadTarget = std::filesystem::exists(target, fileError);
        if (hadTarget) {
            fileError.clear();
            std::filesystem::rename(target, backup, fileError);
            if (fileError) {
                std::filesystem::remove(temporary, fileError);
                error = "기존 작업공간 백업 실패: " + fileError.message();
                return false;
            }
        }

        fileError.clear();
        std::filesystem::rename(temporary, target, fileError);
        if (fileError) {
            if (hadTarget) {
                std::error_code restoreError;
                std::filesystem::rename(backup, target, restoreError);
            }
            error = "작업공간 파일 교체 실패: " + fileError.message();
            return false;
        }

        std::filesystem::remove(backup, fileError);
        error.clear();
        return true;
    }
}
