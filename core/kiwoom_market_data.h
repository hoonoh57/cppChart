#pragma once

#include "kiwoom_protocol.h"
#include "kiwoom_reconciliation.h"
#include "market_types.h"

#include <string>
#include <vector>

namespace trading
{
    enum class MinuteBarInstrument
    {
        Stock,
        Index
    };

    struct MinuteBarsPage final
    {
        ProtocolResult result;
        MinuteBarInstrument instrument = MinuteBarInstrument::Stock;
        std::string code;
        int minuteUnit = 1;
        std::vector<Bar> bars;
    };

    struct StockTradeTick final
    {
        std::string code;
        PriceWon priceWon = 0;
        Volume tradeVolume = 0;
        Volume cumulativeVolume = 0;
        int tradeTimeHhmmss = 0;
    };

    struct StockTradeDecodeResult final
    {
        ProtocolResult result;
        StockTradeTick tick;
    };

    bool IsSupportedMinuteUnit(int minuteUnit) noexcept;

    RestRequest BuildStockMinuteBarsRestRequest(
        const std::string& stockCode,
        int minuteUnit,
        const std::string& bearerToken,
        bool adjustedPrice,
        const Continuation& continuation,
        std::string& error);

    RestRequest BuildIndexMinuteBarsRestRequest(
        const std::string& indexCode,
        int minuteUnit,
        const std::string& bearerToken,
        const Continuation& continuation,
        std::string& error);

    MinuteBarsPage ParseStockMinuteBarsResponse(
        const std::string& stockCode,
        int minuteUnit,
        const std::string& json);

    MinuteBarsPage ParseIndexMinuteBarsResponse(
        const std::string& indexCode,
        int minuteUnit,
        const std::string& json);


    StockTradeDecodeResult DecodeStockTradeRecord(
        const RealTimeRecord& record);

    bool MergeStockTradeIntoMinuteBars(
        std::vector<Bar>& bars,
        int minuteUnit,
        EpochMillis sessionDateStartMs,
        const StockTradeTick& tick,
        std::string& error);
}
