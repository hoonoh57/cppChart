#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../core/stock_pool_engine.h"

namespace trading::stock_pool::platform
{
    struct MinuteSeriesFetchResult final
    {
        bool ok = false;
        std::vector<Bar> bars;
        std::size_t responseRowCount = 0U;
        std::size_t acceptedRowCount = 0U;
        std::string source;
        std::string error;
    };

    MinuteSeriesFetchResult FetchMinuteSeriesViaServer32(
        const std::string& code,
        const std::string& tradingDate,
        const std::string& captureTime,
        const std::string& environmentFilePath);
}
