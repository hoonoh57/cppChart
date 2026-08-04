#include "../core/kiwoom_index_realtime.h"

#include <cstdio>
#include <cstdlib>

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
    trading::RealTimeRecord record;
    record.type = "0J";
    record.item = "001";
    record.values["20"] = "101530";
    record.values["10"] = "+2,845.67";
    record.values["15"] = "15";
    record.values["13"] = "124500";

    trading::IndexValueDecodeResult decimal =
        trading::DecodeIndexValueRecord(record);
    Check(decimal.result.ok,
          "decimal 0J index value must decode");
    Check(decimal.tick.code == "001",
          "decimal index code mismatch");
    Check(decimal.tick.value == 284567,
          "decimal index value must normalize to x100 integer");
    Check(decimal.tick.tradeTimeHhmmss == 101530,
          "decimal index trade time mismatch");
    Check(decimal.tick.tradeVolume == 15,
          "decimal index trade volume mismatch");
    Check(decimal.tick.cumulativeVolume == 124500,
          "decimal index cumulative volume mismatch");

    record.values["10"] = "+284567";
    trading::IndexValueDecodeResult scaled =
        trading::DecodeIndexValueRecord(record);
    Check(scaled.result.ok,
          "pre-scaled 0J index value must decode");
    Check(scaled.tick.value == decimal.tick.value,
          "decimal and pre-scaled index values must normalize identically");

    record.values["10"] = "+2,845.xx";
    Check(!trading::DecodeIndexValueRecord(record).result.ok,
          "malformed decimal index value must fail closed");

    record.type = "0B";
    record.values["10"] = "+284567";
    Check(!trading::DecodeIndexValueRecord(record).result.ok,
          "non-0J record must fail closed");

    std::puts("[PASS] kiwoom_index_realtime_tests");
    return 0;
}
