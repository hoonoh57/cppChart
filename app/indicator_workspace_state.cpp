#include "indicator_workspace_state.h"

#include "../core/json_lite.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace trading::app
{
    namespace
    {
        constexpr int CurrentSchemaVersion = 4;
        constexpr int LegacySchemaVersion = 3;

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

        bool NumberValue(
            const json_lite::Value& object,
            const char* key,
            double& result)
        {
            const json_lite::Value* value = Find(object, key);
            if (value == nullptr || !value->IsNumber()) return false;
            result = value->AsNumber();
            return std::isfinite(result);
        }

        bool IntegerValue(
            const json_lite::Value& object,
            const char* key,
            int minimum,
            int maximum,
            int& result)
        {
            double numeric = 0.0;
            if (!NumberValue(object, key, numeric) ||
                std::floor(numeric) != numeric ||
                numeric < static_cast<double>(minimum) ||
                numeric > static_cast<double>(maximum))
            {
                return false;
            }
            result = static_cast<int>(numeric);
            return true;
        }

        bool SizeValue(
            const json_lite::Value& object,
            const char* key,
            std::size_t& result)
        {
            double numeric = 0.0;
            if (!NumberValue(object, key, numeric) ||
                std::floor(numeric) != numeric ||
                numeric < 0.0 ||
                numeric > static_cast<double>(
                    (std::numeric_limits<std::uint32_t>::max)()))
            {
                return false;
            }
            result = static_cast<std::size_t>(numeric);
            return true;
        }

        bool LevelVisible(
            const json_lite::Value& object,
            bool fallback)
        {
            const json_lite::Value* level = Find(object, "level");
            if (level != nullptr && level->IsString()) {
                const std::string value = level->AsString();
                if (value == "Visible" || value == "Active") return true;
                if (value == "Off" || value == "Standby") return false;
            }
            return BooleanValue(object, "visible", fallback);
        }

        void AppendJsonString(
            std::ostringstream& stream,
            const std::string& value)
        {
            stream << json_lite::EscapeString(value);
        }

        const char* DefaultIdForType(const std::string& type) noexcept
        {
            if (type == "SMA") return "sma.20";
            if (type == "EMA") return "ema.20";
            if (type == "BOLLINGER") return "bollinger.20.2";
            if (type == "RSI") return "rsi.14";
            if (type == "MACD") return "macd.12.26.9";
            if (type == "DMI") return "dmi.14";
            if (type == "SUPERTREND") return "supertrend.14.2";
            if (type == "JMA") return "jma.20";
            if (type == "VWAP") return "vwap.session";
            if (type == "OBV") return "obv.20";
            if (type == "ADX") return "adx.14";
            return "indicator.default";
        }

        const char* RenderKindName(IndicatorRenderKind kind) noexcept
        {
            return kind == IndicatorRenderKind::Histogram
                ? "Histogram"
                : "Line";
        }

        bool ParseRenderKind(
            const std::string& value,
            IndicatorRenderKind& kind)
        {
            if (value == "Line") {
                kind = IndicatorRenderKind::Line;
                return true;
            }
            if (value == "Histogram") {
                kind = IndicatorRenderKind::Histogram;
                return true;
            }
            return false;
        }

        const char* LineStyleName(render::LineStyle style) noexcept
        {
            switch (style) {
            case render::LineStyle::Dashed:
                return "Dashed";
            case render::LineStyle::Dotted:
                return "Dotted";
            default:
                return "Solid";
            }
        }

        bool ParseLineStyle(
            const std::string& value,
            render::LineStyle& style)
        {
            if (value == "Solid") {
                style = render::LineStyle::Solid;
                return true;
            }
            if (value == "Dashed") {
                style = render::LineStyle::Dashed;
                return true;
            }
            if (value == "Dotted") {
                style = render::LineStyle::Dotted;
                return true;
            }
            return false;
        }

        const char* PaneScaleName(render::PaneValueScale scale) noexcept
        {
            switch (scale) {
            case render::PaneValueScale::Fixed:
                return "Fixed";
            case render::PaneValueScale::Symmetric:
                return "Symmetric";
            default:
                return "Auto";
            }
        }

        bool ParsePaneScale(
            const std::string& value,
            render::PaneValueScale& scale)
        {
            if (value == "Auto") {
                scale = render::PaneValueScale::Auto;
                return true;
            }
            if (value == "Fixed") {
                scale = render::PaneValueScale::Fixed;
                return true;
            }
            if (value == "Symmetric") {
                scale = render::PaneValueScale::Symmetric;
                return true;
            }
            return false;
        }

        void AppendColor(
            std::ostringstream& stream,
            const render::ColorRgba& color)
        {
            stream << '['
                << static_cast<unsigned int>(color.red) << ','
                << static_cast<unsigned int>(color.green) << ','
                << static_cast<unsigned int>(color.blue) << ','
                << static_cast<unsigned int>(color.alpha) << ']';
        }

        bool ParseColor(
            const json_lite::Value* value,
            render::ColorRgba& color)
        {
            if (value == nullptr || !value->IsArray() ||
                value->AsArray().size() != 4U)
            {
                return false;
            }

            std::uint8_t channels[4]{};
            for (std::size_t index = 0; index < 4U; ++index) {
                const json_lite::Value& component = value->AsArray()[index];
                if (!component.IsNumber()) return false;
                const double numeric = component.AsNumber();
                if (!std::isfinite(numeric) || std::floor(numeric) != numeric ||
                    numeric < 0.0 || numeric > 255.0)
                {
                    return false;
                }
                channels[index] = static_cast<std::uint8_t>(numeric);
            }
            color = { channels[0], channels[1], channels[2], channels[3] };
            return true;
        }

        void AppendValueGrid(
            std::ostringstream& stream,
            const render::ValueGrid& grid)
        {
            stream << "{\"enabled\":" << (grid.enabled ? "true" : "false")
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

        bool ParseValueGrid(
            const json_lite::Value* value,
            render::ValueGrid& grid,
            std::string& error)
        {
            if (value == nullptr || !value->IsObject()) {
                error = "cursor_grid 객체가 없습니다.";
                return false;
            }

            render::ValueGrid candidate;
            candidate.enabled = BooleanValue(*value, "enabled", false);
            if (!NumberValue(*value, "fallback_step", candidate.fallbackStep)) {
                error = "cursor_grid fallback_step이 유효하지 않습니다.";
                return false;
            }

            const json_lite::Value* bands = Find(*value, "bands");
            if (bands == nullptr || !bands->IsArray()) {
                error = "cursor_grid bands 배열이 없습니다.";
                return false;
            }
            for (const json_lite::Value& item : bands->AsArray()) {
                if (!item.IsObject()) {
                    error = "cursor_grid band가 객체가 아닙니다.";
                    return false;
                }
                render::ValueGridBand band;
                if (!NumberValue(item, "upper_exclusive", band.upperExclusive) ||
                    !NumberValue(item, "step", band.step))
                {
                    error = "cursor_grid band 값이 유효하지 않습니다.";
                    return false;
                }
                candidate.bands.push_back(band);
            }

            const char* gridError = nullptr;
            if (!render::ValidateValueGrid(candidate, &gridError)) {
                error = std::string("cursor_grid 검증 실패: ") +
                    (gridError != nullptr ? gridError : "unknown");
                return false;
            }
            grid = std::move(candidate);
            error.clear();
            return true;
        }

        bool ReadText(
            const std::filesystem::path& path,
            std::string& text,
            std::string& error)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                error = "지표 JSON 파일을 열지 못했습니다: " + path.string();
                return false;
            }
            std::ostringstream buffer;
            buffer << input.rdbuf();
            if (!input.good() && !input.eof()) {
                error = "지표 JSON 파일을 읽지 못했습니다: " + path.string();
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

        bool WriteAtomic(
            const std::filesystem::path& target,
            const std::string& text,
            std::string& error)
        {
            const std::filesystem::path temporary = target.string() + ".tmp";
            std::error_code fileError;
            if (!target.parent_path().empty()) {
                std::filesystem::create_directories(
                    target.parent_path(),
                    fileError);
                if (fileError) {
                    error = "지표 JSON 폴더 생성 실패: " + fileError.message();
                    return false;
                }
            }

            {
                std::ofstream output(
                    temporary,
                    std::ios::binary | std::ios::trunc);
                if (!output) {
                    error = "지표 JSON 임시 파일을 만들지 못했습니다.";
                    return false;
                }
                output.write(
                    text.data(),
                    static_cast<std::streamsize>(text.size()));
                output.flush();
                if (!output) {
                    error = "지표 JSON 저장에 실패했습니다.";
                    return false;
                }
            }

            std::filesystem::remove(target, fileError);
            fileError.clear();
            std::filesystem::rename(temporary, target, fileError);
            if (fileError) {
                std::filesystem::remove(temporary, fileError);
                error = "지표 JSON 교체 실패: " + fileError.message();
                return false;
            }
            error.clear();
            return true;
        }

        IndicatorOutputBinding* FindOutput(
            IndicatorInstanceDefinition& definition,
            const std::string& id) noexcept
        {
            for (IndicatorOutputBinding& output : definition.outputs) {
                if (output.seriesId == id) return &output;
            }
            return nullptr;
        }

        IndicatorReferenceBinding* FindReference(
            IndicatorInstanceDefinition& definition,
            const std::string& id) noexcept
        {
            for (IndicatorReferenceBinding& reference : definition.references) {
                if (reference.referenceId == id) return &reference;
            }
            return nullptr;
        }

        void ApplyLegacyOutputStates(
            const json_lite::Value* values,
            IndicatorInstanceDefinition& definition)
        {
            if (values == nullptr || !values->IsArray()) return;
            for (const json_lite::Value& item : values->AsArray()) {
                if (!item.IsObject()) continue;
                std::string id = StringValue(item, "id");
                if (id.empty()) id = StringValue(item, "series_id");
                IndicatorOutputBinding* output = FindOutput(definition, id);
                if (output != nullptr) {
                    output->visible = LevelVisible(item, output->visible);
                }
            }
        }

        void ApplyLegacyReferenceStates(
            const json_lite::Value* values,
            IndicatorInstanceDefinition& definition)
        {
            if (values == nullptr || !values->IsArray()) return;
            for (const json_lite::Value& item : values->AsArray()) {
                if (!item.IsObject()) continue;
                std::string id = StringValue(item, "id");
                if (id.empty()) id = StringValue(item, "reference_id");
                IndicatorReferenceBinding* reference =
                    FindReference(definition, id);
                if (reference != nullptr) {
                    reference->visible = LevelVisible(item, reference->visible);
                }
            }
        }

        bool ParseOutputV4(
            const json_lite::Value& item,
            const std::string& indicatorId,
            IndicatorOutputBinding& output,
            std::string& error)
        {
            if (!item.IsObject()) {
                error = "출력 정의가 객체가 아닙니다.";
                return false;
            }

            IndicatorOutputBinding candidate;
            candidate.indicatorId = StringValue(item, "indicator_id");
            candidate.seriesId = StringValue(item, "id");
            candidate.paneId = StringValue(item, "pane_id");
            candidate.paneTitle = StringValue(item, "pane_title");
            candidate.label = StringValue(item, "label");
            candidate.legendRole = StringValue(item, "legend_role");
            candidate.legendLabel = StringValue(item, "legend_label");

            if (candidate.indicatorId != indicatorId ||
                candidate.seriesId.empty() || candidate.paneId.empty() ||
                candidate.label.empty())
            {
                error = "출력 정의의 식별자 또는 패널/라벨이 유효하지 않습니다.";
                return false;
            }
            if (!SizeValue(item, "output_index", candidate.outputIndex) ||
                !ParseRenderKind(StringValue(item, "kind"), candidate.kind) ||
                !ParsePaneScale(
                    StringValue(item, "pane_value_scale"),
                    candidate.paneValueScale))
            {
                error = "출력 정의의 index/kind/축 방식이 유효하지 않습니다.";
                return false;
            }

            double paneHeight = 0.0;
            double width = 0.0;
            if (!NumberValue(item, "pane_height_weight", paneHeight) ||
                paneHeight <= 0.0 || paneHeight > 100.0 ||
                !NumberValue(item, "fixed_minimum", candidate.fixedMinimum) ||
                !NumberValue(item, "fixed_maximum", candidate.fixedMaximum) ||
                !IntegerValue(item, "value_decimals", 0, 8, candidate.valueDecimals) ||
                !NumberValue(item, "width", width) ||
                width <= 0.0 || width > 32.0)
            {
                error = "출력 정의의 패널/축/두께 값이 유효하지 않습니다.";
                return false;
            }
            candidate.paneHeightWeight = static_cast<float>(paneHeight);
            candidate.width = static_cast<float>(width);

            if (!ParseValueGrid(
                    Find(item, "cursor_grid"),
                    candidate.cursorGrid,
                    error) ||
                !ParseColor(Find(item, "primary_color"), candidate.primaryColor) ||
                !ParseColor(Find(item, "secondary_color"), candidate.secondaryColor) ||
                !ParseLineStyle(StringValue(item, "style"), candidate.style))
            {
                if (error.empty()) {
                    error = "출력 정의의 그리드/색상/선종류가 유효하지 않습니다.";
                }
                return false;
            }
            candidate.visible = LevelVisible(item, true);
            output = std::move(candidate);
            error.clear();
            return true;
        }

        bool ParseReferenceV4(
            const json_lite::Value& item,
            const std::string& indicatorId,
            IndicatorReferenceBinding& reference,
            std::string& error)
        {
            if (!item.IsObject()) {
                error = "기준선 정의가 객체가 아닙니다.";
                return false;
            }

            IndicatorReferenceBinding candidate;
            candidate.indicatorId = StringValue(item, "indicator_id");
            candidate.referenceId = StringValue(item, "id");
            candidate.paneId = StringValue(item, "pane_id");
            candidate.paneTitle = StringValue(item, "pane_title");
            candidate.label = StringValue(item, "label");
            if (candidate.indicatorId != indicatorId ||
                candidate.referenceId.empty() || candidate.paneId.empty() ||
                candidate.label.empty())
            {
                error = "기준선 정의의 식별자 또는 패널/라벨이 유효하지 않습니다.";
                return false;
            }
            if (!ParsePaneScale(
                    StringValue(item, "pane_value_scale"),
                    candidate.paneValueScale))
            {
                error = "기준선 정의의 축 방식이 유효하지 않습니다.";
                return false;
            }

            double paneHeight = 0.0;
            double width = 0.0;
            if (!NumberValue(item, "pane_height_weight", paneHeight) ||
                paneHeight <= 0.0 || paneHeight > 100.0 ||
                !NumberValue(item, "fixed_minimum", candidate.fixedMinimum) ||
                !NumberValue(item, "fixed_maximum", candidate.fixedMaximum) ||
                !IntegerValue(item, "value_decimals", 0, 8, candidate.valueDecimals) ||
                !NumberValue(item, "value", candidate.value) ||
                !NumberValue(item, "width", width) ||
                width <= 0.0 || width > 32.0)
            {
                error = "기준선 정의의 패널/축/값/두께가 유효하지 않습니다.";
                return false;
            }
            candidate.paneHeightWeight = static_cast<float>(paneHeight);
            candidate.width = static_cast<float>(width);

            if (!ParseValueGrid(
                    Find(item, "cursor_grid"),
                    candidate.cursorGrid,
                    error) ||
                !ParseColor(Find(item, "color"), candidate.color) ||
                !ParseLineStyle(StringValue(item, "style"), candidate.style))
            {
                if (error.empty()) {
                    error = "기준선 정의의 그리드/색상/선종류가 유효하지 않습니다.";
                }
                return false;
            }
            candidate.visible = LevelVisible(item, true);
            reference = std::move(candidate);
            error.clear();
            return true;
        }

        bool ApplyV4Bindings(
            const json_lite::Value& item,
            IndicatorInstanceDefinition& definition,
            std::string& error)
        {
            const json_lite::Value* outputs = Find(item, "outputs");
            const json_lite::Value* references = Find(item, "references");
            if (outputs == nullptr || !outputs->IsArray() ||
                references == nullptr || !references->IsArray())
            {
                error = "schema 4 지표에 outputs/references 배열이 없습니다.";
                return false;
            }

            std::vector<IndicatorOutputBinding> parsedOutputs;
            std::set<std::string> outputIds;
            for (const json_lite::Value& outputValue : outputs->AsArray()) {
                IndicatorOutputBinding output;
                if (!ParseOutputV4(
                        outputValue,
                        definition.spec.id,
                        output,
                        error))
                {
                    return false;
                }
                if (!outputIds.insert(output.seriesId).second) {
                    error = "중복 출력 id: " + output.seriesId;
                    return false;
                }
                parsedOutputs.push_back(std::move(output));
            }

            std::vector<IndicatorReferenceBinding> parsedReferences;
            std::set<std::string> referenceIds;
            for (const json_lite::Value& referenceValue : references->AsArray()) {
                IndicatorReferenceBinding reference;
                if (!ParseReferenceV4(
                        referenceValue,
                        definition.spec.id,
                        reference,
                        error))
                {
                    return false;
                }
                if (!referenceIds.insert(reference.referenceId).second) {
                    error = "중복 기준선 id: " + reference.referenceId;
                    return false;
                }
                parsedReferences.push_back(std::move(reference));
            }

            definition.outputs = std::move(parsedOutputs);
            definition.references = std::move(parsedReferences);
            error.clear();
            return true;
        }

        void AppendOutputV4(
            std::ostringstream& stream,
            const IndicatorOutputBinding& output)
        {
            stream << "{\"id\":";
            AppendJsonString(stream, output.seriesId);
            stream << ",\"indicator_id\":";
            AppendJsonString(stream, output.indicatorId);
            stream << ",\"output_index\":" << output.outputIndex;
            stream << ",\"kind\":";
            AppendJsonString(stream, RenderKindName(output.kind));
            stream << ",\"pane_id\":";
            AppendJsonString(stream, output.paneId);
            stream << ",\"pane_title\":";
            AppendJsonString(stream, output.paneTitle);
            stream << ",\"pane_height_weight\":" << output.paneHeightWeight;
            stream << ",\"pane_value_scale\":";
            AppendJsonString(stream, PaneScaleName(output.paneValueScale));
            stream << ",\"fixed_minimum\":" << output.fixedMinimum;
            stream << ",\"fixed_maximum\":" << output.fixedMaximum;
            stream << ",\"cursor_grid\":";
            AppendValueGrid(stream, output.cursorGrid);
            stream << ",\"value_decimals\":" << output.valueDecimals;
            stream << ",\"label\":";
            AppendJsonString(stream, output.label);
            stream << ",\"primary_color\":";
            AppendColor(stream, output.primaryColor);
            stream << ",\"secondary_color\":";
            AppendColor(stream, output.secondaryColor);
            stream << ",\"width\":" << output.width;
            stream << ",\"style\":";
            AppendJsonString(stream, LineStyleName(output.style));
            stream << ",\"level\":";
            AppendJsonString(stream, output.visible ? "Visible" : "Off");
            stream << ",\"legend_role\":";
            AppendJsonString(stream, output.legendRole);
            stream << ",\"legend_label\":";
            AppendJsonString(stream, output.legendLabel);
            stream << '}';
        }

        void AppendReferenceV4(
            std::ostringstream& stream,
            const IndicatorReferenceBinding& reference)
        {
            stream << "{\"id\":";
            AppendJsonString(stream, reference.referenceId);
            stream << ",\"indicator_id\":";
            AppendJsonString(stream, reference.indicatorId);
            stream << ",\"pane_id\":";
            AppendJsonString(stream, reference.paneId);
            stream << ",\"pane_title\":";
            AppendJsonString(stream, reference.paneTitle);
            stream << ",\"pane_height_weight\":"
                << reference.paneHeightWeight;
            stream << ",\"pane_value_scale\":";
            AppendJsonString(stream, PaneScaleName(reference.paneValueScale));
            stream << ",\"fixed_minimum\":" << reference.fixedMinimum;
            stream << ",\"fixed_maximum\":" << reference.fixedMaximum;
            stream << ",\"cursor_grid\":";
            AppendValueGrid(stream, reference.cursorGrid);
            stream << ",\"value_decimals\":" << reference.valueDecimals;
            stream << ",\"label\":";
            AppendJsonString(stream, reference.label);
            stream << ",\"value\":" << reference.value;
            stream << ",\"color\":";
            AppendColor(stream, reference.color);
            stream << ",\"width\":" << reference.width;
            stream << ",\"style\":";
            AppendJsonString(stream, LineStyleName(reference.style));
            stream << ",\"level\":";
            AppendJsonString(stream, reference.visible ? "Visible" : "Off");
            stream << '}';
        }

        bool ParseState(
            const std::string& text,
            IndicatorWorkspaceState& state,
            std::string& error)
        {
            const json_lite::ParseResult parsed = json_lite::Parse(text);
            if (!parsed.ok || !parsed.value.IsObject()) {
                std::ostringstream message;
                message << (parsed.ok
                    ? "지표 JSON 루트가 객체가 아닙니다."
                    : "지표 JSON 파싱 실패: " + parsed.error);
                if (!parsed.ok) message << " at byte " << parsed.errorOffset;
                error = message.str();
                return false;
            }

            const json_lite::Value* schema = Find(parsed.value, "schema_version");
            if (schema == nullptr || !schema->IsNumber()) {
                error = "지표 JSON schema_version이 없습니다.";
                return false;
            }
            const int schemaVersion = schema->AsInt(0);
            if (schemaVersion != LegacySchemaVersion &&
                schemaVersion != CurrentSchemaVersion)
            {
                error = "지원하지 않는 지표 JSON schema_version입니다.";
                return false;
            }

            const json_lite::Value* indicatorsValue =
                Find(parsed.value, "indicators");
            if (indicatorsValue == nullptr || !indicatorsValue->IsArray()) {
                error = "지표 JSON에 indicators 배열이 없습니다.";
                return false;
            }

            IndicatorWorkspaceState candidate;
            std::set<std::string> ids;
            for (const json_lite::Value& item : indicatorsValue->AsArray()) {
                if (!item.IsObject()) {
                    error = "지표 JSON 항목이 객체가 아닙니다.";
                    return false;
                }

                indicators::IndicatorSpec spec;
                spec.id = StringValue(item, "id");
                spec.type = StringValue(item, "type");
                if (spec.id.empty() || spec.type.empty()) {
                    error = "지표 JSON의 id 또는 type이 비어 있습니다.";
                    return false;
                }
                if (!ids.insert(spec.id).second) {
                    error = "중복 지표 id: " + spec.id;
                    return false;
                }

                const json_lite::Value* parameters = Find(item, "parameters");
                if (parameters != nullptr && parameters->IsObject()) {
                    for (const auto& parameter : parameters->AsObject()) {
                        if (!parameter.second.IsNumber()) continue;
                        const double value = parameter.second.AsNumber();
                        if (std::isfinite(value)) {
                            spec.parameters[parameter.first] = value;
                        }
                    }
                }

                IndicatorInstanceDefinition definition;
                if (!CreateIndicatorDefinition(spec, definition, error)) {
                    error = "지표 JSON 정의 생성 실패 " + spec.id + ": " + error;
                    return false;
                }
                definition.visible = LevelVisible(item, false);
                if (schemaVersion == LegacySchemaVersion) {
                    ApplyLegacyOutputStates(Find(item, "outputs"), definition);
                    ApplyLegacyReferenceStates(
                        Find(item, "references"),
                        definition);
                }
                else if (!ApplyV4Bindings(item, definition, error)) {
                    error = "지표 JSON 속성 복원 실패 " + spec.id + ": " + error;
                    return false;
                }
                candidate.indicators.push_back(std::move(definition));
            }

            if (!NormalizeIndicatorWorkspaceDefinitions(
                    candidate.indicators,
                    error))
            {
                return false;
            }

            const json_lite::Value* paneWeights =
                Find(parsed.value, "pane_height_weights");
            if (paneWeights != nullptr && paneWeights->IsObject()) {
                for (const auto& pane : paneWeights->AsObject()) {
                    if (!pane.second.IsNumber()) continue;
                    const double value = pane.second.AsNumber();
                    if (!pane.first.empty() && std::isfinite(value) &&
                        value > 0.0 && value <= 100.0)
                    {
                        candidate.paneHeightWeights[pane.first] =
                            static_cast<float>(value);
                    }
                }
            }

            state = std::move(candidate);
            error.clear();
            return true;
        }

        bool LoadOne(
            const std::string& path,
            IndicatorWorkspaceState& state,
            std::string& error)
        {
            std::string text;
            if (!ReadText(std::filesystem::path(path), text, error)) return false;
            return ParseState(text, state, error);
        }

        bool Exists(const std::string& path)
        {
            std::error_code error;
            return !path.empty() &&
                std::filesystem::exists(std::filesystem::path(path), error);
        }
    }

    bool NormalizeIndicatorWorkspaceDefinitions(
        std::vector<IndicatorInstanceDefinition>& definitions,
        std::string& error)
    {
        std::set<std::string> ids;
        for (const IndicatorInstanceDefinition& definition : definitions) {
            if (definition.spec.id.empty() || definition.spec.type.empty()) {
                error = "지표 id 또는 type이 비어 있습니다.";
                return false;
            }
            if (!ids.insert(definition.spec.id).second) {
                error = "중복 지표 id: " + definition.spec.id;
                return false;
            }
        }

        for (const IndicatorCatalogEntry& entry : IndicatorCatalog()) {
            const std::string defaultId = DefaultIdForType(entry.type);
            if (ids.find(defaultId) != ids.end()) continue;
            IndicatorInstanceDefinition definition;
            std::string createError;
            if (!CreateDefaultIndicatorDefinition(
                    entry.type,
                    defaultId,
                    definition,
                    createError))
            {
                error = "기본 지표 정의 생성 실패 " + entry.type + ": " +
                    createError;
                return false;
            }
            definition.visible = false;
            definitions.push_back(std::move(definition));
            ids.insert(defaultId);
        }

        IndicatorRenderPlan plan;
        if (!BuildIndicatorRenderPlan(definitions, plan, error)) {
            error = "지표 JSON 렌더 계획 검증 실패: " + error;
            return false;
        }
        error.clear();
        return true;
    }

    bool SerializeIndicatorWorkspaceState(
        const IndicatorWorkspaceState& state,
        std::string& json,
        std::string& error)
    {
        std::vector<IndicatorInstanceDefinition> definitions = state.indicators;
        if (!NormalizeIndicatorWorkspaceDefinitions(definitions, error)) {
            return false;
        }

        std::ostringstream stream;
        stream << std::setprecision(17);
        stream << "{\n  \"schema_version\": " << CurrentSchemaVersion;
        stream << ",\n  \"indicators\": [";
        for (std::size_t index = 0; index < definitions.size(); ++index) {
            const IndicatorInstanceDefinition& definition = definitions[index];
            stream << (index == 0U ? "\n" : ",\n");
            stream << "    {\"id\":";
            AppendJsonString(stream, definition.spec.id);
            stream << ",\"type\":";
            AppendJsonString(stream, definition.spec.type);
            stream << ",\"level\":";
            AppendJsonString(stream, definition.visible ? "Visible" : "Off");
            stream << ",\"parameters\":{";
            bool firstParameter = true;
            for (const auto& parameter : definition.spec.parameters) {
                if (!std::isfinite(parameter.second)) continue;
                if (!firstParameter) stream << ',';
                firstParameter = false;
                AppendJsonString(stream, parameter.first);
                stream << ':' << parameter.second;
            }
            stream << "},\"outputs\":[";
            for (std::size_t outputIndex = 0;
                 outputIndex < definition.outputs.size();
                 ++outputIndex)
            {
                if (outputIndex > 0U) stream << ',';
                AppendOutputV4(stream, definition.outputs[outputIndex]);
            }
            stream << "],\"references\":[";
            for (std::size_t referenceIndex = 0;
                 referenceIndex < definition.references.size();
                 ++referenceIndex)
            {
                if (referenceIndex > 0U) stream << ',';
                AppendReferenceV4(
                    stream,
                    definition.references[referenceIndex]);
            }
            stream << "]}";
        }
        stream << "\n  ],\n  \"pane_height_weights\": {";
        bool firstPane = true;
        for (const auto& pane : state.paneHeightWeights) {
            if (pane.first.empty() || !std::isfinite(pane.second) ||
                pane.second <= 0.0f || pane.second > 100.0f)
            {
                continue;
            }
            stream << (firstPane ? "\n" : ",\n");
            firstPane = false;
            stream << "    ";
            AppendJsonString(stream, pane.first);
            stream << ": " << pane.second;
        }
        if (!firstPane) stream << '\n';
        stream << "  }\n}\n";

        const std::string candidate = stream.str();
        IndicatorWorkspaceState roundTrip;
        std::string parseError;
        if (!ParseState(candidate, roundTrip, parseError)) {
            error = "직렬화된 지표 JSON 자체 검증 실패: " + parseError;
            return false;
        }

        json = candidate;
        error.clear();
        return true;
    }

    bool LoadIndicatorWorkspaceState(
        const std::string& savedPath,
        const std::string& defaultPath,
        IndicatorWorkspaceState& state,
        IndicatorWorkspaceSource& source,
        std::string& diagnostic)
    {
        source = IndicatorWorkspaceSource::None;
        diagnostic.clear();

        if (Exists(savedPath)) {
            IndicatorWorkspaceState saved;
            std::string savedError;
            if (LoadOne(savedPath, saved, savedError)) {
                state = std::move(saved);
                source = IndicatorWorkspaceSource::Saved;
                return true;
            }
            diagnostic = "기존 지표 JSON 무시: " + savedError;
        }

        if (Exists(defaultPath)) {
            IndicatorWorkspaceState defaults;
            std::string defaultError;
            if (LoadOne(defaultPath, defaults, defaultError)) {
                state = std::move(defaults);
                source = IndicatorWorkspaceSource::Default;
                return true;
            }
            if (!diagnostic.empty()) diagnostic += " / ";
            diagnostic += "기본 지표 JSON 실패: " + defaultError;
        }

        if (diagnostic.empty()) {
            diagnostic = "schema_version 3 또는 4 지표 JSON 파일을 찾지 못했습니다.";
        }
        return false;
    }

    bool SaveIndicatorWorkspaceState(
        const std::string& path,
        const IndicatorWorkspaceState& state,
        std::string& error)
    {
        std::string json;
        if (!SerializeIndicatorWorkspaceState(state, json, error)) return false;
        return WriteAtomic(std::filesystem::path(path), json, error);
    }

    const char* IndicatorWorkspaceSourceName(
        IndicatorWorkspaceSource source) noexcept
    {
        switch (source) {
        case IndicatorWorkspaceSource::Saved:
            return "직전 저장 JSON";
        case IndicatorWorkspaceSource::Default:
            return "기본 JSON";
        default:
            return "없음";
        }
    }
}
