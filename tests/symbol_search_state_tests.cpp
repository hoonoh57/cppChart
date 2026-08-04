#include "../ui/symbol_search_state.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::exit(1);
    }

    void Check(bool condition, const char* message)
    {
        if (!condition) Fail(message);
    }
}

int main()
{
    using namespace trading;
    using namespace trading::ui;

    const std::vector<SymbolCatalogEntry> catalog = {
        { "005930", "삼성전자", "KOSPI" },
        { "005935", "삼성전자우", "KOSPI" },
        { "000660", "SK하이닉스", "KOSPI" }
    };

    SymbolSearchState state;
    SetSymbolCatalogLoading(state);
    Check(state.catalogState == SymbolCatalogLoadState::Loading,
          "loading state mismatch");
    SetSymbolCatalogLoaded(state, catalog.size());
    Check(state.catalogState == SymbolCatalogLoadState::Loaded,
          "loaded state mismatch");
    Check(state.catalogCount == catalog.size(),
          "loaded count mismatch");

    std::snprintf(state.query, sizeof(state.query), "%s", "삼성");
    RefreshSymbolMatches(state, catalog);
    Check(state.popupOpen, "candidate popup must open");
    Check(state.matches.size() == 2U, "Korean candidate count mismatch");
    Check(state.highlightedIndex == 0, "initial highlight mismatch");

    MoveSymbolHighlight(state, 1);
    Check(state.highlightedIndex == 1, "down navigation mismatch");
    MoveSymbolHighlight(state, 1);
    Check(state.highlightedIndex == 0, "down navigation wrap mismatch");
    MoveSymbolHighlight(state, -1);
    Check(state.highlightedIndex == 1, "up navigation wrap mismatch");

    Check(ConfirmHighlightedSymbol(state), "Enter confirmation failed");
    Check(state.HasValidSelection(), "confirmed selection must be valid");
    Check(state.selection.code == "005935", "confirmed code mismatch");
    Check(state.selection.name == "삼성전자우", "confirmed name mismatch");
    Check(!state.popupOpen, "popup must close after confirmation");

    std::snprintf(state.query, sizeof(state.query), "%s", "arbitrary");
    RejectFreeSymbolText(state);
    Check(!state.HasValidSelection(), "free text must invalidate selection");

    std::snprintf(state.query, sizeof(state.query), "%s", "005930");
    Check(ConfirmExactSymbolCode(state, catalog),
          "exact six-digit code confirmation failed");
    Check(state.selection.code == "005930", "exact code selection mismatch");
    Check(ConfirmedSymbolLabel(state) == "005930 삼성전자 [KOSPI]",
          "confirmed label mismatch");

    std::snprintf(state.query, sizeof(state.query), "%s", "123456");
    ClearSymbolSelection(state);
    Check(!ConfirmExactSymbolCode(state, catalog),
          "unknown exact code must be rejected");
    Check(!state.HasValidSelection(),
          "unknown exact code must not create a selection");

    SetSymbolCatalogFailed(state, "ka10099 timeout");
    Check(state.catalogState == SymbolCatalogLoadState::Failed,
          "failed state mismatch");
    Check(state.catalogError == "ka10099 timeout",
          "exact catalog error must be retained");
    Check(state.matches.empty(), "failed catalog must clear matches");

    std::puts("[PASS] symbol_search_state_tests");
    return 0;
}
