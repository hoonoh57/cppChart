#pragma once

#include "kiwoom_symbol_catalog.h"

#include <chrono>
#include <string>
#include <vector>

namespace trading
{
    struct SymbolMasterCacheLoadResult final
    {
        bool loaded = false;
        bool fresh = false;
        std::vector<SymbolCatalogEntry> entries;
        std::string error;
    };

    class SymbolMasterCache final
    {
    public:
        static SymbolMasterCacheLoadResult Load(
            const std::string& path,
            std::chrono::hours maximumAge = std::chrono::hours(24));

        static bool SaveAtomic(
            const std::string& path,
            const std::vector<SymbolCatalogEntry>& entries,
            std::string& error);

        static std::vector<SymbolCatalogEntry> Normalize(
            const std::vector<SymbolCatalogEntry>& entries);
    };
}
