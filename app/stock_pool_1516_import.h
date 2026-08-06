#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace trading::stock_pool::import1516
{
    struct SymbolMasterEntry final
    {
        std::string code;
        std::string name;
        std::string market;
    };

    enum class ResolutionStatus
    {
        Unresolved,
        Resolved,
        MissingSymbol,
        AmbiguousSymbol,
        DuplicateSymbol,
        InvalidRow
    };

    struct ImportedRow final
    {
        std::size_t sourceLine = 0U;
        std::string name;
        double return1mPercent = 0.0;
        double return3mPercent = 0.0;
        double return7hPercent = 0.0;
        double maximumReturnPercent = 0.0;
        std::int64_t captureVolume = 0;
        double otherValue = 0.0;

        std::string code;
        std::string market;
        ResolutionStatus status = ResolutionStatus::Unresolved;
        std::string reason;
    };

    struct ParseResult final
    {
        std::vector<ImportedRow> rows;
        std::vector<std::string> diagnostics;
        std::size_t ignoredHeaderLines = 0U;
    };

    ParseResult ParseClipboardText(const std::string& text);

    void ResolveExactSymbolNames(
        std::vector<ImportedRow>& rows,
        const std::vector<SymbolMasterEntry>& symbolMaster);

    const char* ResolutionStatusName(ResolutionStatus status) noexcept;
}
