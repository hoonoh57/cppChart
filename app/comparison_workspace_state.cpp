#include "comparison_workspace_state.h"

#include "../core/json_lite.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace trading::app
{
    namespace
    {
        constexpr int SchemaVersion = 1;

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

        const char* KindName(ComparisonInstrumentKind kind) noexcept
        {
            return kind == ComparisonInstrumentKind::Index
                ? "Index"
                : "Stock";
        }

        bool ParseKind(
            const std::string& value,
            ComparisonInstrumentKind& kind) noexcept
        {
            if (value == "Stock") {
                kind = ComparisonInstrumentKind::Stock;
                return true;
            }
            if (value == "Index") {
                kind = ComparisonInstrumentKind::Index;
                return true;
            }
            return false;
        }

        const char* PlacementName(ComparisonPlacement placement) noexcept
        {
            return placement == ComparisonPlacement::PriceSecondaryAxis
                ? "PriceSecondaryAxis"
                : "SeparatePane";
        }

        bool ParsePlacement(
            const std::string& value,
            ComparisonPlacement& placement) noexcept
        {
            if (value == "SeparatePane") {
                placement = ComparisonPlacement::SeparatePane;
                return true;
            }
            if (value == "PriceSecondaryAxis") {
                placement = ComparisonPlacement::PriceSecondaryAxis;
                return true;
            }
            return false;
        }

        const char* ValueModeName(ComparisonValueMode mode) noexcept
        {
            switch (mode) {
            case ComparisonValueMode::Indexed100:
                return "Indexed100";
            case ComparisonValueMode::ReturnPercent:
                return "ReturnPercent";
            case ComparisonValueMode::RelativeStrength100:
                return "RelativeStrength100";
            default:
                return "RawClose";
            }
        }

        bool ParseValueMode(
            const std::string& value,
            ComparisonValueMode& mode) noexcept
        {
            if (value == "RawClose") {
                mode = ComparisonValueMode::RawClose;
                return true;
            }
            if (value == "Indexed100") {
                mode = ComparisonValueMode::Indexed100;
                return true;
            }
            if (value == "ReturnPercent") {
                mode = ComparisonValueMode::ReturnPercent;
                return true;
            }
            if (value == "RelativeStrength100") {
                mode = ComparisonValueMode::RelativeStrength100;
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
            render::LineStyle& style) noexcept
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

        bool ParseColor(
            const json_lite::Value* value,
            render::ColorRgba& color) noexcept
        {
            if (value == nullptr || !value->IsObject()) return false;
            const int red = IntegerValue(*value, "red", -1);
            const int green = IntegerValue(*value, "green", -1);
            const int blue = IntegerValue(*value, "blue", -1);
            const int alpha = IntegerValue(*value, "alpha", -1);
            if (red < 0 || red > 255 || green < 0 || green > 255 ||
                blue < 0 || blue > 255 || alpha < 0 || alpha > 255)
            {
                return false;
            }
            color = {
                static_cast<std::uint8_t>(red),
                static_cast<std::uint8_t>(green),
                static_cast<std::uint8_t>(blue),
                static_cast<std::uint8_t>(alpha)
            };
            return true;
        }

        bool ReadText(
            const std::filesystem::path& path,
            std::string& text,
            std::string& error)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                error = "비교 상태 파일을 열지 못했습니다: " + path.string();
                return false;
            }
            std::ostringstream buffer;
            buffer << input.rdbuf();
            if (!input.good() && !input.eof()) {
                error = "비교 상태 파일을 읽지 못했습니다: " + path.string();
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
                    error = "비교 상태 폴더 생성 실패: " + fileError.message();
                    return false;
                }
            }
            {
                std::ofstream output(
                    temporary,
                    std::ios::binary | std::ios::trunc);
                if (!output) {
                    error = "비교 상태 임시 파일을 만들지 못했습니다.";
                    return false;
                }
                output.write(text.data(), static_cast<std::streamsize>(text.size()));
                output.flush();
                if (!output) {
                    error = "비교 상태 파일 저장에 실패했습니다.";
                    return false;
                }
            }
            std::filesystem::remove(target, fileError);
            fileError.clear();
            std::filesystem::rename(temporary, target, fileError);
            if (fileError) {
                std::filesystem::remove(temporary, fileError);
                error = "비교 상태 파일 교체 실패: " + fileError.message();
                return false;
            }
            error.clear();
            return true;
        }

        bool ParseState(
            const std::string& text,
            ComparisonWorkspaceState& state,
            std::string& error)
        {
            const json_lite::ParseResult parsed = json_lite::Parse(text);
            if (!parsed.ok || !parsed.value.IsObject()) {
                error = parsed.ok
                    ? "비교 상태 JSON 루트가 객체가 아닙니다."
                    : "비교 상태 JSON 파싱 실패: " + parsed.error;
                return false;
            }
            const json_lite::Value* schema = Find(parsed.value, "schema_version");
            if (schema == nullptr || !schema->IsNumber() ||
                schema->AsInt(0) != SchemaVersion)
            {
                error = "지원하지 않는 비교 상태 JSON 버전입니다.";
                return false;
            }
            const json_lite::Value* definitions = Find(parsed.value, "definitions");
            if (definitions == nullptr || !definitions->IsArray()) {
                error = "비교 상태 JSON에 definitions 배열이 없습니다.";
                return false;
            }

            ComparisonWorkspaceState candidate;
            for (const json_lite::Value& item : definitions->AsArray()) {
                if (!item.IsObject()) {
                    error = "비교 정의가 객체가 아닙니다.";
                    return false;
                }
                ComparisonDefinition definition;
                definition.id = StringValue(item, "id");
                definition.code = StringValue(item, "code");
                definition.displayName = StringValue(item, "display_name");
                definition.visible = BooleanValue(item, "visible", true);
                definition.paneId = StringValue(item, "pane_id");
                definition.paneTitle = StringValue(item, "pane_title");
                definition.valueDivisor = NumberValue(item, "value_divisor", 1.0);
                definition.width = static_cast<float>(
                    NumberValue(item, "width", 1.5));
                definition.paneHeightWeight = static_cast<float>(
                    NumberValue(item, "pane_height_weight", 0.35));
                definition.valueDecimals = IntegerValue(item, "value_decimals", 2);

                if (!ParseKind(StringValue(item, "kind"), definition.kind)) {
                    error = "비교 종류가 올바르지 않습니다: " + definition.id;
                    return false;
                }
                if (!ParsePlacement(
                        StringValue(item, "placement"),
                        definition.placement))
                {
                    error = "비교 배치가 올바르지 않습니다: " + definition.id;
                    return false;
                }
                if (!ParseValueMode(
                        StringValue(item, "value_mode"),
                        definition.valueMode))
                {
                    error = "비교 값 모드가 올바르지 않습니다: " + definition.id;
                    return false;
                }
                if (!ParseLineStyle(
                        StringValue(item, "line_style"),
                        definition.style))
                {
                    error = "비교 선 스타일이 올바르지 않습니다: " + definition.id;
                    return false;
                }
                if (!ParseColor(Find(item, "color"), definition.color)) {
                    error = "비교 선 색상이 올바르지 않습니다: " + definition.id;
                    return false;
                }
                candidate.definitions.push_back(std::move(definition));
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

            if (!ValidateComparisonWorkspaceState(candidate, error)) return false;
            state = std::move(candidate);
            error.clear();
            return true;
        }

        bool LoadOne(
            const std::string& path,
            ComparisonWorkspaceState& state,
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

    bool ValidateComparisonWorkspaceState(
        const ComparisonWorkspaceState& state,
        std::string& error)
    {
        if (state.definitions.size() > 32U) {
            error = "비교 시계열은 최대 32개입니다.";
            return false;
        }
        std::set<std::string> ids;
        std::set<std::string> sources;
        for (const ComparisonDefinition& definition : state.definitions) {
            if (!ComparisonModule::ValidateDefinition(definition, error)) {
                return false;
            }
            if (!ids.insert(definition.id).second) {
                error = "중복 비교 id: " + definition.id;
                return false;
            }
            const std::string source =
                std::to_string(static_cast<int>(definition.kind)) + ":" +
                definition.code;
            if (!sources.insert(source).second) {
                error = "동일 비교 소스가 중복되었습니다: " + definition.code;
                return false;
            }
        }
        for (const auto& pane : state.paneHeightWeights) {
            if (pane.first.empty() || !std::isfinite(pane.second) ||
                pane.second <= 0.0f || pane.second > 100.0f)
            {
                error = "비교 패널 높이가 올바르지 않습니다.";
                return false;
            }
        }
        error.clear();
        return true;
    }

    bool SerializeComparisonWorkspaceState(
        const ComparisonWorkspaceState& state,
        std::string& json,
        std::string& error)
    {
        if (!ValidateComparisonWorkspaceState(state, error)) return false;

        std::ostringstream stream;
        stream.precision(17);
        stream << "{\n  \"schema_version\": " << SchemaVersion;
        stream << ",\n  \"definitions\": [";
        for (std::size_t index = 0; index < state.definitions.size(); ++index) {
            const ComparisonDefinition& definition = state.definitions[index];
            stream << (index == 0U ? "\n" : ",\n");
            stream << "    {\"id\":" << json_lite::EscapeString(definition.id);
            stream << ",\"kind\":" << json_lite::EscapeString(KindName(definition.kind));
            stream << ",\"code\":" << json_lite::EscapeString(definition.code);
            stream << ",\"display_name\":" << json_lite::EscapeString(definition.displayName);
            stream << ",\"visible\":" << (definition.visible ? "true" : "false");
            stream << ",\"placement\":" << json_lite::EscapeString(PlacementName(definition.placement));
            stream << ",\"value_mode\":" << json_lite::EscapeString(ValueModeName(definition.valueMode));
            stream << ",\"pane_id\":" << json_lite::EscapeString(definition.paneId);
            stream << ",\"pane_title\":" << json_lite::EscapeString(definition.paneTitle);
            stream << ",\"value_divisor\":" << definition.valueDivisor;
            stream << ",\"color\":{\"red\":" << static_cast<int>(definition.color.red)
                   << ",\"green\":" << static_cast<int>(definition.color.green)
                   << ",\"blue\":" << static_cast<int>(definition.color.blue)
                   << ",\"alpha\":" << static_cast<int>(definition.color.alpha) << '}';
            stream << ",\"width\":" << definition.width;
            stream << ",\"line_style\":" << json_lite::EscapeString(LineStyleName(definition.style));
            stream << ",\"pane_height_weight\":" << definition.paneHeightWeight;
            stream << ",\"value_decimals\":" << definition.valueDecimals;
            stream << '}';
        }
        stream << "\n  ],\n  \"pane_height_weights\": {";
        bool firstPane = true;
        for (const auto& pane : state.paneHeightWeights) {
            stream << (firstPane ? "\n" : ",\n");
            firstPane = false;
            stream << "    " << json_lite::EscapeString(pane.first)
                   << ": " << pane.second;
        }
        if (!firstPane) stream << '\n';
        stream << "  }\n}\n";

        const json_lite::ParseResult parsed = json_lite::Parse(stream.str());
        if (!parsed.ok) {
            error = "비교 상태 JSON 자체검증 실패: " + parsed.error;
            return false;
        }
        json = stream.str();
        error.clear();
        return true;
    }

    bool SaveComparisonWorkspaceState(
        const std::string& path,
        const ComparisonWorkspaceState& state,
        std::string& error)
    {
        std::string json;
        if (!SerializeComparisonWorkspaceState(state, json, error)) return false;
        return WriteAtomic(std::filesystem::path(path), json, error);
    }

    bool LoadComparisonWorkspaceState(
        const std::string& savedPath,
        const std::string& defaultPath,
        ComparisonWorkspaceState& state,
        ComparisonWorkspaceSource& source,
        std::string& diagnostic)
    {
        source = ComparisonWorkspaceSource::None;
        diagnostic.clear();
        if (Exists(savedPath)) {
            std::string error;
            if (!LoadOne(savedPath, state, error)) {
                diagnostic = "저장 비교 JSON 오류: " + error;
                return false;
            }
            source = ComparisonWorkspaceSource::Saved;
            return true;
        }
        if (Exists(defaultPath)) {
            std::string error;
            if (!LoadOne(defaultPath, state, error)) {
                diagnostic = "기본 비교 JSON 오류: " + error;
                return false;
            }
            source = ComparisonWorkspaceSource::Default;
            return true;
        }
        diagnostic = "비교 상태 JSON 파일을 찾지 못했습니다.";
        return false;
    }

    const char* ComparisonWorkspaceSourceName(
        ComparisonWorkspaceSource source) noexcept
    {
        switch (source) {
        case ComparisonWorkspaceSource::Saved:
            return "직전 저장값";
        case ComparisonWorkspaceSource::Default:
            return "기본값";
        default:
            return "없음";
        }
    }
}
