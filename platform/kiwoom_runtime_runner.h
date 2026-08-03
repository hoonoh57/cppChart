#pragma once

#include "../core/kiwoom_runtime_engine.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace trading::platform
{
    struct RuntimeTransportResponse final
    {
        bool transportOk = false;
        unsigned long statusCode = 0;
        std::map<std::string, std::string> headers;
        std::string body;
        std::string error;
    };

    enum class RuntimeReceiveKind
    {
        Text,
        Closed,
        Error
    };

    struct RuntimeReceiveResult final
    {
        RuntimeReceiveKind kind = RuntimeReceiveKind::Error;
        std::string text;
        unsigned short closeStatus = 0;
        std::string error;
    };

    class IKiwoomRuntimeTransport
    {
    public:
        virtual ~IKiwoomRuntimeTransport() = default;

        virtual RuntimeTransportResponse SendRest(
            const std::string& baseUrl,
            const RestRequest& request,
            int timeoutMilliseconds) = 0;

        virtual bool ConnectWebSocket(
            const std::string& url,
            int timeoutMilliseconds,
            std::string& error) = 0;

        virtual bool SendWebSocketText(
            const std::string& text,
            std::string& error) = 0;

        virtual RuntimeReceiveResult ReceiveWebSocket() = 0;

        virtual void CloseWebSocket() = 0;

        virtual bool IsWebSocketConnected() const noexcept = 0;
    };

    struct KiwoomRunnerCallbacks final
    {
        std::function<void(
            const char* category,
            const std::string& message)> log;
        std::function<void()> wakeUi;
        std::function<void(bool enabled)> setObserveMode;
        std::function<void(
            const MinuteBarsPage& page,
            const Continuation& continuation)> minuteBars;
        std::function<void(
            const StockTradeTick& tick)> stockTrade;
    };

    class KiwoomRuntimeRunner final
    {
    public:
        KiwoomRuntimeRunner(
            KiwoomRuntimeEngine& engine,
            std::unique_ptr<IKiwoomRuntimeTransport> transport,
            KiwoomRunnerCallbacks callbacks = {});

        ~KiwoomRuntimeRunner();

        KiwoomRuntimeRunner(const KiwoomRuntimeRunner&) = delete;
        KiwoomRuntimeRunner& operator=(const KiwoomRuntimeRunner&) = delete;

        bool Start(
            const RuntimeConfig& config,
            std::string& error);

        void Stop();

        bool RequestStockMinuteBars(
            const std::string& stockCode,
            int minuteUnit,
            const Continuation& continuation,
            std::string& error);

        bool RequestIndexMinuteBars(
            const std::string& indexCode,
            int minuteUnit,
            const Continuation& continuation,
            std::string& error);

        bool SubscribeStockTrades(
            const std::string& stockCode,
            std::string& error);

        bool UnsubscribeStockTrades(
            const std::string& stockCode,
            std::string& error);

        bool SubmitOrder(
            const OrderIntent& intent,
            std::string& error);

        bool SubmitLiquidation(
            bool selectedOnly,
            std::string& error);

        bool IsRunning() const noexcept;

        KiwoomRuntimeSnapshot Snapshot() const;

    private:
        void Enqueue(
            std::vector<KiwoomRuntimeAction> actions);

        void WorkerLoop();

        void ReceiverLoop();

        void HandleAction(
            KiwoomRuntimeAction action);

        void TryQueueStockTradeSubscription();

        void StartReceiver();

        void StopReceiver();

        void ScheduleReconnect(
            int delayMilliseconds);

        Continuation ReadContinuation(
            const RuntimeTransportResponse& response) const;

        void DeliverMinuteBars(
            const KiwoomRuntimeAction& action,
            const RuntimeTransportResponse& response,
            const Continuation& continuation);

        void Log(
            const char* category,
            const std::string& message) const;

        void WakeUi() const;

        KiwoomRuntimeEngine& engine_;
        std::unique_ptr<IKiwoomRuntimeTransport> transport_;
        KiwoomRunnerCallbacks callbacks_;

        std::atomic<bool> running_{ false };
        std::atomic<bool> receiverRunning_{ false };
        mutable std::mutex queueMutex_;
        std::condition_variable queueCv_;
        std::deque<KiwoomRuntimeAction> queue_;
        bool reconnectScheduled_ = false;
        std::chrono::steady_clock::time_point reconnectDue_{};
        std::thread workerThread_;
        std::thread receiverThread_;

        std::mutex subscriptionMutex_;
        std::string stockTradeCode_;
        bool stockTradeSubscriptionSent_ = false;
    };
}
