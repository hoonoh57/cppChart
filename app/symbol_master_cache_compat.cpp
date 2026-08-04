#include "symbol_master_cache.h"
#include "../core/symbol_master_cache.h"

namespace trading
{
    SymbolMasterCacheLoadResult SymbolMasterCache::Load(
        const std::string& path,
        std::chrono::hours maximumAge)
    {
        const app::SymbolMasterCacheLoadResult source =
            app::SymbolMasterCache::Load(path, maximumAge);

        SymbolMasterCacheLoadResult result;
        result.loaded = source.loaded;
        result.fresh = source.fresh;
        result.entries = source.entries;
        result.error = source.error;
        return result;
    }

    bool SymbolMasterCache::SaveAtomic(
        const std::string& path,
        const std::vector<SymbolCatalogEntry>& entries,
        std::string& error)
    {
        return app::SymbolMasterCache::SaveAtomic(path, entries, error);
    }

    std::vector<SymbolCatalogEntry> SymbolMasterCache::Normalize(
        const std::vector<SymbolCatalogEntry>& entries)
    {
        return app::SymbolMasterCache::Normalize(entries);
    }
}
