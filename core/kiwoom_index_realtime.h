#pragma once

#include "kiwoom_protocol.h"
#include "market_types.h"

#include <string>

namespace trading
{
    struct IndexValueTick final
    {
        std::string code;
        PriceWon value = 0;
        Volume tradeVolume = 0;
        Volume cumulativeVolume = 0;
        int tradeTimeHhmmss = 0;
    };

    struct IndexValueDecodeResult final
    {
        ProtocolResult result;
        IndexValueTick tick;
    };

    IndexValueDecodeResult DecodeIndexValueRecord(
        const RealTimeRecord& record);
}
