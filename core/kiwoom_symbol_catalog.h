#pragma once

#include "kiwoom_protocol.h"
#include "kiwoom_reconciliation.h"

#include <string>
#include <vector>

namespace trading
{
    struct SymbolCatalogEntry final
    {
        std::string code;
        std::string name;
        std::string market;
    };

    struct SymbolCatalogPage final
    {
        ProtocolResult result;
        std::vector<SymbolCatalogEntry> entries;
    };

    RestRequest BuildSymbolCatalogRestRequest(
        const std::string& marketType,
        const std::string& bearerToken,
        const Continuation& continuation,
        std::string& error);

    SymbolCatalogPage ParseSymbolCatalogResponse(
        const std::string& market,
        const std::string& json);

    std::vector<SymbolCatalogEntry> SearchSymbolCatalog(
        const std::vector<SymbolCatalogEntry>& entries,
        const std::string& query,
        std::size_t limit = 20U);
}
