#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace trading
{
    using EpochMillis = std::int64_t;
    using PriceWon = std::int32_t;
    using Quantity = std::int32_t;
    using TickCount = std::int32_t;
    using Volume = std::int64_t;
    using MoneyWon = std::int64_t;

    struct Bar final
    {
        PriceWon open = 0;
        PriceWon high = 0;
        PriceWon low = 0;
        PriceWon close = 0;
        Volume volume = 0;
        EpochMillis closeTimestampMs = 0;
        TickCount tickCount = 0;
    };

    struct PositionSnapshot final
    {
        std::string code;
        std::string name;
        Quantity quantity = 0;
        MoneyWon costBasisWon = 0;
        PriceWon currentPriceWon = 0;
        bool selected = false;

        double AveragePriceWon() const noexcept
        {
            return quantity > 0
                ? static_cast<double>(costBasisWon) /
                    static_cast<double>(quantity)
                : 0.0;
        }

        MoneyWon EvaluationWon() const noexcept
        {
            return
                static_cast<MoneyWon>(currentPriceWon) *
                static_cast<MoneyWon>(quantity);
        }

        MoneyWon UnrealizedPnlWon() const noexcept
        {
            return EvaluationWon() - costBasisWon;
        }
    };

    inline bool IsValidPrice(PriceWon price) noexcept
    {
        return price > 0;
    }

    inline bool IsValidQuantity(Quantity quantity) noexcept
    {
        return quantity > 0;
    }

    inline bool TryCalculateNotional(
        PriceWon price,
        Quantity quantity,
        MoneyWon& out) noexcept
    {
        if (!IsValidPrice(price) || !IsValidQuantity(quantity)) {
            out = 0;
            return false;
        }

        const MoneyWon widePrice = static_cast<MoneyWon>(price);
        const MoneyWon wideQuantity = static_cast<MoneyWon>(quantity);

        if (
            widePrice >
            (std::numeric_limits<MoneyWon>::max)() /
            wideQuantity)
        {
            out = 0;
            return false;
        }

        out = widePrice * wideQuantity;
        return true;
    }
}
