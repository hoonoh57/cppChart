#pragma once

#include "market_types.h"

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace trading
{
    enum class OrderSide
    {
        Buy,
        Sell
    };

    struct CumulativeFill final
    {
        std::string orderId;
        std::string executionId;
        std::string code;
        std::string name;
        OrderSide side = OrderSide::Buy;
        Quantity cumulativeQuantity = 0;
        PriceWon fillPriceWon = 0;
        EpochMillis executionTimestampMs = 0;
    };

    enum class ApplyFillStatus
    {
        Applied,
        Duplicate,
        Stale,
        Rejected
    };

    struct ApplyFillResult final
    {
        ApplyFillStatus status = ApplyFillStatus::Rejected;
        Quantity appliedQuantity = 0;
        MoneyWon appliedNotionalWon = 0;
        MoneyWon realizedPnlWon = 0;
        std::string error;
    };

    struct LiquidationOrder final
    {
        std::string code;
        std::string name;
        Quantity quantity = 0;
    };

    class TradingState final
    {
    public:
        TradingState() = default;
        TradingState(const TradingState&) = delete;
        TradingState& operator=(const TradingState&) = delete;

        ApplyFillResult ApplyCumulativeFill(
            const CumulativeFill& fill);

        bool ReconcilePosition(
            const PositionSnapshot& brokerPosition,
            std::string& error);

        bool UpdateCurrentPrice(
            const std::string& code,
            PriceWon priceWon);

        bool SetSelected(
            const std::string& code,
            bool selected);

        std::vector<PositionSnapshot> SnapshotPositions() const;

        std::vector<LiquidationOrder> BuildLiquidationPlan(
            bool selectedOnly) const;

        MoneyWon RealizedPnlWon() const;

        void Reset();

    private:
        struct OrderProgress final
        {
            std::string code;
            OrderSide side = OrderSide::Buy;
            Quantity cumulativeApplied = 0;
        };

        static bool CheckedAdd(
            MoneyWon left,
            MoneyWon right,
            MoneyWon& out) noexcept;

        mutable std::mutex mutex_;
        std::map<std::string, PositionSnapshot> positions_;
        std::unordered_map<std::string, OrderProgress> orderProgress_;
        std::unordered_set<std::string> executionIds_;
        MoneyWon realizedPnlWon_ = 0;
    };
}
