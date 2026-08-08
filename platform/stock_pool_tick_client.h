#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../core/stock_pool_engine.h"

namespace trading::stock_pool::platform
{
    struct TickSeriesFetchResult final
    {
        bool ok = false;
        std::vector<Bar> bars;
        std::size_t responseRowCount = 0U;
        std::size_t acceptedRowCount = 0U;
        std::size_t warmupRowCount = 0U;
        std::size_t sessionRowCount = 0U;
        int tickSize = 0;
        std::string previousTradingDate;
        std::string source;
        std::string error;
    };

    // Fetches real CYBOS T<n> candles through server32. The returned series
    // contains only (a) the tail of the latest trading session before
    // tradingDate, used strictly for indicator warm-up, and (b) target-day
    // candles from 09:00 through analysisEndTime. No synthetic tick data is
    // generated from minute volume.
    TickSeriesFetchResult FetchTickSeriesViaServer32(
        const std::string& code,
        const std::string& tradingDate,
        int tickSize,
        const std::string& analysisEndTime,
        std::size_t warmupBars,
        const std::string& environmentFilePath);
}
