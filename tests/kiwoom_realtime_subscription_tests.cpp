#include "../core/json_lite.h"
#include "../core/kiwoom_protocol.h"

#include <cstdio>
#include <cstdlib>
#include <string>

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

    void TestStockTradeRemoval()
    {
        const std::string message =
            trading::BuildWebSocketRemovalMessage(
                "2",
                { "000660" },
                { "0B" });

        const json_lite::ParseResult parsed =
            json_lite::Parse(message);
        Check(parsed.ok, "REMOVE message must be valid JSON");
        Check(parsed.value.Find("trnm")->AsString() == "REMOVE",
              "REMOVE transaction name mismatch");
        Check(parsed.value.Find("grp_no")->AsString() == "2",
              "REMOVE group number mismatch");
        Check(parsed.value.Find("refresh") == nullptr,
              "REMOVE message must not contain refresh");

        const json_lite::Value& data =
            parsed.value.Find("data")->AsArray().front();
        Check(data.Find("item")->AsArray().size() == 1,
              "REMOVE item count mismatch");
        Check(data.Find("item")->AsArray().front().AsString() == "000660",
              "REMOVE stock code mismatch");
        Check(data.Find("type")->AsArray().size() == 1,
              "REMOVE type count mismatch");
        Check(data.Find("type")->AsArray().front().AsString() == "0B",
              "REMOVE real-time type mismatch");
    }
}

int main()
{
    TestStockTradeRemoval();
    std::puts("[PASS] kiwoom_realtime_subscription_tests");
    return 0;
}
