#pragma once

#include <vector>

namespace trading::render
{
    struct ValueGridBand final
    {
        double upperExclusive = 0.0;
        double step = 0.0;
    };

    struct ValueGrid final
    {
        bool enabled = false;
        double fallbackStep = 0.0;
        std::vector<ValueGridBand> bands;
    };

    bool ValidateValueGrid(
        const ValueGrid& grid,
        const char** error = nullptr) noexcept;

    double ValueGridStep(
        const ValueGrid& grid,
        double value) noexcept;

    double QuantizeValue(
        const ValueGrid& grid,
        double value) noexcept;
}
