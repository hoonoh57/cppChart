from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "core" / "kiwoom_market_data.cpp"
TESTS = ROOT / "tests" / "kiwoom_market_data_tests.cpp"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


core = CORE.read_text(encoding="utf-8-sig")
core = replace_once(
    core,
    'constexpr const char* StockMinuteApiId = "ka10079";',
    'constexpr const char* StockMinuteApiId = "ka10080";',
    "stock minute API ID",
)
core = replace_once(
    core,
    'constexpr const char* IndexMinuteApiId = "ka20004";',
    'constexpr const char* IndexMinuteApiId = "ka20005";',
    "index minute API ID",
)

old_missing = '''            if (rows == nullptr) {
                response.result.ok = false;
                response.result.error =
                    "required response array is missing: " + arrayKey;
                return response;
            }
'''
new_missing = '''            if (rows == nullptr) {
                response.result.ok = false;

                if (
                    instrument == MinuteBarInstrument::Stock &&
                    parsed.value.Find("stk_tic_chart_qry") != nullptr)
                {
                    response.result.error =
                        "received stock tick-chart response from api-id ka10079; "
                        "stock minute bars require api-id ka10080";
                    return response;
                }

                if (
                    instrument == MinuteBarInstrument::Index &&
                    (
                        parsed.value.Find("inds_tic_pole_qry") != nullptr ||
                        parsed.value.Find("inds_tic_chart_qry") != nullptr))
                {
                    response.result.error =
                        "received index tick-chart response from api-id ka20004; "
                        "index minute bars require api-id ka20005";
                    return response;
                }

                std::ostringstream message;
                message
                    << "required response array is missing: "
                    << arrayKey
                    << "; response keys=";

                bool firstKey = true;
                for (const auto& entry : parsed.value.AsObject()) {
                    if (!firstKey) message << ',';
                    message << entry.first;
                    firstKey = false;
                }

                response.result.error = message.str();
                return response;
            }
'''
core = replace_once(
    core,
    old_missing,
    new_missing,
    "missing array diagnostics",
)
CORE.write_text("\ufeff" + core, encoding="utf-8")


tests = TESTS.read_text(encoding="utf-8-sig")
tests = replace_once(
    tests,
    'Check(request.apiId == "ka10079", "stock minute api-id mismatch");',
    'Check(request.apiId == "ka10080", "stock minute api-id mismatch");',
    "stock request test API ID",
)
tests = replace_once(
    tests,
    'Check(request.apiId == "ka20004", "index minute api-id mismatch");',
    'Check(request.apiId == "ka20005", "index minute api-id mismatch");',
    "index request test API ID",
)

anchor = '''        Check(missing.result.error.find("stk_min_pole_chart_qry") != std::string::npos,
              "missing array error must name the field");

'''
addition = anchor + '''        const trading::MinuteBarsPage wrongApi =
            trading::ParseStockMinuteBarsResponse(
                "005930",
                1,
                "{\"return_code\":0,\"stk_tic_chart_qry\":[]}");
        Check(!wrongApi.result.ok,
              "tick-chart response must not be accepted as minute bars");
        Check(wrongApi.result.error.find("ka10080") != std::string::npos,
              "wrong API-ID error must identify ka10080");

'''
tests = replace_once(
    tests,
    anchor,
    addition,
    "wrong API response regression test",
)
TESTS.write_text("\ufeff" + tests, encoding="utf-8")

print("Corrected stock/index minute API IDs and added wrong-response diagnostics")
