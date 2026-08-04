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
    record.type = "0I";
    record.item = "001";
    record.values["20"] = "101530";
    record.values["10"] = "+2,845.67";
    record.values["15"] = "15";
    record.values["13"] = "124500";

    trading::IndexValueDecodeResult decimal =
        trading::DecodeIndexValueRecord(record);
    Check(!decimal.result.ok,
          "decimal index payload must fail until normalized integer contract is supplied");

    record.values["10"] = "+284567";
    trading::IndexValueDecodeResult decoded =
        trading::DecodeIndexValueRecord(record);
    Check(decoded.result.ok, "valid 0I index record must decode");
    Check(decoded.tick.code == "001", "index code mismatch");
    Check(decoded.tick.value == 284567, "index value mismatch");
    Check(decoded.tick.tradeTimeHhmmss == 101530,
          "index trade time mismatch");
    Check(decoded.tick.tradeVolume == 15,
          "index trade volume mismatch");
    Check(decoded.tick.cumulativeVolume == 124500,
          "index cumulative volume mismatch");

    record.type = "0B";
    Check(!trading::DecodeIndexValueRecord(record).result.ok,
          "non-0I record must fail closed");

    std::puts("[PASS] kiwoom_index_realtime_tests");
    return 0;
}
