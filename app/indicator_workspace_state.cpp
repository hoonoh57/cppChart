#include "indicator_workspace_state.h"

#include "../core/json_lite.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace trading::app
{
    namespace
    {
        constexpr int SchemaVersion = 3;

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

        void AppendQuoted(
            std::ostringstream& stream,
            const std::string& value)
        {
            stream << '"' << json_lite::EscapeString(value) << '"';
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

        void ApplyOutputStates(
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

        void ApplyReferenceStates(
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

        bool ParseState(
            const std::string& text,
            IndicatorWorkspaceState& state,
            std::string& error)
        {
            const json_lite::ParseResult parsed = json_lite::Parse(text);
            if (!parsed.ok || !parsed.value.IsObject()) {
                error = parsed.ok
                    ? "지표 JSON 루트가 객체가 아닙니다."
                    : "지표 JSON 파싱 실패: " + parsed.error;
                return false;
            }

            const json_lite::Value* schema =
                Find(parsed.value, "schema_version");
            if (schema == nullptr || !schema->IsNumber() ||
                schema->AsInt(0) != SchemaVersion)
            {
                error = "지표 JSON schema_version이 3이 아닙니다.";
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

                const json_lite::Value* parameters =
                    Find(item, "parameters");
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
                    error = "지표 JSON 정의 생성 실패 " + spec.id + ": " +
                        error;
                    return false;
                }
                definition.visible = LevelVisible(item, false);
                ApplyOutputStates(Find(item, "outputs"), definition);
                ApplyReferenceStates(Find(item, "references"), definition);
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
                    if (!pane.first.empty() &&
                        std::isfinite(value) && value > 0.0 && value <= 100.0)
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
            if (!ReadText(std::filesystem::path(path), text, error)) {
                return false;
            }
            return ParseState(text, state, error);
        }

        bool Exists(const std::string& path)
        {
            std::error_code error;
            return std::filesystem::exists(
                std::filesystem::path(path),
                error);
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
        std::vector<IndicatorInstanceDefinition> definitions =
            state.indicators;
        if (!NormalizeIndicatorWorkspaceDefinitions(definitions, error)) {
            return false;
        }

        std::ostringstream stream;
        stream << std::setprecision(17);
        stream << "{\n  \"schema_version\": " << SchemaVersion;
        stream << ",\n  \"indicators\": [";
        for (std::size_t index = 0; index < definitions.size(); ++index) {
            const IndicatorInstanceDefinition& definition = definitions[index];
            stream << (index == 0U ? "\n" : ",\n");
            stream << "    {\"id\":";
            AppendQuoted(stream, definition.spec.id);
            stream << ",\"type\":";
            AppendQuoted(stream, definition.spec.type);
            stream << ",\"level\":";
            AppendQuoted(stream, definition.visible ? "Visible" : "Off");
            stream << ",\"parameters\":{";
            bool firstParameter = true;
            for (const auto& parameter : definition.spec.parameters) {
                if (!std::isfinite(parameter.second)) continue;
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
                stream << "{\"id\":";
                AppendQuoted(stream, definition.outputs[outputIndex].seriesId);
                stream << ",\"level\":";
                AppendQuoted(
                    stream,
                    definition.outputs[outputIndex].visible
                        ? "Visible" : "Off");
                stream << '}';
            }
            stream << "],\"references\":[";
            for (std::size_t referenceIndex = 0;
                 referenceIndex < definition.references.size();
                 ++referenceIndex)
            {
                if (referenceIndex > 0U) stream << ',';
                stream << "{\"id\":";
                AppendQuoted(
                    stream,
                    definition.references[referenceIndex].referenceId);
                stream << ",\"level\":";
                AppendQuoted(
                    stream,
                    definition.references[referenceIndex].visible
                        ? "Visible" : "Off");
                stream << '}';
            }
            stream << "]}";
        }
        stream << "\n  ],\n  \"pane_height_weights\": {";
        bool firstPane = true;
        for (const auto& pane : state.paneHeightWeights) {
            if (pane.first.empty() ||
                !std::isfinite(pane.second) ||
                pane.second <= 0.0f || pane.second > 100.0f)
            {
                continue;
            }
            stream << (firstPane ? "\n" : ",\n");
            firstPane = false;
            stream << "    ";
            AppendQuoted(stream, pane.first);
            stream << ": " << pane.second;
        }
        if (!firstPane) stream << '\n';
        stream << "  }\n}\n";

        json = stream.str();
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

        if (!savedPath.empty() && Exists(savedPath)) {
            IndicatorWorkspaceState saved;
            std::string savedError;
            if (LoadOne(savedPath, saved, savedError)) {
                state = std::move(saved);
                source = IndicatorWorkspaceSource::Saved;
                return true;
            }
            diagnostic = "기존 지표 JSON 무시: " + savedError;
        }

        if (!defaultPath.empty() && Exists(defaultPath)) {
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
            diagnostic = "schema_version 3 지표 JSON 파일을 찾지 못했습니다.";
        }
        return false;
    }

    bool SaveIndicatorWorkspaceState(
        const std::string& path,
        const IndicatorWorkspaceState& state,
        std::string& error)
    {
        std::string json;
        if (!SerializeIndicatorWorkspaceState(state, json, error)) {
            return false;
        }
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
