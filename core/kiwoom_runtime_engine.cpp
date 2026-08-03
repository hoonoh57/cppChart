#include "kiwoom_runtime_engine.h"

#include "json_lite.h"

#include <algorithm>
#include <iterator>
#include <sstream>
#include <utility>

namespace trading
{
    namespace
    {
        OrderSide ToTradingSide(StockOrderSide side) noexcept
        {
            return side == StockOrderSide::Buy
                ? OrderSide::Buy
                : OrderSide::Sell;
        }

        std::string BuildProtocolFailureJson(
            const std::string& message)
        {
            return
                "{\"return_code\":-1,\"return_msg\":" +
                json_lite::EscapeString(message) +
                "}";
        }
    }

    KiwoomRuntimeEngine::KiwoomRuntimeEngine(
        TradingState& tradingState,
        OrderCoordinator& orderCoordinator,
        KiwoomGatewayCore& gatewayCore,
        BrokerOpenOrderRegistry& brokerOpenOrders)
        : tradingState_(tradingState),
          orderCoordinator_(orderCoordinator),
          gatewayCore_(gatewayCore),
          brokerOpenOrders_(brokerOpenOrders)
    {
    }

    std::vector<KiwoomRuntimeAction> KiwoomRuntimeEngine::Start(
        const RuntimeConfig& config)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        accessToken_.clear();
        lastError_.clear();
        bufferedOpenOrders_.clear();
        bufferedExecutions_.clear();
        bufferedPositions_.clear();
        brokerOpenOrders_.Clear();
        orderCoordinator_.SetSubmissionAllowed(false);

        return TranslateSessionActionsLocked(
            session_.Start(config_));
    }

    std::vector<KiwoomRuntimeAction> KiwoomRuntimeEngine::Stop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        orderCoordinator_.SetSubmissionAllowed(false);
        accessToken_.clear();
        bufferedOpenOrders_.clear();
        bufferedExecutions_.clear();
        bufferedPositions_.clear();
        brokerOpenOrders_.Clear();
        return TranslateSessionActionsLocked(session_.Stop());
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::OnTokenHttpResponse(
        bool transportOk,
        unsigned long statusCode,
        const std::string& body,
        const std::string& transportError)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string sessionBody = body;
        if (!IsSuccessfulHttp(transportOk, statusCode)) {
            lastError_ = HttpFailureMessage(
                "token request",
                transportOk,
                statusCode,
                transportError);
            sessionBody = BuildProtocolFailureJson(lastError_);
        }
        else {
            const TokenResponse token = ParseTokenResponse(body);
            if (token.result.ok) {
                accessToken_ = token.token;
                lastError_.clear();
            }
            else {
                accessToken_.clear();
                lastError_ = !token.result.error.empty()
                    ? token.result.error
                    : token.result.returnMessage;
            }
        }

        return TranslateSessionActionsLocked(
            session_.OnTokenResponse(sessionBody));
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::OnWebSocketConnected()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return TranslateSessionActionsLocked(
            session_.OnWebSocketConnected());
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::OnWebSocketMessage(
        const std::string& json,
        EpochMillis sessionDateStartMs)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<KiwoomRuntimeAction> actions =
            TranslateSessionActionsLocked(
                session_.OnWebSocketMessage(json));

        const RealTimeEnvelope envelope =
            ParseRealTimeEnvelope(json);

        if (!envelope.result.ok || envelope.records.empty()) {
            return actions;
        }

        const GatewayEventReport report =
            gatewayCore_.ApplyRealTimeEnvelope(
                envelope,
                sessionDateStartMs);

        if (!report.Ok()) {
            std::ostringstream message;
            message << "real-time event application failed";
            for (const std::string& error : report.errors) {
                message << "; " << error;
            }
            lastError_ = message.str();
            orderCoordinator_.SetSubmissionAllowed(false);

            KiwoomRuntimeAction action;
            action.type = KiwoomRuntimeActionType::EnterObserveMode;
            action.text = lastError_;
            actions.push_back(std::move(action));
        }

        return actions;
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::OnWebSocketClosed(
        const std::string& reason)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        orderCoordinator_.SetSubmissionAllowed(false);
        lastError_ = reason.empty()
            ? "WebSocket disconnected"
            : reason;
        return TranslateSessionActionsLocked(
            session_.OnWebSocketClosed(lastError_));
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::OnReconnectTimer()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return TranslateSessionActionsLocked(
            session_.OnReconnectTimer());
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::OnOpenOrdersHttpResponse(
        bool transportOk,
        unsigned long statusCode,
        const std::string& body,
        const Continuation& responseContinuation,
        const std::string& transportError)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!IsSuccessfulHttp(transportOk, statusCode)) {
            return FailReconciliationLocked(
                HttpFailureMessage(
                    "open-order reconciliation",
                    transportOk,
                    statusCode,
                    transportError));
        }

        const OpenOrdersResponse response =
            ParseOpenOrdersResponse(body);
        if (!response.result.ok) {
            return FailReconciliationLocked(
                !response.result.error.empty()
                    ? response.result.error
                    : response.result.returnMessage);
        }

        bufferedOpenOrders_.insert(
            bufferedOpenOrders_.end(),
            response.orders.begin(),
            response.orders.end());

        if (HasContinuation(responseContinuation)) {
            return {
                MakeRestActionLocked(
                    KiwoomRuntimeActionType::RequestOpenOrders,
                    BuildOpenOrdersRestRequest(
                        accessToken_,
                        {},
                        ReconciliationSide::All,
                        responseContinuation))
            };
        }

        std::string registryError;
        if (!brokerOpenOrders_.Replace(
                bufferedOpenOrders_,
                registryError))
        {
            return FailReconciliationLocked(registryError);
        }

        return {
            MakeRestActionLocked(
                KiwoomRuntimeActionType::RequestExecutions,
                BuildExecutionsRestRequest(accessToken_))
        };
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::OnExecutionsHttpResponse(
        bool transportOk,
        unsigned long statusCode,
        const std::string& body,
        const Continuation& responseContinuation,
        const std::string& transportError)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!IsSuccessfulHttp(transportOk, statusCode)) {
            return FailReconciliationLocked(
                HttpFailureMessage(
                    "execution reconciliation",
                    transportOk,
                    statusCode,
                    transportError));
        }

        const ExecutionsResponse response =
            ParseExecutionsResponse(body);
        if (!response.result.ok) {
            return FailReconciliationLocked(
                !response.result.error.empty()
                    ? response.result.error
                    : response.result.returnMessage);
        }

        bufferedExecutions_.insert(
            bufferedExecutions_.end(),
            response.executions.begin(),
            response.executions.end());

        if (HasContinuation(responseContinuation)) {
            return {
                MakeRestActionLocked(
                    KiwoomRuntimeActionType::RequestExecutions,
                    BuildExecutionsRestRequest(
                        accessToken_,
                        {},
                        ReconciliationSide::All,
                        {},
                        responseContinuation))
            };
        }

        for (const ExecutionSnapshot& execution : bufferedExecutions_) {
            if (
                execution.brokerOrderNumber.empty() ||
                execution.code.empty() ||
                execution.cumulativeFilledQuantity <= 0)
            {
                continue;
            }

            std::string progressError;
            if (!tradingState_.ReconcileOrderProgress(
                    execution.brokerOrderNumber,
                    execution.code,
                    ToTradingSide(execution.side),
                    execution.cumulativeFilledQuantity,
                    progressError))
            {
                return FailReconciliationLocked(progressError);
            }
        }

        return {
            MakeRestActionLocked(
                KiwoomRuntimeActionType::RequestAccountBalance,
                BuildAccountBalanceRestRequest(accessToken_))
        };
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::OnAccountBalanceHttpResponse(
        bool transportOk,
        unsigned long statusCode,
        const std::string& body,
        const Continuation& responseContinuation,
        const std::string& transportError)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!IsSuccessfulHttp(transportOk, statusCode)) {
            return FailReconciliationLocked(
                HttpFailureMessage(
                    "account-balance reconciliation",
                    transportOk,
                    statusCode,
                    transportError));
        }

        const AccountBalanceResponse response =
            ParseAccountBalanceResponse(body);
        if (!response.result.ok) {
            return FailReconciliationLocked(
                !response.result.error.empty()
                    ? response.result.error
                    : response.result.returnMessage);
        }

        bufferedPositions_.insert(
            bufferedPositions_.end(),
            response.positions.begin(),
            response.positions.end());

        if (HasContinuation(responseContinuation)) {
            return {
                MakeRestActionLocked(
                    KiwoomRuntimeActionType::RequestAccountBalance,
                    BuildAccountBalanceRestRequest(
                        accessToken_,
                        responseContinuation))
            };
        }

        std::string reconcileError;
        if (!gatewayCore_.ApplyAuthoritativePositions(
                bufferedPositions_,
                reconcileError))
        {
            return FailReconciliationLocked(reconcileError);
        }

        orderCoordinator_.CompleteReconciliation(true);
        lastError_.clear();
        return TranslateSessionActionsLocked(
            session_.OnReconciliationCompleted(true));
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::RequestStockMinuteBars(
        const std::string& stockCode,
        int minuteUnit,
        const Continuation& continuation,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return RequestMinuteBarsLocked(
            MinuteBarInstrument::Stock,
            stockCode,
            minuteUnit,
            continuation,
            error);
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::RequestIndexMinuteBars(
        const std::string& indexCode,
        int minuteUnit,
        const Continuation& continuation,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return RequestMinuteBarsLocked(
            MinuteBarInstrument::Index,
            indexCode,
            minuteUnit,
            continuation,
            error);
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::SubmitOrder(
        const OrderIntent& intent,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const KiwoomSessionSnapshot sessionSnapshot =
            session_.Snapshot();
        if (!sessionSnapshot.orderSubmissionAllowed) {
            error = "Kiwoom session is not ready for orders";
            return {};
        }
        if (accessToken_.empty()) {
            error = "access token is not available";
            return {};
        }

        const CreateOrderResult created =
            orderCoordinator_.CreateOrder(intent);
        if (!created.ok) {
            error = created.error;
            return {};
        }

        RestRequest request;
        if (!orderCoordinator_.BuildRestRequest(
                created.order.clientIntentId,
                accessToken_,
                request,
                error))
        {
            std::string rejectError;
            orderCoordinator_.MarkRestRejected(
                created.order.clientIntentId,
                error,
                rejectError);
            return {};
        }

        error.clear();
        return {
            MakeRestActionLocked(
                KiwoomRuntimeActionType::SubmitOrderHttp,
                std::move(request),
                created.order.clientIntentId)
        };
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::SubmitLiquidation(
        bool selectedOnly,
        std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const KiwoomSessionSnapshot sessionSnapshot =
            session_.Snapshot();
        if (!sessionSnapshot.orderSubmissionAllowed) {
            error = "Kiwoom session is not ready for liquidation";
            return {};
        }
        if (accessToken_.empty()) {
            error = "access token is not available";
            return {};
        }

        const std::vector<LiquidationOrder> plan =
            BuildBrokerAwareLiquidationPlan(
                tradingState_,
                orderCoordinator_,
                brokerOpenOrders_,
                selectedOnly);

        std::vector<KiwoomRuntimeAction> actions;
        for (const LiquidationOrder& liquidation : plan) {
            OrderIntent intent;
            intent.code = liquidation.code;
            intent.name = liquidation.name;
            intent.side = StockOrderSide::Sell;
            intent.type = StockOrderType::Market;
            intent.quantity = liquidation.quantity;

            const CreateOrderResult created =
                orderCoordinator_.CreateOrder(intent);
            if (!created.ok) {
                error = created.error;
                return {};
            }

            RestRequest request;
            std::string buildError;
            if (!orderCoordinator_.BuildRestRequest(
                    created.order.clientIntentId,
                    accessToken_,
                    request,
                    buildError))
            {
                std::string rejectError;
                orderCoordinator_.MarkRestRejected(
                    created.order.clientIntentId,
                    buildError,
                    rejectError);
                error = buildError;
                return {};
            }

            actions.push_back(
                MakeRestActionLocked(
                    KiwoomRuntimeActionType::SubmitOrderHttp,
                    std::move(request),
                    created.order.clientIntentId));
        }

        if (actions.empty()) {
            error = selectedOnly
                ? "no selected quantity is available for liquidation"
                : "no quantity is available for liquidation";
            return {};
        }

        error.clear();
        return actions;
    }

    bool KiwoomRuntimeEngine::OnOrderHttpResponse(
        const std::string& clientIntentId,
        bool transportOk,
        unsigned long statusCode,
        const std::string& body,
        std::string& error,
        const std::string& transportError)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!IsSuccessfulHttp(transportOk, statusCode)) {
            const std::string message = HttpFailureMessage(
                "order submission",
                transportOk,
                statusCode,
                transportError);
            std::string markError;
            orderCoordinator_.MarkRestRejected(
                clientIntentId,
                message,
                markError);
            lastError_ = message;
            error = markError.empty() ? message : markError;
            return false;
        }

        const OrderResponse response = ParseOrderResponse(body);
        if (!response.result.ok) {
            const std::string message =
                !response.result.error.empty()
                    ? response.result.error
                    : response.result.returnMessage;
            std::string markError;
            orderCoordinator_.MarkRestRejected(
                clientIntentId,
                message,
                markError);
            lastError_ = message;
            error = markError.empty() ? message : markError;
            return false;
        }

        if (!orderCoordinator_.MarkRestAccepted(
                clientIntentId,
                response.orderNumber,
                error))
        {
            lastError_ = error;
            return false;
        }

        lastError_.clear();
        error.clear();
        return true;
    }

    KiwoomRuntimeSnapshot KiwoomRuntimeEngine::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const KiwoomSessionSnapshot sessionSnapshot =
            session_.Snapshot();

        KiwoomRuntimeSnapshot result;
        result.mode = config_.mode;
        result.sessionState = sessionSnapshot.state;
        result.tokenAvailable = !accessToken_.empty();
        result.reconciliationInProgress =
            orderCoordinator_.ReconciliationInProgress();
        result.orderSubmissionAllowed =
            sessionSnapshot.orderSubmissionAllowed &&
            orderCoordinator_.SubmissionAllowed();
        result.reconnectAttempt = sessionSnapshot.reconnectAttempt;
        result.positionCount =
            tradingState_.SnapshotPositions().size();
        result.orderCount =
            orderCoordinator_.SnapshotOrders().size();
        result.brokerOpenOrderCount =
            brokerOpenOrders_.Snapshot().size();
        result.tokenExpiresAt = sessionSnapshot.tokenExpiresAt;
        result.lastError = !lastError_.empty()
            ? lastError_
            : sessionSnapshot.lastError;
        return result;
    }

    RuntimeConfig KiwoomRuntimeEngine::ConfigSnapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_;
    }

    bool KiwoomRuntimeEngine::IsSuccessfulHttp(
        bool transportOk,
        unsigned long statusCode) noexcept
    {
        return transportOk && statusCode >= 200 && statusCode < 300;
    }

    bool KiwoomRuntimeEngine::HasContinuation(
        const Continuation& continuation) noexcept
    {
        return
            !continuation.nextKey.empty() &&
            (
                continuation.continueYn == "Y" ||
                continuation.continueYn == "y" ||
                continuation.continueYn == "1");
    }

    std::string KiwoomRuntimeEngine::HttpFailureMessage(
        const char* operation,
        bool transportOk,
        unsigned long statusCode,
        const std::string& transportError)
    {
        std::ostringstream message;
        message << operation << " failed";
        if (!transportOk) {
            if (!transportError.empty()) {
                message << ": " << transportError;
            }
            else {
                message << ": transport error";
            }
        }
        else {
            message << ": HTTP " << statusCode;
        }
        return message.str();
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::TranslateSessionActionsLocked(
        const std::vector<KiwoomSessionAction>& sessionActions)
    {
        std::vector<KiwoomRuntimeAction> result;

        for (const KiwoomSessionAction& sessionAction : sessionActions) {
            switch (sessionAction.type) {
            case KiwoomSessionActionType::RequestToken: {
                RestRequest request;
                request.method = "POST";
                request.path = KiwoomEndpoints::TokenPath;
                request.headers["content-type"] =
                    "application/json;charset=UTF-8";
                request.body = BuildTokenRequestBody(
                    config_.appKey,
                    config_.secretKey);

                KiwoomRuntimeAction action = MakeRestActionLocked(
                    KiwoomRuntimeActionType::RequestTokenHttp,
                    std::move(request));
                action.sensitive = true;
                result.push_back(std::move(action));
                break;
            }

            case KiwoomSessionActionType::ConnectWebSocket: {
                KiwoomRuntimeAction action;
                action.type = KiwoomRuntimeActionType::ConnectWebSocket;
                action.text = sessionAction.text;
                result.push_back(std::move(action));
                break;
            }

            case KiwoomSessionActionType::SendWebSocketText: {
                KiwoomRuntimeAction action;
                action.type = KiwoomRuntimeActionType::SendWebSocketText;
                action.text = sessionAction.text;
                action.sensitive = sessionAction.sensitive;
                result.push_back(std::move(action));
                break;
            }

            case KiwoomSessionActionType::StartReconciliation: {
                std::vector<KiwoomRuntimeAction> reconciliation =
                    BeginReconciliationLocked();
                result.insert(
                    result.end(),
                    std::make_move_iterator(reconciliation.begin()),
                    std::make_move_iterator(reconciliation.end()));
                break;
            }

            case KiwoomSessionActionType::ScheduleReconnect: {
                KiwoomRuntimeAction action;
                action.type = KiwoomRuntimeActionType::ScheduleReconnect;
                action.text = sessionAction.text;
                action.delayMilliseconds =
                    sessionAction.delayMilliseconds;
                result.push_back(std::move(action));
                break;
            }

            case KiwoomSessionActionType::EnterObserveMode: {
                KiwoomRuntimeAction action;
                action.type = KiwoomRuntimeActionType::EnterObserveMode;
                action.text = sessionAction.text;
                result.push_back(std::move(action));
                break;
            }

            default:
                break;
            }
        }

        return result;
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::BeginReconciliationLocked()
    {
        if (accessToken_.empty()) {
            return FailReconciliationLocked(
                "reconciliation requires an access token");
        }

        bufferedOpenOrders_.clear();
        bufferedExecutions_.clear();
        bufferedPositions_.clear();
        orderCoordinator_.BeginReconciliation();

        return {
            MakeRestActionLocked(
                KiwoomRuntimeActionType::RequestOpenOrders,
                BuildOpenOrdersRestRequest(accessToken_))
        };
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::FailReconciliationLocked(
        const std::string& error)
    {
        lastError_ = error.empty()
            ? "broker reconciliation failed"
            : error;
        bufferedOpenOrders_.clear();
        bufferedExecutions_.clear();
        bufferedPositions_.clear();
        orderCoordinator_.CompleteReconciliation(false);
        return TranslateSessionActionsLocked(
            session_.OnReconciliationCompleted(
                false,
                lastError_));
    }

    std::vector<KiwoomRuntimeAction>
    KiwoomRuntimeEngine::RequestMinuteBarsLocked(
        MinuteBarInstrument instrument,
        const std::string& code,
        int minuteUnit,
        const Continuation& continuation,
        std::string& error)
    {
        if (accessToken_.empty()) {
            error = "access token is not available for market data";
            return {};
        }

        RestRequest request;
        if (instrument == MinuteBarInstrument::Stock) {
            request = BuildStockMinuteBarsRestRequest(
                code,
                minuteUnit,
                accessToken_,
                true,
                continuation,
                error);
        }
        else {
            request = BuildIndexMinuteBarsRestRequest(
                code,
                minuteUnit,
                accessToken_,
                continuation,
                error);
        }

        if (!error.empty()) return {};

        KiwoomRuntimeAction action = MakeRestActionLocked(
            instrument == MinuteBarInstrument::Stock
                ? KiwoomRuntimeActionType::RequestStockMinuteBars
                : KiwoomRuntimeActionType::RequestIndexMinuteBars,
            std::move(request));
        action.marketInstrument = instrument;
        action.marketCode = code;
        action.minuteUnit = minuteUnit;
        action.continuation = continuation;
        error.clear();
        return { std::move(action) };
    }

    KiwoomRuntimeAction KiwoomRuntimeEngine::MakeRestActionLocked(
        KiwoomRuntimeActionType type,
        RestRequest request,
        const std::string& clientIntentId) const
    {
        KiwoomRuntimeAction action;
        action.type = type;
        action.request = std::move(request);
        action.clientIntentId = clientIntentId;
        return action;
    }
}
