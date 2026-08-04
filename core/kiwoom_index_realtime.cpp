#include "kiwoom_index_realtime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace trading
{
    namespace
    {
        std::string NormalizeNumber(std::string value)
        {
            value.erase(
                std::remove(value.begin(), value.end(), ','),
                value.end());
            while (!value.empty() &&
                   std::isspace(
                       static_cast<unsigned char>(value.front())) != 0)
            {
                value.erase(value.begin());
            }
            while (!value.empty() &&
                   std::isspace(
                       static_cast<unsigned char>(value.back())) != 0)
            {
                value.pop_back();
            }
            return value;
        }

        bool ParseAbsoluteInteger(
            const std::string& text,
            std::int64_t& value) noexcept
        {
            try {
                const std::string normalized = NormalizeNumber(text);
                if (normalized.empty()) return false;
                std::size_t consumed = 0;
                const long long parsed =
                    std::stoll(normalized, &consumed, 10);
                if (consumed != normalized.size() ||
                    parsed == (std::numeric_limits<long long>::min)())
                {
                    return false;
                }
                value = parsed < 0 ? -parsed : parsed;
                return true;
            }
            catch (...) {
                return false;
            }
        }

        bool ParseScaledIndexValue(
            const std::string& text,
            std::int64_t& value) noexcept
        {
            try {
                const std::string normalized = NormalizeNumber(text);
                if (normalized.empty()) return false;
                if (normalized.find('.') == std::string::npos) {
                    return ParseAbsoluteInteger(normalized, value);
                }

                std::size_t consumed = 0;
                const double parsed =
                    std::stod(normalized, &consumed);
                if (consumed != normalized.size() ||
                    !std::isfinite(parsed))
                {
                    return false;
                }
                const double absolute = std::fabs(parsed);
                if (absolute >
                    static_cast<double>(
                        (std::numeric_limits<std::int64_t>::max)()) /
                        100.0)
                {
                    return false;
                }
                value = static_cast<std::int64_t>(
                    std::llround(absolute * 100.0));
                return true;
            }
            catch (...) {
                return false;
            }
        }

        bool ReadRequiredInteger(
            const RealTimeRecord& record,
            const char* key,
            std::int64_t& value,
            std::string& error)
        {
            const auto found = record.values.find(key);
            if (found == record.values.end() ||
                !ParseAbsoluteInteger(found->second, value))
            {
                error = std::string("index realtime field is invalid: ") + key;
                return false;
            }
            return true;
        }

        bool ReadRequiredIndexValue(
            const RealTimeRecord& record,
            const char* key,
            std::int64_t& value,
            std::string& error)
        {
            const auto found = record.values.find(key);
            if (found == record.values.end() ||
                !ParseScaledIndexValue(found->second, value))
            {
                error = std::string("index realtime field is invalid: ") + key;
                return false;
            }
            return true;
        }

        std::int64_t ReadOptionalInteger(
            const RealTimeRecord& record,
            const char* key) noexcept
        {
            const auto found = record.values.find(key);
            std::int64_t value = 0;
            return found != record.values.end() &&
                   ParseAbsoluteInteger(found->second, value)
                ? value
                : 0;
        }
    }

    IndexValueDecodeResult DecodeIndexValueRecord(
        const RealTimeRecord& record)
    {
        IndexValueDecodeResult result;
        if (record.type != "0I") {
            result.result.error = "realtime record is not index type 0I";
            return result;
        }

        result.tick.code = record.item;
        if (!result.tick.code.empty() && result.tick.code.front() == 'A') {
            result.tick.code.erase(result.tick.code.begin());
        }
        if (result.tick.code.empty()) {
            result.result.error = "index realtime code is empty";
            return result;
        }

        std::int64_t time = 0;
        std::int64_t value = 0;
        if (!ReadRequiredInteger(
                record, "20", time, result.result.error) ||
            !ReadRequiredIndexValue(
                record, "10", value, result.result.error))
        {
            return result;
        }
        if (time < 0 || time > 235959 ||
            value <= 0 ||
            value > (std::numeric_limits<PriceWon>::max)())
        {
            result.result.error = "index realtime value is outside range";
            return result;
        }

        const std::int64_t tradeVolume =
            ReadOptionalInteger(record, "15");
        const std::int64_t cumulativeVolume =
            ReadOptionalInteger(record, "13");
        result.tick.tradeTimeHhmmss = static_cast<int>(time);
        result.tick.value = static_cast<PriceWon>(value);
        result.tick.tradeVolume = tradeVolume;
        result.tick.cumulativeVolume = cumulativeVolume;
        result.result.ok = true;
        result.result.returnCode = 0;
        return result;
    }
}
