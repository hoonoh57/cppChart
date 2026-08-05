#include "chart_workspace_profile.h"

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
        constexpr int ProfileSchemaVersion = 1;

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
                error = "지표 작업공간 파일을 열지 못했습니다: " +
                    path.string();
                return false;
            }
            std::ostringstream buffer;
            buffer << input.rdbuf();
            if (!input.good() && !input.eof()) {
                error = "지표 작업공간 파일을 읽지 못했습니다: " +
                    path.string();
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
            const std::filesystem::path temporary =
                target.string() + ".tmp";
            std::error_code fileError;
            if (!target.parent_path().empty()) {
                std::filesystem::create_directories(
                    target.parent_path(),
                    fileError);
                if (fileError) {
                    error = "지표 작업공간 폴더 생성 실패: " +
                        fileError.message();
                    return false;
                }
            }

            {
                std::ofstream output(
                    temporary,
                    std::ios::binary | std::ios::trunc);
                if (!output) {
                    error = "지표 작업공간 임시 파일을 만들지 못했습니다.";
                    return false;
                }
                output.write(
                    text.data(),
                    static_cast<std::streamsize>(text.size()));
                output.flush();
                if (!output) {
                    error = "지표 작업공간 임시 파일 저장에 실패했습니다.";
                    return false;
                }
            }

            std::filesystem::remove(target, fileError);
            fileError.clear();
            std::filesystem::rename(temporary, target, fileError);
            if (fileError) {
                std::filesystem::remove(temporary, fileError);
                error = "지표 작업공간 파일 교체 실패: " +
                    fileError.message();
                return false;
            }
            error.clear();
            return true;
        }

        bool ParseProfile(
            const std::string& text,
            ChartWorkspacePersistenceState& state,
            std::string& error)
        {
            const json_lite::ParseResult parsed = json_lite::Parse(text);
            if (!parsed.ok || !parsed.value.IsObject()) {
                error = parsed.ok
                    ? "지표 작업공간 JSON 루트가 객체가 아닙니다."
                    : "지표 작업공간 JSON 파싱 실패: " + parsed.error;
                return false;
            }

            const json_lite::Value* indicatorsValue =
                Find(parsed.value, "indicators");
            if (indicatorsValue == nullptr ||
                !indicatorsValue->IsArray())
            {
                error = "지표 작업공간 JSON에 indicators 배열이 없습니다.";
                return false;
            }

            ChartWorkspacePersistenceState candidate;
            std::set<std::string> ids;
            std::set<std::string> types;
            for (const json_lite::Value& item :
                 indicatorsValue->AsArray())
            {
                if (!item.IsObject()) {
                    error = "지표 작업공간 항목이 객체가 아닙니다.";
                    return false;
                }

                indicators::IndicatorSpec spec;
                spec.id = StringValue(item, "id");
                spec.type = StringValue(item, "type");
                if (spec.id.empty() || spec.type.empty()) {
                    error = "지표 작업공간의 id 또는 type이 비어 있습니다.";
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
                    error = "지표 작업공간 복원 실패 " + spec.id +
                        ": " + error;
                    return false;
                }
                definition.visible = BooleanValue(
                    item,
                    "visible",
                    definition.visible);
                candidate.indicators.push_back(std::move(definition));
                types.insert(spec.type);
            }

            // Feature-registry style contract: every supported indicator type
            // always exists. Missing types are restored as hidden entries so
            // the user changes state instead of creating/removing primitives.
            for (const IndicatorCatalogEntry& entry : IndicatorCatalog()) {
                if (types.find(entry.type) != types.end()) continue;
                IndicatorInstanceDefinition definition;
                std::string createError;
                if (!CreateDefaultIndicatorDefinition(
                        entry.type,
                        DefaultIdForType(entry.type),
                        definition,
                        createError))
                {
                    error = "기본 지표 생성 실패 " + entry.type +
                        ": " + createError;
                    return false;
                }
                definition.visible = false;
                candidate.indicators.push_back(std::move(definition));
            }

            const json_lite::Value* paneWeights =
                Find(parsed.value, "pane_height_weights");
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

            IndicatorRenderPlan plan;
            if (!BuildIndicatorRenderPlan(
                    candidate.indicators,
                    plan,
                    error))
            {
                error = "지표 작업공간 렌더 계획 검증 실패: " + error;
                return false;
            }

            state = std::move(candidate);
            error.clear();
            return true;
        }
    }

    bool LoadChartWorkspaceProfileState(
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
        if (!ReadText(target, text, error)) return false;
        ChartWorkspacePersistenceState candidate;
        if (!ParseProfile(text, candidate, error)) return false;
        state = std::move(candidate);
        found = true;
        error.clear();
        return true;
    }

    bool SaveChartWorkspaceProfileState(
        const std::string& path,
        const ChartWorkspacePersistenceState& state,
        std::string& error)
    {
        IndicatorRenderPlan plan;
        if (!BuildIndicatorRenderPlan(state.indicators, plan, error)) {
            return false;
        }

        std::ostringstream stream;
        stream << std::setprecision(17);
        stream << "{\"schema_version\":" << ProfileSchemaVersion;
        stream << ",\"indicators\":[";
        for (std::size_t index = 0;
             index < state.indicators.size();
             ++index)
        {
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
                pane.second <= 0.0f || pane.second > 100.0f)
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
}
