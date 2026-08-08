#pragma once

#include "../core/stock_pool_engine.h"

#include <cstddef>
#include <string>
#include <vector>

namespace trading::stock_pool::evaluation
{
    struct WinnerCapture final
    {
        std::string code;
        std::string name;
        double maximumReturnPercent = 0.0;
        bool captured = false;
        std::size_t firstPublishedSnapshot = 0;
        int firstPublishedMinute = -1;
    };

    struct EvaluationReport final
    {
        int winnerCount = 0;
        int capturedWinnerCount = 0;
        double captureRatePercent = 0.0;
        std::vector<WinnerCapture> winners;
    };

    EvaluationReport EvaluateWinnerCapture(
        const std::vector<MemberSeries>& members,
        const BacktestResult& backtest,
        int winnerK);
}
