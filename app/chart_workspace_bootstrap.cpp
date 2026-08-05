#include "chart_workspace_bootstrap.h"

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
        constexpr int BootstrapSchemaVersion = 1;

        void AppendQuoted(
            std::ostringstream& stream,
            const std::string& value)
        {
            stream << '"' << json_lite::EscapeString(value) << '"';
        }

        const json_lite::Value* Find(
            const json_lite::Value& object,
            const char* key)
        {
            return object.IsObject() ? object.Find(key) : nullptr;
        }

        std::string StringValue(
            const json_lite::Value& object,
            const char* key)
        {
            const json_lite::Value* value = Find(object, key);
            return value != nullptr && value->IsString()
                ? value->AsString()
                : std::string{};
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

        bool ReadFile(
            const std::filesystem::path& path,
            std::string& text,
            std::string& error)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                error = "차트 복원본 파일을 열지 못했습니다: " + path.string();
                return false;
            }
            std::ostringstream buffer;
            buffer << input.rdbuf();
            if (!input.good() && !input.eof()) {
                error = "차트 복원본 파일을 읽지 못했습니다: " + path.string();
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
                    error = "차트 복원본 폴더 생성 실패: " +
                        fileError.message();
                    return false;
                }
            }

            {
                std::ofstream output(
                    temporary,
                    std::ios::binary | std::ios::trunc);
                if (!output) {
                    error = "차트 복원본 임시 파일을 만들지 못했습니다.";
                    return false;
                }
                output.write(
                    text.data(),
                    static_cast<std::streamsize>(text.size()));
                output.flush();
                if (!output) {
                    error = "차트 복원본 저장에 실패했습니다.";
                    return false;
                }
            }

            std::filesystem::remove(target, fileError);
            fileError.clear();
            std::filesystem::rename(temporary, target, fileError);
            if (fileError) {
                std::filesystem::remove(temporary, fileError);
                error = "차트 복원본 교체 실패: " + fileError.message();
                return false;
            }
            error.clear();
            return true;
        }
    }

    bool SaveChartWorkspaceBootstrap(
        const std::string& path,
        const ChartWorkspacePersistenceState& state,
        std::string& error)
    {
        std::ostringstream stream;
        stream << std::setprecision(17);
        stream << "{\"schema_version\":" << BootstrapSchemaVersion;
        stream << ",\"indicators\":[";
        for (std::size_t index = 0; index < state.indicators.size(); ++index) {
            if (index > 0U) stream << ',';
            const IndicatorInstanceDefinition& definition =
                state.indicators[index];
            stream << '{';
            stream << "\"id\":";
            AppendQuoted(stream, definition.spec.id);
            stream << ",\"type\":";
            AppendQuoted(stream, definition.spec.type);
            stream << ",\"visible\":"
                   << (definition.visible ? "true" : "false");
            stream << ",\"parameters\":{";
            bool firstParameter = true;
            for (const auto& parameter : definition.spec.parameters) {
                if (!std::isfinite(parameter.second)) continue;
                if (!firstParameter) stream << ',';
                firstParameter = false;
                AppendQuoted(stream, parameter.first);
                stream << ':' << parameter.second;
            }
            stream << "}}";
        }
        stream << "],\"pane_height_weights\":{";
        bool firstPane = true;
        for (const auto& pane : state.paneHeightWeights) {
            if (pane.first.empty() ||
                !std::isfinite(pane.second) ||
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

        return WriteAtomic(
            std::filesystem::path(path),
            stream.str(),
            error);
    }

    bool LoadChartWorkspaceBootstrap(
        const std::string& path,
        ChartWorkspacePersistenceState& state,
        bool& found,
        std::string& error)
    {
        found = false;
        const std::filesystem::path target(path);
        std::error_code existsError;
        if (!std::filesystem::exists(target, existsError)) {
            error.clear();
            return true;
        }

        std::string text;
        if (!ReadFile(target, text, error)) return false;
        const json_lite::ParseResult parsed = json_lite::Parse(text);
        if (!parsed.ok || !parsed.value.IsObject()) {
            error = parsed.ok
                ? "차트 복원본 JSON 루트가 객체가 아닙니다."
                : "차트 복원본 JSON 파싱 실패: " + parsed.error;
            return false;
        }

        const json_lite::Value* schema = Find(
            parsed.value,
            "schema_version");
        if (schema == nullptr || !schema->IsNumber() ||
            schema->AsInt(0) != BootstrapSchemaVersion)
        {
            error = "지원하지 않는 차트 복원본 버전입니다.";
            return false;
        }

        const json_lite::Value* indicatorsValue = Find(
            parsed.value,
            "indicators");
        if (indicatorsValue == nullptr || !indicatorsValue->IsArray()) {
            error = "차트 복원본에 indicators 배열이 없습니다.";
            return false;
        }

        ChartWorkspacePersistenceState candidate;
        for (const json_lite::Value& item : indicatorsValue->AsArray()) {
            if (!item.IsObject()) {
                error = "차트 복원본 지표 항목이 객체가 아닙니다.";
                return false;
            }

            indicators::IndicatorSpec spec;
            spec.id = StringValue(item, "id");
            spec.type = StringValue(item, "type");
            const json_lite::Value* parameters = Find(item, "parameters");
            if (parameters != nullptr && parameters->IsObject()) {
                for (const auto& parameter : parameters->AsObject()) {
                    if (parameter.second.IsNumber() &&
                        std::isfinite(parameter.second.AsNumber()))
                    {
                        spec.parameters[parameter.first] =
                            parameter.second.AsNumber();
                    }
                }
            }
            if (spec.id.empty() || spec.type.empty()) {
                error = "차트 복원본 지표 id 또는 type이 비어 있습니다.";
                return false;
            }

            IndicatorInstanceDefinition definition;
            if (!CreateIndicatorDefinition(spec, definition, error)) {
                error = "차트 복원본 지표 생성 실패 " + spec.id +
                    ": " + error;
                return false;
            }
            definition.visible = BooleanValue(
                item,
                "visible",
                definition.visible);
            candidate.indicators.push_back(std::move(definition));
        }

        const json_lite::Value* paneWeights = Find(
            parsed.value,
            "pane_height_weights");
        if (paneWeights != nullptr && paneWeights->IsObject()) {
            for (const auto& pane : paneWeights->AsObject()) {
                if (!pane.second.IsNumber()) continue;
                const double weight = pane.second.AsNumber();
                if (!pane.first.empty() &&
                    std::isfinite(weight) &&
                    weight > 0.0 && weight <= 100.0)
                {
                    candidate.paneHeightWeights[pane.first] =
                        static_cast<float>(weight);
                }
            }
        }

        state = std::move(candidate);
        found = true;
        error.clear();
        return true;
    }
}
