#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "../core/stock_pool_engine.h"
#include "../platform/stock_pool_minute_client.h"

namespace trading::stock_pool::hydration
{
    struct HistoricalHydrationResult final
    {
        bool ok = false;
        std::vector<MemberSeries> members;
        std::size_t barsPerMember = 0U;
        std::size_t carriedForwardBarCount = 0U;
        EpochMillis firstTimestamp = 0;
        EpochMillis lastTimestamp = 0;
        std::string source;
        std::string error;
    };

    inline std::string DigitsOnly(const std::string& value)
    {
        std::string result;
        result.reserve(value.size());
        for (unsigned char character : value) {
            if (character >= '0' && character <= '9') {
                result.push_back(static_cast<char>(character));
            }
        }
        return result;
    }

    inline bool PackedTimestampParts(
        EpochMillis timestamp,
        long long& date,
        int& minuteOfDay,
        int& second)
    {
        const long long packed = timestamp / 1000LL;
        if (packed <= 0LL) return false;
        second = static_cast<int>(packed % 100LL);
        const int minute = static_cast<int>((packed / 100LL) % 100LL);
        const int hour = static_cast<int>((packed / 10000LL) % 100LL);
        date = packed / 1000000LL;
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
            second < 0 || second > 59)
        {
            return false;
        }
        minuteOfDay = hour * 60 + minute;
        return true;
    }

    inline EpochMillis PackMinuteTimestamp(long long date, int minuteOfDay)
    {
        const int hour = minuteOfDay / 60;
        const int minute = minuteOfDay % 60;
        const long long packed =
            (((date * 100LL + hour) * 100LL + minute) * 100LL);
        return static_cast<EpochMillis>(packed) * 1000LL;
    }

    inline HistoricalHydrationResult HydrateHistoricalMembers(
        const std::vector<MemberSeries>& cohortMembers,
        const std::string& tradingDate,
        const std::string& captureTime,
        int minimumHistoryBars,
        const std::string& environmentFilePath)
    {
        HistoricalHydrationResult result;
        if (cohortMembers.empty()) {
            result.error = "Frozen Cohort가 비어 있습니다.";
            return result;
        }

        const std::string dateDigits = DigitsOnly(tradingDate);
        std::string timeDigits = DigitsOnly(captureTime);
        if (dateDigits.size() != 8U) {
            result.error = "거래일자는 YYYY-MM-DD 또는 YYYYMMDD 형식이어야 합니다.";
            return result;
        }
        if (timeDigits.size() == 4U) timeDigits += "00";
        if (timeDigits.size() != 6U) {
            result.error = "포착시각은 HH:mm 또는 HHmmss 형식이어야 합니다.";
            return result;
        }

        const long long expectedDate = std::strtoll(
            dateDigits.c_str(), nullptr, 10);
        const int captureHour = std::atoi(timeDigits.substr(0U, 2U).c_str());
        const int captureMinute = std::atoi(timeDigits.substr(2U, 2U).c_str());
        const int captureSecond = std::atoi(timeDigits.substr(4U, 2U).c_str());
        if (captureHour < 0 || captureHour > 23 ||
            captureMinute < 0 || captureMinute > 59 ||
            captureSecond != 0)
        {
            result.error = "포착시각은 유효한 정분 시각이어야 합니다.";
            return result;
        }
        const int captureMinuteOfDay = captureHour * 60 + captureMinute;

        std::vector<MemberSeries> fetchedMembers;
        fetchedMembers.reserve(cohortMembers.size());
        int commonStartMinute = captureMinuteOfDay;
        int commonEndMinute = 24 * 60 - 1;
        bool firstMember = true;
        std::string source;

        for (const MemberSeries& metadata : cohortMembers) {
            const auto fetched =
                platform::FetchMinuteSeriesViaServer32(
                    metadata.code,
                    tradingDate,
                    captureTime,
                    environmentFilePath);
            if (!fetched.ok) {
                result.error = metadata.code + " " + metadata.name +
                    " 분봉 조회 실패: " + fetched.error;
                return result;
            }

            MemberSeries member;
            member.code = metadata.code;
            member.name = metadata.name;
            member.market = metadata.market;
            member.bars = fetched.bars;
            std::sort(
                member.bars.begin(),
                member.bars.end(),
                [](const Bar& left, const Bar& right) {
                    return left.closeTimestampMs < right.closeTimestampMs;
                });

            if (member.bars.empty()) {
                result.error = metadata.code + " " + metadata.name +
                    " 분봉 응답이 비어 있습니다.";
                return result;
            }

            int firstMinute = -1;
            int lastMinute = -1;
            for (const Bar& bar : member.bars) {
                long long date = 0LL;
                int minuteOfDay = 0;
                int second = 0;
                if (!PackedTimestampParts(
                        bar.closeTimestampMs,
                        date,
                        minuteOfDay,
                        second) ||
                    date != expectedDate || second != 0)
                {
                    result.error = metadata.code +
                        " 분봉 timestamp 형식 또는 거래일 불변식이 깨졌습니다: " +
                        std::to_string(bar.closeTimestampMs);
                    return result;
                }
                if (minuteOfDay < captureMinuteOfDay) continue;
                if (firstMinute < 0) firstMinute = minuteOfDay;
                lastMinute = minuteOfDay;
            }

            if (firstMinute < 0 || lastMinute < firstMinute) {
                result.error = metadata.code + " " + metadata.name +
                    " 포착시각 이후 유효한 실제 분봉이 없습니다.";
                return result;
            }

            if (firstMember) {
                commonStartMinute = firstMinute;
                commonEndMinute = lastMinute;
                firstMember = false;
            }
            else {
                commonStartMinute = (std::max)(commonStartMinute, firstMinute);
                commonEndMinute = (std::min)(commonEndMinute, lastMinute);
            }

            fetchedMembers.push_back(std::move(member));
            source = fetched.source;
        }

        minimumHistoryBars = (std::max)(1, minimumHistoryBars);
        if (commonEndMinute < commonStartMinute) {
            result.error =
                "모든 종목이 동시에 평가 가능한 공통 분봉 구간이 없습니다.";
            return result;
        }

        const std::size_t timelineCount = static_cast<std::size_t>(
            commonEndMinute - commonStartMinute + 1);
        if (timelineCount < static_cast<std::size_t>(minimumHistoryBars)) {
            result.error =
                "공통 평가 구간이 " + std::to_string(timelineCount) +
                "분뿐입니다. 최소 " +
                std::to_string(minimumHistoryBars) + "분이 필요합니다.";
            return result;
        }

        std::size_t carriedForward = 0U;
        for (MemberSeries& member : fetchedMembers) {
            std::map<int, Bar> observedByMinute;
            for (const Bar& bar : member.bars) {
                long long date = 0LL;
                int minuteOfDay = 0;
                int second = 0;
                if (PackedTimestampParts(
                        bar.closeTimestampMs,
                        date,
                        minuteOfDay,
                        second) &&
                    date == expectedDate && second == 0 &&
                    minuteOfDay >= captureMinuteOfDay)
                {
                    observedByMinute[minuteOfDay] = bar;
                }
            }

            std::vector<Bar> synchronized;
            synchronized.reserve(timelineCount);
            Bar lastKnown;
            bool hasLastKnown = false;

            const auto firstUsable = observedByMinute.upper_bound(commonStartMinute);
            if (firstUsable != observedByMinute.begin()) {
                auto previous = firstUsable;
                --previous;
                lastKnown = previous->second;
                hasLastKnown = true;
            }

            for (int minute = commonStartMinute;
                 minute <= commonEndMinute;
                 ++minute)
            {
                const auto observed = observedByMinute.find(minute);
                if (observed != observedByMinute.end()) {
                    lastKnown = observed->second;
                    hasLastKnown = true;
                    synchronized.push_back(lastKnown);
                    continue;
                }

                if (!hasLastKnown || lastKnown.close <= 0.0) {
                    result.error = member.code +
                        " 공통 시작시각 이전의 인과적 기준가격이 없습니다.";
                    return result;
                }

                Bar noTrade = lastKnown;
                noTrade.closeTimestampMs =
                    PackMinuteTimestamp(expectedDate, minute);
                noTrade.open = lastKnown.close;
                noTrade.high = lastKnown.close;
                noTrade.low = lastKnown.close;
                noTrade.close = lastKnown.close;
                noTrade.tradeIntensity = 100.0;
                synchronized.push_back(noTrade);
                lastKnown = noTrade;
                ++carriedForward;
            }

            if (synchronized.size() != timelineCount) {
                result.error = member.code +
                    " 동기화 분봉 수가 공통 timeline과 일치하지 않습니다.";
                return result;
            }
            member.bars = std::move(synchronized);
        }

        result.members = std::move(fetchedMembers);
        result.barsPerMember = timelineCount;
        result.carriedForwardBarCount = carriedForward;
        result.firstTimestamp =
            PackMinuteTimestamp(expectedDate, commonStartMinute);
        result.lastTimestamp =
            PackMinuteTimestamp(expectedDate, commonEndMinute);
        result.source = source +
            " | alignment=causal-no-trade-carry | carried=" +
            std::to_string(carriedForward);
        result.ok = true;
        return result;
    }
}
