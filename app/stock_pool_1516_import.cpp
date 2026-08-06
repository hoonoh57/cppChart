#include "stock_pool_1516_import.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace trading::stock_pool::import1516
{
    namespace
    {
        std::string Trim(std::string value)
        {
            const auto isSpace = [](unsigned char character) {
                return character == ' ' || character == '\t' ||
                    character == '\r' || character == '\n';
            };
            while (!value.empty() && isSpace(
                       static_cast<unsigned char>(value.front())))
            {
                value.erase(value.begin());
            }
            while (!value.empty() && isSpace(
                       static_cast<unsigned char>(value.back())))
            {
                value.pop_back();
            }
            if (value.size() >= 2U && value.front() == '"' &&
                value.back() == '"')
            {
                value = value.substr(1U, value.size() - 2U);
            }
            return value;
        }

        std::vector<std::string> SplitTabs(const std::string& line)
        {
            std::vector<std::string> fields;
            std::size_t start = 0U;
            for (;;) {
                const std::size_t delimiter = line.find('\t', start);
                if (delimiter == std::string::npos) {
                    fields.push_back(Trim(line.substr(start)));
                    break;
                }
                fields.push_back(Trim(line.substr(start, delimiter - start)));
                start = delimiter + 1U;
            }
            while (!fields.empty() && fields.front().empty()) {
                fields.erase(fields.begin());
            }
            while (!fields.empty() && fields.back().empty()) {
                fields.pop_back();
            }
            return fields;
        }

        bool IsHeader(const std::vector<std::string>& fields)
        {
            if (fields.empty()) return true;
            const std::string& first = fields.front();
            return first.empty() || first == "종목명" ||
                first.find("기간 수익률") != std::string::npos ||
                first == "1분간" || first == "3분간" ||
                first == "7시간" || first.find("최고수익률") != std::string::npos;
        }

        bool ParseDoubleField(
            const std::string& source,
            bool percentage,
            double& value)
        {
            std::string text = Trim(source);
            text.erase(
                std::remove(text.begin(), text.end(), ','),
                text.end());
            if (percentage && !text.empty() && text.back() == '%') {
                text.pop_back();
            }
            text = Trim(text);
            if (text.empty()) return false;

            errno = 0;
            char* end = nullptr;
            const double parsed = std::strtod(text.c_str(), &end);
            if (end == text.c_str() || errno == ERANGE ||
                !std::isfinite(parsed))
            {
                return false;
            }
            while (end != nullptr && *end != '\0') {
                if (*end != ' ' && *end != '\t') return false;
                ++end;
            }
            value = parsed;
            return true;
        }

        bool ParseVolume(const std::string& source, std::int64_t& value)
        {
            std::string text = Trim(source);
            text.erase(
                std::remove(text.begin(), text.end(), ','),
                text.end());
            if (text.empty()) return false;
            errno = 0;
            char* end = nullptr;
            const long long parsed = std::strtoll(text.c_str(), &end, 10);
            if (end == text.c_str() || errno == ERANGE || parsed < 0) {
                return false;
            }
            while (end != nullptr && *end != '\0') {
                if (*end != ' ' && *end != '\t') return false;
                ++end;
            }
            value = static_cast<std::int64_t>(parsed);
            return true;
        }

        std::string LineDiagnostic(
            std::size_t line,
            const std::string& message)
        {
            return "line " + std::to_string(line) + ": " + message;
        }
    }

    ParseResult ParseClipboardText(const std::string& text)
    {
        ParseResult result;
        std::istringstream stream(text);
        std::string line;
        std::size_t lineNumber = 0U;
        while (std::getline(stream, line)) {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::vector<std::string> fields = SplitTabs(line);
            if (IsHeader(fields)) {
                if (!fields.empty()) ++result.ignoredHeaderLines;
                continue;
            }
            if (fields.size() < 7U) {
                result.diagnostics.push_back(
                    LineDiagnostic(lineNumber, "열 수 부족"));
                continue;
            }

            ImportedRow row;
            row.sourceLine = lineNumber;
            row.name = fields[0];
            bool valid = !row.name.empty();
            valid = valid && ParseDoubleField(
                fields[1], true, row.return1mPercent);
            valid = valid && ParseDoubleField(
                fields[2], true, row.return3mPercent);
            valid = valid && ParseDoubleField(
                fields[3], true, row.return7hPercent);
            valid = valid && ParseDoubleField(
                fields[4], true, row.maximumReturnPercent);
            valid = valid && ParseVolume(fields[5], row.captureVolume);
            valid = valid && ParseDoubleField(
                fields[6], false, row.otherValue);

            if (!valid) {
                row.status = ResolutionStatus::InvalidRow;
                row.reason = "1516 수치 형식 변환 실패";
                result.diagnostics.push_back(
                    LineDiagnostic(lineNumber, row.name + " 형식 오류"));
            }
            result.rows.push_back(std::move(row));
        }
        if (result.rows.empty()) {
            result.diagnostics.push_back("변환 가능한 종목 행이 없습니다.");
        }
        return result;
    }

    void ResolveExactSymbolNames(
        std::vector<ImportedRow>& rows,
        const std::vector<SymbolMasterEntry>& symbolMaster)
    {
        std::unordered_map<std::string, std::vector<const SymbolMasterEntry*>>
            byName;
        byName.reserve(symbolMaster.size());
        for (const SymbolMasterEntry& entry : symbolMaster) {
            if (entry.name.empty() || entry.code.empty()) continue;
            byName[entry.name].push_back(&entry);
        }

        std::unordered_set<std::string> resolvedCodes;
        for (ImportedRow& row : rows) {
            if (row.status == ResolutionStatus::InvalidRow) continue;
            const auto found = byName.find(row.name);
            if (found == byName.end() || found->second.empty()) {
                row.status = ResolutionStatus::MissingSymbol;
                row.reason = "gate3.g3_symbol_master exact name 0건";
                continue;
            }
            if (found->second.size() != 1U) {
                row.status = ResolutionStatus::AmbiguousSymbol;
                row.reason = "gate3.g3_symbol_master exact name 다건";
                continue;
            }
            const SymbolMasterEntry& symbol = *found->second.front();
            if (!resolvedCodes.insert(symbol.code).second) {
                row.status = ResolutionStatus::DuplicateSymbol;
                row.reason = "붙여넣기 내 종목코드 중복";
                continue;
            }
            row.code = symbol.code;
            row.market = symbol.market;
            row.status = ResolutionStatus::Resolved;
            row.reason.clear();
        }
    }

    const char* ResolutionStatusName(ResolutionStatus status) noexcept
    {
        switch (status) {
        case ResolutionStatus::Unresolved:
            return "미조회";
        case ResolutionStatus::Resolved:
            return "확정";
        case ResolutionStatus::MissingSymbol:
            return "코드없음";
        case ResolutionStatus::AmbiguousSymbol:
            return "동명이름";
        case ResolutionStatus::DuplicateSymbol:
            return "중복";
        case ResolutionStatus::InvalidRow:
            return "형식오류";
        }
        return "Unknown";
    }
}
