#pragma once

#include "kiwoom_gateway_core.h"
#include "kiwoom_reconciliation.h"
#include "kiwoom_session.h"
#include "order_coordinator.h"
#include "runtime_config.h"
#include "safe_liquidation.h"
#include "trading_state.h"

#include <mutex>
#include <string>
#include <vector>

namespace trading
{
    enum class KiwoomRuntimeActionType
    {
        RequestTokenHttp,
        ConnectWebSocket,
        SendWebSocketText,
        RequestOpenOrders,
        RequestExecutions,
        RequestAccountBalance,
        SubmitOrderHttp,
        ScheduleReconnect,
        EnterObserveMode
    };

    struct KiwoomRuntimeAction final
    {
        KiwoomRuntimeActionType type =
            KiwoomRuntimeActionType::RequestTokenHttp;
        RestRequest request;
        std::string text;
        std::string clientIntentId;
        int delayMilliseconds = 0;
        bool sensitive = false;
    };

    struct KiwoomRuntimeSnapshot final
    {
        RuntimeMode mode = RuntimeMode::Unconfigured;
        KiwoomSessionState sessionState = KiwoomSessionState::Stopped;
        bool tokenAvailable = false;
        bool reconciliationInProgress = false;
        bool orderSubmissionAllowed = false;
        int reconnectAttempt = 0;
        std::size_t positionCount = 0;
        std::size_t orderCount = 0;
        std::size_t brokerOpenOrderCount = 0;
        std::string tokenExpiresAt;
        std::string lastError;
    };

    class KiwoomRuntimeEngine final
    {
    public:
        KiwoomRuntimeEngine(
            TradingState& tradingState,
            OrderCoordinator& orderCoordinator,
            KiwoomGatewayCore& gatewayCore,
            BrokerOpenOrderRegistry& brokerOpenOrders);

        KiwoomRuntimeEngine(const KiwoomRuntimeEngine&) = delete;
        KiwoomRuntimeEngine& operator=(const KiwoomRuntimeEngine&) = delete;

        std::vector<KiwoomRuntimeAction> Start(
            const RuntimeConfig& config);

        std::vector<KiwoomRuntimeAction> Stop();

        std::vector<KiwoomRuntimeAction> OnTokenHttpResponse(
            bool transportOk,
            unsigned long statusCode,
            const std::string& body,
            const std::string& transportError = {});

        std::vector<KiwoomRuntimeAction> OnWebSocketConnected();

        std::vector<KiwoomRuntimeAction> OnWebSocketMessage(
            const std::string& json,
            EpochMillis sessionDateStartMs = 0);

        std::vector<KiwoomRuntimeAction> OnWebSocketClosed(
            const std::string& reason);

        std::vector<KiwoomRuntimeAction> OnReconnectTimer();

        std::vector<KiwoomRuntimeAction> OnOpenOrdersHttpResponse(
            bool transportOk,
            unsigned long statusCode,
            const std::string& body,
            const Continuation& responseContinuation = {},
            const std::string& transportError = {});

        std::vector<KiwoomRuntimeAction> OnExecutionsHttpResponse(
            bool transportOk,
            unsigned long statusCode,
            const std::string& body,
            const Continuation& responseContinuation = {},
            const std::string& transportError = {});

        std::vector<KiwoomRuntimeAction> OnAccountBalanceHttpResponse(
            bool transportOk,
            unsigned long statusCode,
            const std::string& body,
            const Continuation& responseContinuation = {},
            const std::string& transportError = {});

        std::vector<KiwoomRuntimeAction> SubmitOrder(
            const OrderIntent& intent,
            std::string& error);

        std::vector<KiwoomRuntimeAction> SubmitLiquidation(
            bool selectedOnly,
            std::string& error);

        bool OnOrderHttpResponse(
            const std::string& clientIntentId,
            bool transportOk,
            unsigned long statusCode,
            const std::string& body,
            std::string& error,
            const std::string& transportError = {});

        KiwoomRuntimeSnapshot Snapshot() const;

        RuntimeConfig ConfigSnapshot() const;

    private:
        static bool IsSuccessfulHttp(
            bool transportOk,
            unsigned long statusCode) noexcept;

        static bool HasContinuation(
            const Continuation& continuation) noexcept;

        static std::string HttpFailureMessage(
            const char* operation,
            bool transportOk,
            unsigned long statusCode,
            const std::string& transportError);

        std::vector<KiwoomRuntimeAction> TranslateSessionActionsLocked(
            const std::vector<KiwoomSessionAction>& actions);

        std::vector<KiwoomRuntimeAction> BeginReconciliationLocked();

        std::vector<KiwoomRuntimeAction> FailReconciliationLocked(
            const std::string& error);

        KiwoomRuntimeAction MakeRestActionLocked(
            KiwoomRuntimeActionType type,
            RestRequest request,
            const std::string& clientIntentId = {}) const;

        TradingState& tradingState_;
        OrderCoordinator& orderCoordinator_;
        KiwoomGatewayCore& gatewayCore_;
        BrokerOpenOrderRegistry& brokerOpenOrders_;

        mutable std::mutex mutex_;
        KiwoomSession session_;
        RuntimeConfig config_;
        std::string accessToken_;
        std::string lastError_;
        std::vector<OpenOrderSnapshot> bufferedOpenOrders_;
        std::vector<ExecutionSnapshot> bufferedExecutions_;
        std::vector<PositionSnapshot> bufferedPositions_;
    };
}
