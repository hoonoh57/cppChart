#include "kiwoom_runtime_runner.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace trading::platform
{
    namespace
    {
        std::string Lower(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
            return value;
        }

        const char* ActionName(KiwoomRuntimeActionType type) noexcept
        {
            switch (type) {
            case KiwoomRuntimeActionType::RequestTokenHttp:
                return "token";
            case KiwoomRuntimeActionType::RequestOpenOrders:
                return "open-orders";
            case KiwoomRuntimeActionType::RequestExecutions:
                return "executions";
            case KiwoomRuntimeActionType::RequestAccountBalance:
                return "balance";
            case KiwoomRuntimeActionType::RequestStockMinuteBars:
                return "stock-minute-bars";
            case KiwoomRuntimeActionType::RequestIndexMinuteBars:
                return "index-minute-bars";
            case KiwoomRuntimeActionType::SubmitOrderHttp:
                return "order";
            default:
                return "runtime";
            }
        }
    }

    KiwoomRuntimeRunner::KiwoomRuntimeRunner(
        KiwoomRuntimeEngine& engine,
        std::unique_ptr<IKiwoomRuntimeTransport> transport,
        KiwoomRunnerCallbacks callbacks)
        : engine_(engine),
          transport_(std::move(transport)),
          callbacks_(std::move(callbacks))
    {
    }

    KiwoomRuntimeRunner::~KiwoomRuntimeRunner()
    {
        Stop();
    }

    bool KiwoomRuntimeRunner::Start(
        const RuntimeConfig& config,
        std::string& error)
    {
        if (transport_ == nullptr) {
            error = "Kiwoom runtime transport is not configured";
            return false;
        }

        bool expected = false;
        if (!running_.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel))
        {
            error = "Kiwoom runtime is already running";
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queue_.clear();
            reconnectScheduled_ = false;
        }
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            stockTradeCode_.clear();
            stockTradeSubscriptionSent_ = false;
        }

        workerThread_ = std::thread(&KiwoomRuntimeRunner::WorkerLoop, this);
        Enqueue(engine_.Start(config));
        Log("SYS", "Kiwoom runtime started");
        WakeUi();
        error.clear();
        return true;
    }

    void KiwoomRuntimeRunner::Stop()
    {
        const bool wasRunning =
            running_.exchange(false, std::memory_order_acq_rel);

        if (!wasRunning) return;

        engine_.Stop();
        transport_->CloseWebSocket();
        queueCv_.notify_all();
        StopReceiver();

        if (workerThread_.joinable()) {
            workerThread_.join();
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queue_.clear();
            reconnectScheduled_ = false;
        }
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            stockTradeCode_.clear();
            stockTradeSubscriptionSent_ = false;
        }

        Log("SYS", "Kiwoom runtime stopped");
        WakeUi();
    }

    bool KiwoomRuntimeRunner::RequestStockMinuteBars(
        const std::string& stockCode,
        int minuteUnit,
        const Continuation& continuation,
        std::string& error)
    {
        if (!running_.load(std::memory_order_acquire)) {
            error = "Kiwoom runtime is not running";
            return false;
        }

        std::vector<KiwoomRuntimeAction> actions =
            engine_.RequestStockMinuteBars(
                stockCode,
                minuteUnit,
                continuation,
                error);
        if (actions.empty()) return false;
        Enqueue(std::move(actions));
        return true;
    }

    bool KiwoomRuntimeRunner::RequestIndexMinuteBars(
        const std::string& indexCode,
        int minuteUnit,
        const Continuation& continuation,
        std::string& error)
    {
        if (!running_.load(std::memory_order_acquire)) {
            error = "Kiwoom runtime is not running";
            return false;
        }

        std::vector<KiwoomRuntimeAction> actions =
            engine_.RequestIndexMinuteBars(
                indexCode,
                minuteUnit,
                continuation,
                error);
        if (actions.empty()) return false;
        Enqueue(std::move(actions));
        return true;
    }

    bool KiwoomRuntimeRunner::SubscribeStockTrades(
        const std::string& stockCode,
        std::string& error)
    {
        if (!running_.load(std::memory_order_acquire)) {
            error = "Kiwoom runtime is not running";
            return false;
        }
        if (stockCode.empty()) {
            error = "stock code is required for real-time subscription";
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            if (stockTradeCode_ != stockCode) {
                stockTradeCode_ = stockCode;
                stockTradeSubscriptionSent_ = false;
            }
        }

        TryQueueStockTradeSubscription();
        error.clear();
        return true;
    }

    bool KiwoomRuntimeRunner::SubmitOrder(
        const OrderIntent& intent,
        std::string& error)
    {
        if (!running_.load(std::memory_order_acquire)) {
            error = "Kiwoom runtime is not running";
            return false;
        }

        std::vector<KiwoomRuntimeAction> actions =
            engine_.SubmitOrder(intent, error);
        if (actions.empty()) return false;

        Enqueue(std::move(actions));
        return true;
    }

    bool KiwoomRuntimeRunner::SubmitLiquidation(
        bool selectedOnly,
        std::string& error)
    {
        if (!running_.load(std::memory_order_acquire)) {
            error = "Kiwoom runtime is not running";
            return false;
        }

        std::vector<KiwoomRuntimeAction> actions =
            engine_.SubmitLiquidation(selectedOnly, error);
        if (actions.empty()) return false;

        Enqueue(std::move(actions));
        return true;
    }

    bool KiwoomRuntimeRunner::IsRunning() const noexcept
    {
        return running_.load(std::memory_order_acquire);
    }

    KiwoomRuntimeSnapshot KiwoomRuntimeRunner::Snapshot() const
    {
        return engine_.Snapshot();
    }

    void KiwoomRuntimeRunner::Enqueue(
        std::vector<KiwoomRuntimeAction> actions)
    {
        if (actions.empty()) return;

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            for (KiwoomRuntimeAction& action : actions) {
                queue_.push_back(std::move(action));
            }
        }

        queueCv_.notify_all();
    }

    void KiwoomRuntimeRunner::WorkerLoop()
    {
        while (running_.load(std::memory_order_acquire)) {
            KiwoomRuntimeAction action;
            bool hasAction = false;
            bool fireReconnect = false;

            {
                std::unique_lock<std::mutex> lock(queueMutex_);

                while (
                    running_.load(std::memory_order_acquire) &&
                    queue_.empty())
                {
                    if (!reconnectScheduled_) {
                        queueCv_.wait(lock);
                        continue;
                    }

                    const bool signalled = queueCv_.wait_until(
                        lock,
                        reconnectDue_,
                        [this] {
                            return
                                !running_.load(std::memory_order_acquire) ||
                                !queue_.empty();
                        });

                    if (!signalled && queue_.empty()) {
                        reconnectScheduled_ = false;
                        fireReconnect = true;
                        break;
                    }
                }

                if (!running_.load(std::memory_order_acquire)) break;

                if (!fireReconnect && !queue_.empty()) {
                    action = std::move(queue_.front());
                    queue_.pop_front();
                    hasAction = true;
                }
            }

            if (fireReconnect) {
                Enqueue(engine_.OnReconnectTimer());
                continue;
            }

            if (hasAction) {
                HandleAction(std::move(action));
            }
        }
    }

    void KiwoomRuntimeRunner::ReceiverLoop()
    {
        while (
            running_.load(std::memory_order_acquire) &&
            receiverRunning_.load(std::memory_order_acquire) &&
            transport_->IsWebSocketConnected())
        {
            RuntimeReceiveResult received =
                transport_->ReceiveWebSocket();

            if (
                !running_.load(std::memory_order_acquire) ||
                !receiverRunning_.load(std::memory_order_acquire))
            {
                break;
            }

            if (received.kind == RuntimeReceiveKind::Text) {
                const RealTimeEnvelope envelope =
                    ParseRealTimeEnvelope(received.text);
                if (envelope.result.ok) {
                    for (const RealTimeRecord& record : envelope.records) {
                        if (record.type != "0B") continue;

                        const StockTradeDecodeResult decoded =
                            DecodeStockTradeRecord(record);
                        if (!decoded.result.ok) {
                            Log(
                                "FAULT",
                                decoded.result.error.empty()
                                    ? "stock trade 0B decode failed"
                                    : decoded.result.error);
                            continue;
                        }
                        if (callbacks_.stockTrade) {
                            callbacks_.stockTrade(decoded.tick);
                        }
                    }
                }

                Enqueue(engine_.OnWebSocketMessage(received.text));
                TryQueueStockTradeSubscription();
                WakeUi();
                continue;
            }

            std::string reason = received.error;
            if (reason.empty()) reason = received.text;
            if (reason.empty()) {
                reason = received.kind == RuntimeReceiveKind::Closed
                    ? "WebSocket closed"
                    : "WebSocket receive failed";
            }

            receiverRunning_.store(false, std::memory_order_release);
            Enqueue(engine_.OnWebSocketClosed(reason));
            Log("WS", reason);
            WakeUi();
            break;
        }

        receiverRunning_.store(false, std::memory_order_release);
    }

    void KiwoomRuntimeRunner::HandleAction(
        KiwoomRuntimeAction action)
    {
        switch (action.type) {
        case KiwoomRuntimeActionType::RequestTokenHttp: {
            const RuntimeConfig config = engine_.ConfigSnapshot();
            const RuntimeTransportResponse response = transport_->SendRest(
                config.restBaseUrl,
                action.request,
                15000);
            Enqueue(engine_.OnTokenHttpResponse(
                response.transportOk,
                response.statusCode,
                response.body,
                response.error));
            Log(
                response.transportOk ? "HTTP" : "FAULT",
                response.transportOk
                    ? "token response received"
                    : "token transport failed: " + response.error);
            WakeUi();
            break;
        }

        case KiwoomRuntimeActionType::ConnectWebSocket: {
            StopReceiver();
            std::string error;
            if (!transport_->ConnectWebSocket(
                    action.text,
                    15000,
                    error))
            {
                Enqueue(engine_.OnWebSocketClosed(error));
                Log("FAULT", "WebSocket connect failed: " + error);
                WakeUi();
                break;
            }

            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                reconnectScheduled_ = false;
            }
            {
                std::lock_guard<std::mutex> lock(subscriptionMutex_);
                stockTradeSubscriptionSent_ = false;
            }

            StartReceiver();
            Enqueue(engine_.OnWebSocketConnected());
            Log("WS", "WebSocket connected");
            WakeUi();
            break;
        }

        case KiwoomRuntimeActionType::SendWebSocketText: {
            std::string error;
            if (!transport_->SendWebSocketText(action.text, error)) {
                Enqueue(engine_.OnWebSocketClosed(error));
                Log("FAULT", "WebSocket send failed: " + error);
            }
            else {
                Log("WS", action.sensitive
                    ? "sensitive WebSocket command sent"
                    : "WebSocket command sent");
            }
            WakeUi();
            break;
        }

        case KiwoomRuntimeActionType::RequestOpenOrders:
        case KiwoomRuntimeActionType::RequestExecutions:
        case KiwoomRuntimeActionType::RequestAccountBalance:
        case KiwoomRuntimeActionType::RequestStockMinuteBars:
        case KiwoomRuntimeActionType::RequestIndexMinuteBars:
        case KiwoomRuntimeActionType::SubmitOrderHttp: {
            const RuntimeConfig config = engine_.ConfigSnapshot();
            const RuntimeTransportResponse response = transport_->SendRest(
                config.restBaseUrl,
                action.request,
                15000);
            const Continuation continuation = ReadContinuation(response);

            if (
                action.type == KiwoomRuntimeActionType::RequestStockMinuteBars ||
                action.type == KiwoomRuntimeActionType::RequestIndexMinuteBars)
            {
                DeliverMinuteBars(action, response, continuation);
            }
            else if (action.type == KiwoomRuntimeActionType::RequestOpenOrders) {
                Enqueue(engine_.OnOpenOrdersHttpResponse(
                    response.transportOk,
                    response.statusCode,
                    response.body,
                    continuation,
                    response.error));
            }
            else if (action.type == KiwoomRuntimeActionType::RequestExecutions) {
                Enqueue(engine_.OnExecutionsHttpResponse(
                    response.transportOk,
                    response.statusCode,
                    response.body,
                    continuation,
                    response.error));
            }
            else if (action.type ==
                     KiwoomRuntimeActionType::RequestAccountBalance)
            {
                Enqueue(engine_.OnAccountBalanceHttpResponse(
                    response.transportOk,
                    response.statusCode,
                    response.body,
                    continuation,
                    response.error));
            }
            else {
                std::string error;
                const bool accepted = engine_.OnOrderHttpResponse(
                    action.clientIntentId,
                    response.transportOk,
                    response.statusCode,
                    response.body,
                    error,
                    response.error);
                Log(
                    accepted ? "ORDER" : "REJECT",
                    accepted
                        ? "broker accepted order intent " + action.clientIntentId
                        : "order intent failed " + action.clientIntentId +
                            ": " + error);
            }

            if (!response.transportOk) {
                Log(
                    "FAULT",
                    std::string(ActionName(action.type)) +
                        " transport failed: " + response.error);
            }
            WakeUi();
            break;
        }

        case KiwoomRuntimeActionType::ScheduleReconnect:
            ScheduleReconnect(action.delayMilliseconds);
            Log("WS", "reconnect scheduled");
            WakeUi();
            break;

        case KiwoomRuntimeActionType::EnterObserveMode:
            if (callbacks_.setObserveMode) {
                callbacks_.setObserveMode(true);
            }
            Log("FAULT", action.text.empty()
                ? "observe mode requested"
                : action.text);
            WakeUi();
            break;
        }

        TryQueueStockTradeSubscription();
    }

    void KiwoomRuntimeRunner::TryQueueStockTradeSubscription()
    {
        if (!running_.load(std::memory_order_acquire)) return;
        if (!engine_.Snapshot().orderSubmissionAllowed) return;
        if (!transport_->IsWebSocketConnected()) return;

        std::string code;
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            if (stockTradeCode_.empty() || stockTradeSubscriptionSent_) {
                return;
            }
            code = stockTradeCode_;
            stockTradeSubscriptionSent_ = true;
        }

        KiwoomRuntimeAction action;
        action.type = KiwoomRuntimeActionType::SendWebSocketText;
        action.text = BuildWebSocketRegistrationMessage(
            "2",
            false,
            { code },
            { "0B" });
        Enqueue({ std::move(action) });
        Log("WS", "stock trade 0B subscription queued: " + code);
    }

    void KiwoomRuntimeRunner::StartReceiver()
    {
        if (receiverThread_.joinable()) {
            receiverThread_.join();
        }
        receiverRunning_.store(true, std::memory_order_release);
        receiverThread_ =
            std::thread(&KiwoomRuntimeRunner::ReceiverLoop, this);
    }

    void KiwoomRuntimeRunner::StopReceiver()
    {
        receiverRunning_.store(false, std::memory_order_release);
        if (transport_ != nullptr) {
            transport_->CloseWebSocket();
        }
        if (
            receiverThread_.joinable() &&
            receiverThread_.get_id() != std::this_thread::get_id())
        {
            receiverThread_.join();
        }
    }

    void KiwoomRuntimeRunner::ScheduleReconnect(
        int delayMilliseconds)
    {
        const int safeDelay = (std::max)(0, delayMilliseconds);
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            reconnectScheduled_ = true;
            reconnectDue_ =
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds(safeDelay);
        }
        queueCv_.notify_all();
    }

    Continuation KiwoomRuntimeRunner::ReadContinuation(
        const RuntimeTransportResponse& response) const
    {
        Continuation result;
        for (const auto& header : response.headers) {
            const std::string name = Lower(header.first);
            if (name == "cont-yn") {
                result.continueYn = header.second;
            }
            else if (name == "next-key") {
                result.nextKey = header.second;
            }
        }
        return result;
    }

    void KiwoomRuntimeRunner::DeliverMinuteBars(
        const KiwoomRuntimeAction& action,
        const RuntimeTransportResponse& response,
        const Continuation& continuation)
    {
        MinuteBarsPage page;
        page.instrument = action.marketInstrument;
        page.code = action.marketCode;
        page.minuteUnit = action.minuteUnit;

        if (!response.transportOk) {
            page.result.error = response.error.empty()
                ? "minute-bar transport failed"
                : "minute-bar transport failed: " + response.error;
        }
        else if (response.statusCode < 200 || response.statusCode >= 300) {
            std::ostringstream message;
            message << "minute-bar request failed: HTTP "
                    << response.statusCode;
            if (!response.body.empty()) {
                message << "; response=" << response.body;
            }
            page.result.error = message.str();
        }
        else if (action.marketInstrument == MinuteBarInstrument::Stock) {
            page = ParseStockMinuteBarsResponse(
                action.marketCode,
                action.minuteUnit,
                response.body);
        }
        else {
            page = ParseIndexMinuteBarsResponse(
                action.marketCode,
                action.minuteUnit,
                response.body);
        }

        if (callbacks_.minuteBars) {
            callbacks_.minuteBars(page, continuation);
        }

        if (page.result.ok) {
            std::ostringstream message;
            message
                << (action.marketInstrument == MinuteBarInstrument::Stock
                    ? "stock" : "index")
                << " minute bars received: code="
                << action.marketCode
                << " unit="
                << action.minuteUnit
                << " rows="
                << page.bars.size();
            if (!continuation.nextKey.empty()) {
                message << " continuation=" << continuation.continueYn;
            }
            Log("DATA", message.str());
        }
        else {
            const std::string error = !page.result.error.empty()
                ? page.result.error
                : page.result.returnMessage;
            Log("FAULT", error.empty()
                ? "minute-bar response rejected"
                : error);
        }
    }

    void KiwoomRuntimeRunner::Log(
        const char* category,
        const std::string& message) const
    {
        if (callbacks_.log) {
            callbacks_.log(category, message);
        }
    }

    void KiwoomRuntimeRunner::WakeUi() const
    {
        if (callbacks_.wakeUi) {
            callbacks_.wakeUi();
        }
    }
}
