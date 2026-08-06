#include "stock_pool_fixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace trading::stock_pool::fixture
{
    namespace
    {
        struct FixtureSpec final
        {
            const char* code;
            const char* name;
            double startPrice;
            double turnoverScale;
        };

        double IdiosyncraticReturn(int memberIndex, int minute)
        {
            switch (memberIndex) {
            case 0: // early leader: strong despite opening market weakness
                if (minute < 18) return 0.0048;
                if (minute < 30) return -0.0012;
                if (minute < 48) return 0.0028;
                if (minute < 75) return 0.0004;
                return 0.0012;
            case 1: // late leader: flat first, then strong recovery/breakout
                if (minute < 28) return 0.0002;
                if (minute < 42) return 0.0012;
                if (minute < 68) return 0.0034;
                if (minute < 82) return -0.0007;
                return 0.0016;
            case 2: // false spike
                if (minute >= 8 && minute <= 11) return 0.0120;
                if (minute > 11 && minute < 24) return -0.0055;
                return -0.0003;
            case 3: // persistent but slower leader
                if (minute < 45) return 0.0017;
                if (minute < 60) return -0.0005;
                return 0.0010;
            case 4:
                return 0.0005 + 0.0008 * std::sin(minute * 0.22);
            case 5:
                return -0.0001 + 0.0010 * std::sin(minute * 0.31 + 1.2);
            case 6:
                return 0.0002 + 0.0007 * std::cos(minute * 0.17);
            case 7:
                return -0.0004 + 0.0006 * std::sin(minute * 0.27 + 2.0);
            case 8:
                return 0.0001 + 0.0005 * std::sin(minute * 0.13 + 0.7);
            case 9:
                return -0.0002 + 0.0009 * std::cos(minute * 0.19 + 0.5);
            case 10:
                if (minute > 72 && minute < 96) return 0.0022;
                return 0.0001;
            default:
                return -0.0001 + 0.0004 * std::sin(minute * 0.11);
            }
        }

        double MarketReturn(int minute)
        {
            if (minute < 15) return -0.0026;
            if (minute < 28) return -0.0008;
            if (minute < 55) return 0.0003;
            if (minute < 78) return -0.0002;
            return 0.0004;
        }

        double TurnoverVelocity(int memberIndex, int minute, double scale)
        {
            double velocity = scale * (1.0 + 0.18 * std::sin(minute * 0.21));
            if (memberIndex == 0 && minute < 50) velocity *= 3.0;
            if (memberIndex == 1 && minute >= 30 && minute < 75) velocity *= 3.4;
            if (memberIndex == 2 && minute >= 8 && minute <= 13) velocity *= 5.0;
            if (memberIndex == 3) velocity *= 1.8;
            if (memberIndex == 10 && minute >= 72 && minute < 96) velocity *= 2.8;
            return (std::max)(1.0, velocity);
        }
    }

    std::vector<MemberSeries> BuildDeterministicFixture()
    {
        static constexpr std::array<FixtureSpec, 12> specs = {{
            {"F0001", "Fixture 조기대장", 10000.0, 120.0},
            {"F0002", "Fixture 후발대장", 18500.0, 105.0},
            {"F0003", "Fixture 순간급등", 7200.0, 90.0},
            {"F0004", "Fixture 지속강자", 13200.0, 100.0},
            {"F0005", "Fixture 순환A", 9600.0, 75.0},
            {"F0006", "Fixture 순환B", 22400.0, 70.0},
            {"F0007", "Fixture 중립A", 8100.0, 68.0},
            {"F0008", "Fixture 약세A", 15300.0, 62.0},
            {"F0009", "Fixture 중립B", 11700.0, 66.0},
            {"F0010", "Fixture 약세B", 5400.0, 58.0},
            {"F0011", "Fixture 오후부상", 19800.0, 82.0},
            {"F0012", "Fixture 기준군", 14600.0, 64.0}
        }};

        constexpr int barCount = 120;
        constexpr EpochMillis baseTimestamp = 1785974400000LL;
        std::vector<MemberSeries> members;
        members.reserve(specs.size());

        for (std::size_t memberIndex = 0U;
             memberIndex < specs.size();
             ++memberIndex)
        {
            const FixtureSpec& spec = specs[memberIndex];
            MemberSeries member;
            member.code = spec.code;
            member.name = spec.name;
            member.market = memberIndex % 2U == 0U ? "KOSPI" : "KOSDAQ";
            member.bars.reserve(barCount);

            double previousClose = spec.startPrice;
            double cumulativeTurnover = 0.0;
            for (int minute = 0; minute < barCount; ++minute) {
                const double rate =
                    MarketReturn(minute) +
                    IdiosyncraticReturn(
                        static_cast<int>(memberIndex),
                        minute);
                const double open = previousClose;
                const double close = (std::max)(100.0, open * (1.0 + rate));
                const double movement = std::abs(rate) + 0.0008;
                const double high =
                    (std::max)(open, close) * (1.0 + movement * 0.45);
                const double low =
                    (std::min)(open, close) * (1.0 - movement * 0.40);
                const double velocity = TurnoverVelocity(
                    static_cast<int>(memberIndex),
                    minute,
                    spec.turnoverScale);
                cumulativeTurnover += velocity;

                Bar bar;
                bar.closeTimestampMs =
                    baseTimestamp +
                    static_cast<EpochMillis>(minute + 1) * 60000LL;
                bar.open = open;
                bar.high = high;
                bar.low = low;
                bar.close = close;
                bar.cumulativeTurnover = cumulativeTurnover;
                bar.tradeIntensity =
                    80.0 + velocity * 0.15 +
                    8.0 * std::sin(minute * 0.19 + memberIndex);
                member.bars.push_back(bar);
                previousClose = close;
            }
            members.push_back(std::move(member));
        }
        return members;
    }
}
