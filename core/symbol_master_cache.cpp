#include "symbol_master_cache.h"

#include "json_lite.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace trading
{
    namespace
    {
        constexpr int kSchemaVersion = 1;

        bool IsSixDigitCode(const std::string& code) noexcept
        {
            return code.size() == 6U &&
                std::all_of(code.begin(), code.end(), [](unsigned char ch) {
                    return std::isdigit(ch) != 0;
                });
        }

        std::int64_t NowEpochMillis()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }

        bool ReplaceFile(
            const std::filesystem::path& temporary,
            const std::filesystem::path& destination,
            std::string& error)
        {
#if defined(_WIN32)
            if (MoveFileExW(
                    temporary.c_str(),
                    destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0)
            {
                error.clear();
                return true;
            }
            error = "종목 마스터 캐시 교체 실패: " +
                std::to_string(static_cast<unsigned long>(GetLastError()));
            return false;
#else
            std::error_code renameError;
            std::filesystem::rename(temporary, destination, renameError);
            if (!renameError) {
                error.clear();
                return true;
            }
            error = "종목 마스터 캐시 교체 실패: " + renameError.message();
            return false;
#endif
        }
    }

    std::vector<SymbolCatalogEntry> SymbolMasterCache::Normalize(
        const std::vector<SymbolCatalogEntry>& entries)
    {
        std::map<std::string, SymbolCatalogEntry> unique;
        for (const SymbolCatalogEntry& entry : entries) {
            if (!IsSixDigitCode(entry.code) ||
                entry.name.empty() ||
                entry.market.empty())
            {
                continue;
            }
            unique[entry.code] = entry;
        }

        std::vector<SymbolCatalogEntry> normalized;
        normalized.reserve(unique.size());
        for (const auto& pair : unique) {
            normalized.push_back(pair.second);
        }
        return normalized;
    }

    SymbolMasterCacheLoadResult SymbolMasterCache::Load(
        const std::string& path,
        std::chrono::hours maximumAge)
    {
        SymbolMasterCacheLoadResult result;
        const std::filesystem::path cachePath(path);

        std::error_code statusError;
        if (!std::filesystem::exists(cachePath, statusError)) {
            result.error = "종목 마스터 캐시가 없습니다.";
            return result;
        }

        std::ifstream input(cachePath, std::ios::binary);
        if (!input) {
            result.error = "종목 마스터 캐시를 열 수 없습니다.";
            return result;
        }
        std::ostringstream stream;
        stream << input.rdbuf();

        const json_lite::ParseResult parsed = json_lite::Parse(stream.str());
        if (!parsed.ok || !parsed.value.IsObject()) {
            result.error = parsed.error.empty()
                ? "종목 마스터 캐시 JSON이 올바르지 않습니다."
                : parsed.error;
            return result;
        }

        const json_lite::Value* schema = parsed.value.Find("schema_version");
        const json_lite::Value* symbols = parsed.value.Find("symbols");
        if (schema == nullptr || schema->AsInt() != kSchemaVersion ||
            symbols == nullptr || !symbols->IsArray())
        {
            result.error = "종목 마스터 캐시 스키마가 올바르지 않습니다.";
            return result;
        }

        std::vector<SymbolCatalogEntry> entries;
        for (const json_lite::Value& value : symbols->AsArray()) {
            if (!value.IsObject()) continue;
            const json_lite::Value* code = value.Find("code");
            const json_lite::Value* name = value.Find("name");
            const json_lite::Value* market = value.Find("market");
            if (code == nullptr || name == nullptr || market == nullptr) continue;

            SymbolCatalogEntry entry;
            entry.code = code->StringOr();
            entry.name = name->StringOr();
            entry.market = market->StringOr();
            entries.push_back(std::move(entry));
        }

        result.entries = Normalize(entries);
        if (result.entries.empty()) {
            result.error = "종목 마스터 캐시에 유효한 종목이 없습니다.";
            return result;
        }

        std::error_code timeError;
        const auto modified = std::filesystem::last_write_time(cachePath, timeError);
        if (!timeError) {
            const auto now = std::filesystem::file_time_type::clock::now();
            result.fresh = now >= modified && (now - modified) <= maximumAge;
        }
        result.loaded = true;
        result.error.clear();
        return result;
    }

    bool SymbolMasterCache::SaveAtomic(
        const std::string& path,
        const std::vector<SymbolCatalogEntry>& entries,
        std::string& error)
    {
        const std::vector<SymbolCatalogEntry> normalized = Normalize(entries);
        if (normalized.empty()) {
            error = "저장할 유효한 종목 마스터가 없습니다.";
            return false;
        }

        const std::filesystem::path destination(path);
        const std::filesystem::path parent = destination.parent_path();
        std::error_code directoryError;
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, directoryError);
            if (directoryError) {
                error = "종목 마스터 캐시 폴더 생성 실패: " +
                    directoryError.message();
                return false;
            }
        }

        const std::filesystem::path temporary = destination.string() + ".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "종목 마스터 임시 캐시를 열 수 없습니다.";
            return false;
        }

        output << "{\n";
        output << "  \"schema_version\": " << kSchemaVersion << ",\n";
        output << "  \"fetched_at_epoch_ms\": " << NowEpochMillis() << ",\n";
        output << "  \"symbols\": [\n";
        for (std::size_t index = 0; index < normalized.size(); ++index) {
            const SymbolCatalogEntry& entry = normalized[index];
            output << "    {\"code\":\"" << json_lite::EscapeString(entry.code)
                   << "\",\"name\":\"" << json_lite::EscapeString(entry.name)
                   << "\",\"market\":\"" << json_lite::EscapeString(entry.market)
                   << "\"}";
            if (index + 1U < normalized.size()) output << ',';
            output << '\n';
        }
        output << "  ]\n";
        output << "}\n";
        output.flush();
        if (!output) {
            output.close();
            std::error_code removeError;
            std::filesystem::remove(temporary, removeError);
            error = "종목 마스터 임시 캐시 쓰기 실패";
            return false;
        }
        output.close();

        if (!ReplaceFile(temporary, destination, error)) {
            std::error_code removeError;
            std::filesystem::remove(temporary, removeError);
            return false;
        }
        error.clear();
        return true;
    }
}
