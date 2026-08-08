#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../app/stock_pool_1516_import.h"

namespace trading::stock_pool::platform
{
    struct GatewayRejectedSymbol final
    {
        std::size_t inputIndex = 0U;
        std::string name;
        std::string reason;
    };

    struct SymbolResolveResult final
    {
        bool ok = false;
        std::vector<import1516::SymbolMasterEntry> entries;
        std::vector<GatewayRejectedSymbol> rejected;
        std::string error;
        std::string source;
    };

    struct CohortSaveResult final
    {
        bool ok = false;
        long long cohortId = 0;
        std::size_t memberCount = 0U;
        std::vector<GatewayRejectedSymbol> rejected;
        std::string rawImportHash;
        std::string source;
        std::string error;
    };

    SymbolResolveResult ResolveSymbolsViaServer32(
        const std::vector<std::string>& names,
        const std::string& environmentFilePath);

    CohortSaveResult Save1516CohortViaServer32(
        const std::string& conditionName,
        const std::string& tradingDate,
        const std::string& captureTime,
        int timeframeMinutes,
        const std::vector<import1516::ImportedRow>& rows,
        const std::string& environmentFilePath);
}
