#include "value_grid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace trading::render
{
    bool ValidateValueGrid(
        const ValueGrid& grid,
        const char** error) noexcept
    {
        const auto fail = [error](const char* message) noexcept {
            if (error != nullptr) *error = message;
            return false;
        };

        if (!grid.enabled) {
            if (error != nullptr) *error = nullptr;
            return true;
        }
        if (!std::isfinite(grid.fallbackStep) || grid.fallbackStep < 0.0) {
            return fail("value-grid fallback step is invalid");
        }

        double previousUpper = 0.0;
        for (std::size_t index = 0; index < grid.bands.size(); ++index) {
            const ValueGridBand& band = grid.bands[index];
            if (!std::isfinite(band.upperExclusive) ||
                !std::isfinite(band.step) ||
                band.upperExclusive <= previousUpper ||
                band.step <= 0.0)
            {
                return fail("value-grid bands must be increasing and positive");
            }
            previousUpper = band.upperExclusive;
        }

        if (grid.bands.empty() && grid.fallbackStep <= 0.0) {
            return fail("enabled value-grid requires a positive step");
        }
        if (!grid.bands.empty() && grid.fallbackStep <= 0.0) {
            return fail("banded value-grid requires a positive fallback step");
        }

        if (error != nullptr) *error = nullptr;
        return true;
    }

    double ValueGridStep(
        const ValueGrid& grid,
        double value) noexcept
    {
        if (!grid.enabled || !std::isfinite(value)) return 0.0;
        const double magnitude = std::fabs(value);
        for (const ValueGridBand& band : grid.bands) {
            if (magnitude < band.upperExclusive) return band.step;
        }
        return grid.fallbackStep;
    }

    double QuantizeValue(
        const ValueGrid& grid,
        double value) noexcept
    {
        if (!std::isfinite(value)) return value;
        const double step = ValueGridStep(grid, value);
        if (!std::isfinite(step) || step <= 0.0) return value;
        const double quantized = std::round(value / step) * step;
        return std::fabs(quantized) < step * 0.5 ? 0.0 : quantized;
    }
}
