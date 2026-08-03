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
    using TradingDateYmd = std::int32_t;
    using Volume = std::int64_t;
    using MoneyWon = std::int64_t;

    inline bool IsLeapYear(int year) noexcept
    {
        return
            (year % 4 == 0 && year % 100 != 0) ||
            year % 400 == 0;
    }

    inline int DaysInMonth(int year, int month) noexcept
    {
        static constexpr int days[] = {
            31, 28, 31, 30, 31, 30,
            31, 31, 30, 31, 30, 31};
        if (month < 1 || month > 12) return 0;
        if (month == 2 && IsLeapYear(year)) return 29;
        return days[month - 1];
    }

    inline bool IsValidTradingDateYmd(TradingDateYmd value) noexcept
    {
        const int year = value / 10000;
        const int month = (value / 100) % 100;
        const int day = value % 100;
        return
            year >= 1970 && year <= 9999 &&
            month >= 1 && month <= 12 &&
            day >= 1 && day <= DaysInMonth(year, month);
    }

    inline TradingDateYmd KstTradingDateYmdFromEpoch(
        EpochMillis timestampMs) noexcept
    {
        constexpr EpochMillis DayMs = 86400000LL;
        constexpr EpochMillis KstOffsetMs = 32400000LL;
        if (timestampMs <= 0 ||
            timestampMs > (std::numeric_limits<EpochMillis>::max)() -
                KstOffsetMs)
        {
            return 0;
        }

        std::int64_t days =
            (timestampMs + KstOffsetMs) / DayMs;
        days += 719468;
        const std::int64_t era =
            (days >= 0 ? days : days - 146096) / 146097;
        const unsigned dayOfEra = static_cast<unsigned>(
            days - era * 146097);
        const unsigned yearOfEra =
            (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 -
                dayOfEra / 146096) /
            365;
        int year = static_cast<int>(yearOfEra) +
            static_cast<int>(era * 400);
        const unsigned dayOfYear =
            dayOfEra -
            (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
        const int monthPrime =
            static_cast<int>((5 * dayOfYear + 2) / 153);
        const int day = static_cast<int>(
            dayOfYear -
            (153U * static_cast<unsigned>(monthPrime) + 2U) / 5U +
            1U);
        const int month =
            monthPrime + (monthPrime < 10 ? 3 : -9);
        year += month <= 2;

        const TradingDateYmd result = static_cast<TradingDateYmd>(
            year * 10000 + month * 100 + day);
        return IsValidTradingDateYmd(result) ? result : 0;
    }

    struct Bar final
    {
        PriceWon open = 0;
        PriceWon high = 0;
        PriceWon low = 0;
        PriceWon close = 0;
        Volume volume = 0;
        EpochMillis closeTimestampMs = 0;
        TickCount tickCount = 0;
        TradingDateYmd tradingDateYmd = 0;
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
