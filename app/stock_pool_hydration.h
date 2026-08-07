#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <set>
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
        const long long expectedFirstPacked = std::strtoll(
            (dateDigits + timeDigits).c_str(), nullptr, 10);

        std::vector<MemberSeries> fetchedMembers;
        fetchedMembers.reserve(cohortMembers.size());
        std::set<EpochMillis> commonTimestamps;
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
            fetchedMembers.push_back(std::move(member));
            source = fetched.source;

            std::set<EpochMillis> timestamps;
            for (const Bar& bar : fetched.bars) {
                timestamps.insert(bar.closeTimestampMs);
            }
            if (firstMember) {
                commonTimestamps = std::move(timestamps);
                firstMember = false;
            }
            else {
                for (auto iterator = commonTimestamps.begin();
                     iterator != commonTimestamps.end();)
                {
                    if (timestamps.find(*iterator) == timestamps.end()) {
                        iterator = commonTimestamps.erase(iterator);
                    }
                    else {
                        ++iterator;
                    }
                }
            }
            if (commonTimestamps.empty()) {
                result.error =
                    "종목 간 공통 1분봉 시각이 없습니다. 시계열을 합성하지 않고 중단했습니다.";
                return result;
            }
        }

        minimumHistoryBars = (std::max)(1, minimumHistoryBars);
        if (commonTimestamps.size() <
            static_cast<std::size_t>(minimumHistoryBars))
        {
            result.error =
                "공통 실제 분봉이 " +
                std::to_string(commonTimestamps.size()) +
                "개뿐입니다. 최소 " +
                std::to_string(minimumHistoryBars) +
                "개가 필요합니다.";
            return result;
        }

        const long long actualFirstPacked =
            *commonTimestamps.begin() / 1000LL;
        if (actualFirstPacked != expectedFirstPacked) {
            result.error =
                "포착시각 첫 봉이 모든 종목에 공통으로 존재하지 않습니다. expected=" +
                std::to_string(expectedFirstPacked) + " actual=" +
                std::to_string(actualFirstPacked) +
                ". 가짜 봉을 만들지 않고 중단했습니다.";
            return result;
        }

        int previousMinute = -1;
        for (EpochMillis timestamp : commonTimestamps) {
            long long date = 0LL;
            int minuteOfDay = 0;
            int second = 0;
            if (!PackedTimestampParts(
                    timestamp,
                    date,
                    minuteOfDay,
                    second) ||
                date != expectedDate || second != 0)
            {
                result.error =
                    "분봉 timestamp 형식 또는 거래일 불변식이 깨졌습니다: " +
                    std::to_string(timestamp);
                return result;
            }
            if (previousMinute >= 0 && minuteOfDay != previousMinute + 1) {
                result.error =
                    "공통 분봉 시계열이 연속 1분 간격이 아닙니다: " +
                    std::to_string(previousMinute) + " -> " +
                    std::to_string(minuteOfDay) +
                    ". 누락 봉을 합성하지 않고 중단했습니다.";
                return result;
            }
            previousMinute = minuteOfDay;
        }

        for (MemberSeries& member : fetchedMembers) {
            std::vector<Bar> synchronized;
            synchronized.reserve(commonTimestamps.size());
            for (const Bar& bar : member.bars) {
                if (commonTimestamps.find(bar.closeTimestampMs) !=
                    commonTimestamps.end())
                {
                    synchronized.push_back(bar);
                }
            }
            if (synchronized.size() != commonTimestamps.size()) {
                result.error = member.code +
                    " 동기화 분봉 수가 공통 timeline과 일치하지 않습니다.";
                return result;
            }
            member.bars = std::move(synchronized);
        }

        result.members = std::move(fetchedMembers);
        result.barsPerMember = commonTimestamps.size();
        result.firstTimestamp = *commonTimestamps.begin();
        result.lastTimestamp = *commonTimestamps.rbegin();
        result.source = source;
        result.ok = true;
        return result;
    }
}
