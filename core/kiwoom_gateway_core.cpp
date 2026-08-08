#include "kiwoom_gateway_core.h"

#include <map>
#include <set>
#include <sstream>

namespace trading
{
    KiwoomGatewayCore::KiwoomGatewayCore(
        TradingState& tradingState,
        OrderCoordinator& orderCoordinator)
        : tradingState_(tradingState),
          orderCoordinator_(orderCoordinator)
    {
    }

    GatewayEventReport KiwoomGatewayCore::ApplyRealTimeEnvelope(
        const RealTimeEnvelope& envelope,
        EpochMillis sessionDateStartMs)
    {
        GatewayEventReport report;

        if (!envelope.result.ok) {
            report.errors.push_back(
                !envelope.result.error.empty()
                    ? envelope.result.error
                    : envelope.result.returnMessage);
            return report;
        }

        for (const RealTimeRecord& record : envelope.records) {
            if (record.type == "00") {
                ++report.orderRecords;

                const OrderExecutionDecodeResult decoded =
                    DecodeKiwoomOrderExecution(
                        record,
                        sessionDateStartMs);

                if (decoded.status == EventDecodeStatus::Invalid) {
                    report.errors.push_back(decoded.error);
                    continue;
                }
                if (decoded.status != EventDecodeStatus::Decoded) {
                    continue;
                }

                BrokerFillEvent fill;
                fill.brokerOrderNumber =
                    decoded.event.brokerOrderNumber;
                fill.executionId = decoded.event.executionId;
                fill.code = decoded.event.code;
                fill.name = decoded.event.name;
                fill.side = decoded.event.side;
                fill.cumulativeQuantity =
                    decoded.event.cumulativeQuantity;
                fill.fillPriceWon = decoded.event.fillPriceWon;
                fill.executionTimestampMs =
                    decoded.event.executionTimestampMs;

                const BrokerFillApplyResult applied =
                    orderCoordinator_.ApplyBrokerFill(fill);

                if (!applied.error.empty()) {
                    report.errors.push_back(applied.error);
                    continue;
                }

                switch (applied.positionResult.status) {
                case ApplyFillStatus::Applied:
                    ++report.fillEventsApplied;
                    break;
                case ApplyFillStatus::Duplicate:
                    ++report.fillEventsDuplicate;
                    break;
                case ApplyFillStatus::Stale:
                    ++report.fillEventsStale;
                    break;
                case ApplyFillStatus::Rejected:
                    report.errors.push_back(
                        applied.positionResult.error.empty()
                            ? "fill was rejected"
                            : applied.positionResult.error);
                    break;
                }
                continue;
            }

            if (record.type == "04") {
                ++report.balanceRecords;

                const BalanceDecodeResult decoded =
                    DecodeKiwoomBalanceUpdate(record);

                if (decoded.status == EventDecodeStatus::Invalid) {
                    report.errors.push_back(decoded.error);
                    continue;
                }
                if (decoded.status != EventDecodeStatus::Decoded) {
                    continue;
                }

                std::string error;
                if (!tradingState_.ReconcilePosition(
                        ToPositionSnapshot(decoded.event),
                        error))
                {
                    report.errors.push_back(error);
                    continue;
                }

                ++report.balanceUpdatesApplied;
            }
        }

        return report;
    }

    bool KiwoomGatewayCore::ApplyAuthoritativePositions(
        const std::vector<PositionSnapshot>& brokerPositions,
        std::string& error)
    {
        std::map<std::string, PositionSnapshot> validated;

        for (const PositionSnapshot& position : brokerPositions) {
            if (position.code.empty()) {
                error = "broker position code is required";
                return false;
            }
            if (position.quantity <= 0) {
                error = "authoritative broker positions must have positive quantity";
                return false;
            }
            if (position.costBasisWon <= 0) {
                error = "authoritative broker positions must have positive cost basis";
                return false;
            }
            if (position.currentPriceWon <= 0) {
                error = "authoritative broker positions must have positive current price";
                return false;
            }
            if (!validated.emplace(position.code, position).second) {
                error = "duplicate broker position code: " + position.code;
                return false;
            }
        }

        const std::vector<PositionSnapshot> existing =
            tradingState_.SnapshotPositions();

        std::map<std::string, bool> selectedByCode;
        for (const PositionSnapshot& position : existing) {
            selectedByCode[position.code] = position.selected;
        }

        for (const PositionSnapshot& position : existing) {
            if (validated.find(position.code) != validated.end()) continue;

            PositionSnapshot flat;
            flat.code = position.code;

            std::string reconcileError;
            if (!tradingState_.ReconcilePosition(flat, reconcileError)) {
                error = reconcileError;
                return false;
            }
        }

        for (auto& entry : validated) {
            PositionSnapshot& position = entry.second;
            const auto selected = selectedByCode.find(position.code);
            if (selected != selectedByCode.end()) {
                position.selected = selected->second;
            }

            std::string reconcileError;
            if (!tradingState_.ReconcilePosition(position, reconcileError)) {
                error = reconcileError;
                return false;
            }
        }

        error.clear();
        return true;
    }
}
