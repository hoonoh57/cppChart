#pragma once

#include <string>
#include <vector>

#include "../app/stock_pool_1516_import.h"

namespace trading::stock_pool::platform
{
    struct SymbolMasterLoadResult final
    {
        bool ok = false;
        std::vector<import1516::SymbolMasterEntry> entries;
        std::string error;
        std::string source;
    };

    SymbolMasterLoadResult LoadGate3SymbolMaster(
        const std::string& environmentFilePath);
}
