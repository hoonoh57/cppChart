// Higher-order tick-candle adapter for the stock-pool workbench.
//
// CYBOS StockChart accepts tick periods only up to T120.  The workbench
// intentionally exposes T60/T120/T180/T360/T720.  Sizes above T120 are
// therefore built deterministically from completed real CYBOS base candles:
//
//   T180 = T60  x 3
//   T360 = T120 x 3
//   T720 = T120 x 6
//
// The user-selected trading date remains authoritative for the target session.
// Prior-session data is used only for indicator warm-up.  Groups never cross a
// session boundary and incomplete groups are never published.

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

// Reuse the proven HTTP/JSON/CYBOS adapter, but rename only its public entry
// inside this translation unit so this file can wrap it without duplicating
// the implementation or changing the server32 contract.
#define FetchTickSeriesViaServer32 FetchNativeTickSeriesViaServer32
#include "stock_pool_tick_client.cpp"
#undef FetchTickSeriesViaServer32

namespace trading::stock_pool::platform
{
    namespace
    {
        struct TickDerivationPlan final
        {
            int nativeTickSize = 0;
            int factor = 0;
        };

        TickDerivationPlan MakeDerivationPlan(int requestedTickSize)
        {
            switch (requestedTickSize) {
            case 60:
                return TickDerivationPlan{60, 1};
            case 120:
                return TickDerivationPlan{120, 1};
            case 180:
                return TickDerivationPlan{60, 3};
            case 360:
                return TickDerivationPlan{120, 3};
            case 720:
                return TickDerivationPlan{120, 6};
            default:
                return {};
            }
        }

        std::string PackedDate(EpochMillis timestamp)
        {
            if (timestamp <= 0) return {};
            const long long packed = timestamp / 1000LL;
            const long long yyyymmdd = packed / 1000000LL;
            const std::string value = std::to_string(yyyymmdd);
            return value.size() == 8U ? value : std::string{};
        }

        Bar MergeCompletedGroup(
            const std::vector<Bar>& source,
            std::size_t begin,
            std::size_t end,
            int targetTickSize)
        {
            Bar result = source[begin];
            double intensityTotal = 0.0;
            for (std::size_t index = begin; index < end; ++index) {
                const Bar& input = source[index];
                result.high = (std::max)(result.high, input.high);
                result.low = (std::min)(result.low, input.low);
                result.close = input.close;
                result.closeTimestampMs = input.closeTimestampMs;
                result.cumulativeTurnover = input.cumulativeTurnover;
                intensityTotal += input.tradeIntensity;
            }
            result.tradeIntensity = intensityTotal /
                static_cast<double>(end - begin);
            result.tickCount = targetTickSize;
            result.tickDurationSeconds = 0.0;
            result.tickRatePerSecond = 0.0;
            return result;
        }

        std::vector<Bar> AggregateFromSessionStart(
            const std::vector<Bar>& source,
            int factor,
            int targetTickSize)
        {
            std::vector<Bar> result;
            if (factor <= 0) return result;
            const std::size_t width = static_cast<std::size_t>(factor);
            result.reserve(source.size() / width);
            for (std::size_t begin = 0U;
                 begin + width <= source.size();
                 begin += width)
            {
                result.push_back(MergeCompletedGroup(
                    source,
                    begin,
                    begin + width,
                    targetTickSize));
            }
            return result;
        }

        std::vector<Bar> AggregateWarmupTail(
            const std::vector<Bar>& source,
            int factor,
            int targetTickSize,
            std::size_t requestedBars)
        {
            std::vector<Bar> result;
            if (factor <= 0 || source.empty() || requestedBars == 0U) {
                return result;
            }

            const std::size_t width = static_cast<std::size_t>(factor);
            const std::size_t fullGroups = source.size() / width;
            const std::size_t first = source.size() - fullGroups * width;
            result.reserve(fullGroups);
            for (std::size_t begin = first;
                 begin + width <= source.size();
                 begin += width)
            {
                result.push_back(MergeCompletedGroup(
                    source,
                    begin,
                    begin + width,
                    targetTickSize));
            }

            if (result.size() > requestedBars) {
                result.erase(
                    result.begin(),
                    result.end() - static_cast<std::ptrdiff_t>(requestedBars));
            }
            return result;
        }
    }

    TickSeriesFetchResult FetchTickSeriesViaServer32(
        const std::string& code,
        const std::string& tradingDate,
        int tickSize,
        const std::string& analysisEndTime,
        std::size_t warmupBars,
        const std::string& environmentFilePath)
    {
        const TickDerivationPlan plan = MakeDerivationPlan(tickSize);
        if (plan.nativeTickSize <= 0 || plan.factor <= 0) {
            TickSeriesFetchResult rejected;
            rejected.tickSize = tickSize;
            rejected.error =
                "지원 틱봉은 T60/T120/T180/T360/T720 중 하나여야 합니다.";
            return rejected;
        }

        if (plan.factor == 1) {
            return FetchNativeTickSeriesViaServer32(
                code,
                tradingDate,
                plan.nativeTickSize,
                analysisEndTime,
                warmupBars,
                environmentFilePath);
        }

        // Ask the native adapter for enough prior-session base bars that the
        // requested number of completed higher-order warm-up bars can always
        // be formed from the tail.  The extra factor-1 bars absorb an unknown
        // alignment at the beginning of the returned tail.
        const std::size_t nativeWarmupBars =
            warmupBars * static_cast<std::size_t>(plan.factor) +
            static_cast<std::size_t>(plan.factor - 1);

        TickSeriesFetchResult native = FetchNativeTickSeriesViaServer32(
            code,
            tradingDate,
            plan.nativeTickSize,
            analysisEndTime,
            nativeWarmupBars,
            environmentFilePath);
        if (!native.ok) {
            native.tickSize = tickSize;
            native.error =
                "T" + std::to_string(tickSize) +
                " 파생용 T" + std::to_string(plan.nativeTickSize) +
                " 원천봉 조회 실패 | " + native.error;
            return native;
        }

        std::string targetDate;
        for (unsigned char character : tradingDate) {
            if (character >= '0' && character <= '9') {
                targetDate.push_back(static_cast<char>(character));
            }
        }

        std::vector<Bar> previousSession;
        std::vector<Bar> targetSession;
        previousSession.reserve(native.warmupRowCount);
        targetSession.reserve(native.sessionRowCount);
        for (const Bar& bar : native.bars) {
            const std::string barDate = PackedDate(bar.closeTimestampMs);
            if (!native.previousTradingDate.empty() &&
                barDate == native.previousTradingDate)
            {
                previousSession.push_back(bar);
            }
            else if (barDate == targetDate) {
                targetSession.push_back(bar);
            }
        }

        std::vector<Bar> warmup = AggregateWarmupTail(
            previousSession,
            plan.factor,
            tickSize,
            warmupBars);
        std::vector<Bar> session = AggregateFromSessionStart(
            targetSession,
            plan.factor,
            tickSize);

        TickSeriesFetchResult result;
        result.tickSize = tickSize;
        result.responseRowCount = native.responseRowCount;
        result.previousTradingDate = native.previousTradingDate;
        result.source =
            native.source + " | derived T" + std::to_string(tickSize) +
            " from completed T" + std::to_string(plan.nativeTickSize) +
            " x" + std::to_string(plan.factor);

        if (warmup.empty()) {
            result.error =
                code + " | T" + std::to_string(tickSize) +
                " 전일 warm-up 파생봉을 만들 수 없습니다.";
            return result;
        }
        if (session.empty()) {
            result.error =
                code + " | 사용자 지정일 " + targetDate +
                "에 완료된 T" + std::to_string(tickSize) +
                " 파생봉이 없습니다.";
            return result;
        }

        result.warmupRowCount = warmup.size();
        result.sessionRowCount = session.size();
        result.bars.reserve(warmup.size() + session.size());
        result.bars.insert(result.bars.end(), warmup.begin(), warmup.end());
        result.bars.insert(result.bars.end(), session.begin(), session.end());
        result.acceptedRowCount = result.bars.size();
        result.ok = true;
        return result;
    }
}
