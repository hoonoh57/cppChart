#include "../core/symbol_master_cache.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* message)
    {
        if (condition) return;
        std::fprintf(stderr, "[FAIL] %s\n", message);
        ++g_failures;
    }
}

int main()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cppchart_symbol_master_cache_test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root, ignored);
    const std::filesystem::path cache = root / "symbols.json";

    std::vector<trading::SymbolCatalogEntry> entries = {
        { "005930", "삼성전자", "KOSPI" },
        { "000660", "SK하이닉스", "KOSPI" },
        { "035720", "카카오", "KOSDAQ" },
        { "005930", "삼성전자", "KOSPI" },
        { "BAD", "잘못된값", "KOSPI" }
    };

    std::string error;
    Check(
        trading::SymbolMasterCache::SaveAtomic(cache.string(), entries, error),
        "cache save succeeds");
    Check(error.empty(), "save error is empty");

    const trading::SymbolMasterCacheLoadResult loaded =
        trading::SymbolMasterCache::Load(cache.string(), std::chrono::hours(24));
    Check(loaded.loaded, "cache loads");
    Check(loaded.fresh, "new cache is fresh");
    Check(loaded.entries.size() == 3U, "invalid and duplicate entries removed");
    Check(loaded.entries[0].code == "000660", "entries sorted by code");
    Check(loaded.entries[1].name == "삼성전자", "Korean name preserved");

    {
        std::ofstream corrupt(cache, std::ios::binary | std::ios::trunc);
        corrupt << "{broken";
    }
    const trading::SymbolMasterCacheLoadResult corrupted =
        trading::SymbolMasterCache::Load(cache.string());
    Check(!corrupted.loaded, "corrupt cache rejected");
    Check(!corrupted.error.empty(), "corrupt cache reports error");

    std::filesystem::remove_all(root, ignored);
    if (g_failures == 0) {
        std::printf("[PASS] symbol_master_cache_tests\n");
        return 0;
    }
    return 1;
}
