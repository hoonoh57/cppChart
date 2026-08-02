#pragma once

#include "kiwoom_protocol.h"
#include "runtime_config.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace trading
{
    enum class KiwoomSessionState
    {
        Stopped,
        ConfigurationError,
        TokenRequestPending,
        SocketConnectPending,
        LoginPending,
        RegistrationPending,
        ReconciliationPending,
        Ready,
        ReconnectWaiting,
        Faulted
    };

    enum class KiwoomSessionActionType
    {
        None,
        RequestToken,
        ConnectWebSocket,
        SendWebSocketText,
        StartReconciliation,
        ScheduleReconnect,
        EnterObserveMode
    };

    struct KiwoomSessionAction final
    {
        KiwoomSessionActionType type = KiwoomSessionActionType::None;
        std::string text;
        int delayMilliseconds = 0;
        bool sensitive = false;
    };

    struct KiwoomSessionSnapshot final
    {
        KiwoomSessionState state = KiwoomSessionState::Stopped;
        int reconnectAttempt = 0;
        std::string lastError;
        std::string tokenExpiresAt;
        bool orderSubmissionAllowed = false;
    };

    class KiwoomSession final
    {
    public:
        KiwoomSession() = default;
        KiwoomSession(const KiwoomSession&) = delete;
        KiwoomSession& operator=(const KiwoomSession&) = delete;

        std::vector<KiwoomSessionAction> Start(
            const RuntimeConfig& config);

        std::vector<KiwoomSessionAction> Stop();

        std::vector<KiwoomSessionAction> OnTokenResponse(
            const std::string& json);

        std::vector<KiwoomSessionAction> OnWebSocketConnected();

        std::vector<KiwoomSessionAction> OnWebSocketMessage(
            const std::string& json);

        std::vector<KiwoomSessionAction> OnWebSocketClosed(
            const std::string& reason);

        std::vector<KiwoomSessionAction> OnReconnectTimer();

        std::vector<KiwoomSessionAction> OnReconciliationCompleted(
            bool success,
            const std::string& error = {});

        std::vector<KiwoomSessionAction> OnTokenExpired();

        KiwoomSessionSnapshot Snapshot() const;

        RuntimeConfig ConfigSnapshot() const;

    private:
        static int ReconnectDelayMilliseconds(
            int reconnectAttempt) noexcept;

        std::vector<KiwoomSessionAction> FailLocked(
            const std::string& error,
            bool observeMode);

        std::vector<KiwoomSessionAction> BeginReconnectLocked(
            const std::string& reason);

        mutable std::mutex mutex_;
        RuntimeConfig config_;
        KiwoomSessionState state_ = KiwoomSessionState::Stopped;
        std::string accessToken_;
        std::string tokenExpiresAt_;
        std::string lastError_;
        int reconnectAttempt_ = 0;
        bool stopRequested_ = false;
    };
}
