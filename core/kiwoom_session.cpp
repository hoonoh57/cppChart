#include "kiwoom_session.h"

#include <algorithm>
#include <utility>

namespace trading
{
    namespace
    {
        KiwoomSessionAction MakeAction(
            KiwoomSessionActionType type,
            std::string text = {},
            int delayMilliseconds = 0,
            bool sensitive = false)
        {
            KiwoomSessionAction action;
            action.type = type;
            action.text = std::move(text);
            action.delayMilliseconds = delayMilliseconds;
            action.sensitive = sensitive;
            return action;
        }
    }

    std::vector<KiwoomSessionAction> KiwoomSession::Start(
        const RuntimeConfig& config)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        config_ = config;
        accessToken_.clear();
        tokenExpiresAt_.clear();
        lastError_.clear();
        reconnectAttempt_ = 0;
        stopRequested_ = false;

        if (config_.mode != RuntimeMode::KiwoomMock) {
            state_ = KiwoomSessionState::ConfigurationError;
            lastError_ =
                "KIWOOM_MOCK runtime configuration is required";
            return {
                MakeAction(
                    KiwoomSessionActionType::EnterObserveMode,
                    lastError_)
            };
        }

        if (!config_.HasKiwoomCredentials()) {
            state_ = KiwoomSessionState::ConfigurationError;
            lastError_ =
                "KIWOOM_MOCK requires an App Key and App Secret";
            return {
                MakeAction(
                    KiwoomSessionActionType::EnterObserveMode,
                    lastError_)
            };
        }

        state_ = KiwoomSessionState::TokenRequestPending;
        return {
            MakeAction(
                KiwoomSessionActionType::RequestToken,
                {},
                0,
                true)
        };
    }

    std::vector<KiwoomSessionAction> KiwoomSession::Stop()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        stopRequested_ = true;
        state_ = KiwoomSessionState::Stopped;
        accessToken_.clear();
        tokenExpiresAt_.clear();
        lastError_.clear();
        reconnectAttempt_ = 0;
        return {};
    }

    std::vector<KiwoomSessionAction> KiwoomSession::OnTokenResponse(
        const std::string& json)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (
            stopRequested_ ||
            state_ != KiwoomSessionState::TokenRequestPending)
        {
            return {};
        }

        const TokenResponse response = ParseTokenResponse(json);
        if (!response.result.ok) {
            const std::string error =
                !response.result.error.empty()
                    ? response.result.error
                    : response.result.returnMessage;
            return BeginReconnectLocked(
                error.empty()
                    ? "token request failed"
                    : error);
        }

        accessToken_ = response.token;
        tokenExpiresAt_ = response.expiresAt;
        lastError_.clear();
        state_ = KiwoomSessionState::SocketConnectPending;

        return {
            MakeAction(
                KiwoomSessionActionType::ConnectWebSocket,
                config_.webSocketUrl)
        };
    }

    std::vector<KiwoomSessionAction> KiwoomSession::OnWebSocketConnected()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (
            stopRequested_ ||
            state_ != KiwoomSessionState::SocketConnectPending)
        {
            return {};
        }

        if (accessToken_.empty()) {
            state_ = KiwoomSessionState::TokenRequestPending;
            return {
                MakeAction(
                    KiwoomSessionActionType::RequestToken,
                    {},
                    0,
                    true)
            };
        }

        state_ = KiwoomSessionState::LoginPending;
        return {
            MakeAction(
                KiwoomSessionActionType::SendWebSocketText,
                BuildWebSocketLoginMessage(accessToken_),
                0,
                true)
        };
    }

    std::vector<KiwoomSessionAction> KiwoomSession::OnWebSocketMessage(
        const std::string& json)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (stopRequested_ || state_ == KiwoomSessionState::Stopped) {
            return {};
        }

        if (IsWebSocketPingMessage(json)) {
            return {
                MakeAction(
                    KiwoomSessionActionType::SendWebSocketText,
                    json)
            };
        }

        const RealTimeEnvelope envelope =
            ParseRealTimeEnvelope(json);

        if (!envelope.result.ok) {
            const std::string error =
                !envelope.result.error.empty()
                    ? envelope.result.error
                    : envelope.result.returnMessage;

            if (
                state_ == KiwoomSessionState::LoginPending ||
                state_ == KiwoomSessionState::RegistrationPending)
            {
                return BeginReconnectLocked(
                    error.empty()
                        ? "WebSocket handshake failed"
                        : error);
            }

            lastError_ =
                error.empty()
                    ? "WebSocket message rejected"
                    : error;
            return {};
        }

        if (
            state_ == KiwoomSessionState::LoginPending &&
            envelope.transactionName == "LOGIN")
        {
            state_ = KiwoomSessionState::RegistrationPending;
            return {
                MakeAction(
                    KiwoomSessionActionType::SendWebSocketText,
                    BuildWebSocketRegistrationMessage(
                        "1",
                        false,
                        {},
                        { "00", "04" }))
            };
        }

        if (
            state_ == KiwoomSessionState::RegistrationPending &&
            envelope.transactionName == "REG")
        {
            state_ = KiwoomSessionState::ReconciliationPending;
            return {
                MakeAction(
                    KiwoomSessionActionType::StartReconciliation)
            };
        }

        return {};
    }

    std::vector<KiwoomSessionAction> KiwoomSession::OnWebSocketClosed(
        const std::string& reason)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (stopRequested_ || state_ == KiwoomSessionState::Stopped) {
            return {};
        }

        return BeginReconnectLocked(
            reason.empty()
                ? "WebSocket disconnected"
                : reason);
    }

    std::vector<KiwoomSessionAction> KiwoomSession::OnReconnectTimer()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (
            stopRequested_ ||
            state_ != KiwoomSessionState::ReconnectWaiting)
        {
            return {};
        }

        if (accessToken_.empty()) {
            state_ = KiwoomSessionState::TokenRequestPending;
            return {
                MakeAction(
                    KiwoomSessionActionType::RequestToken,
                    {},
                    0,
                    true)
            };
        }

        state_ = KiwoomSessionState::SocketConnectPending;
        return {
            MakeAction(
                KiwoomSessionActionType::ConnectWebSocket,
                config_.webSocketUrl)
        };
    }

    std::vector<KiwoomSessionAction>
    KiwoomSession::OnReconciliationCompleted(
        bool success,
        const std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (
            stopRequested_ ||
            state_ != KiwoomSessionState::ReconciliationPending)
        {
            return {};
        }

        if (!success) {
            return FailLocked(
                error.empty()
                    ? "broker reconciliation failed"
                    : error,
                true);
        }

        state_ = KiwoomSessionState::Ready;
        reconnectAttempt_ = 0;
        lastError_.clear();
        return {};
    }

    std::vector<KiwoomSessionAction> KiwoomSession::OnTokenExpired()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (stopRequested_ || state_ == KiwoomSessionState::Stopped) {
            return {};
        }

        accessToken_.clear();
        tokenExpiresAt_.clear();
        state_ = KiwoomSessionState::TokenRequestPending;
        lastError_ = "access token expired";

        return {
            MakeAction(
                KiwoomSessionActionType::RequestToken,
                {},
                0,
                true)
        };
    }

    KiwoomSessionSnapshot KiwoomSession::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        KiwoomSessionSnapshot result;
        result.state = state_;
        result.reconnectAttempt = reconnectAttempt_;
        result.lastError = lastError_;
        result.tokenExpiresAt = tokenExpiresAt_;
        result.orderSubmissionAllowed =
            state_ == KiwoomSessionState::Ready &&
            !stopRequested_;
        return result;
    }

    RuntimeConfig KiwoomSession::ConfigSnapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_;
    }

    int KiwoomSession::ReconnectDelayMilliseconds(
        int reconnectAttempt) noexcept
    {
        const int clampedAttempt =
            (std::max)(1, (std::min)(reconnectAttempt, 6));

        const int delay = 1000 << (clampedAttempt - 1);
        return (std::min)(delay, 30000);
    }

    std::vector<KiwoomSessionAction> KiwoomSession::FailLocked(
        const std::string& error,
        bool observeMode)
    {
        state_ = KiwoomSessionState::Faulted;
        lastError_ = error;

        if (!observeMode) return {};

        return {
            MakeAction(
                KiwoomSessionActionType::EnterObserveMode,
                error)
        };
    }

    std::vector<KiwoomSessionAction>
    KiwoomSession::BeginReconnectLocked(
        const std::string& reason)
    {
        if (stopRequested_) return {};

        ++reconnectAttempt_;
        state_ = KiwoomSessionState::ReconnectWaiting;
        lastError_ = reason;

        std::vector<KiwoomSessionAction> actions;
        actions.push_back(
            MakeAction(
                KiwoomSessionActionType::ScheduleReconnect,
                reason,
                ReconnectDelayMilliseconds(reconnectAttempt_)));

        if (reconnectAttempt_ >= 5) {
            actions.push_back(
                MakeAction(
                    KiwoomSessionActionType::EnterObserveMode,
                    reason));
        }

        return actions;
    }
}
