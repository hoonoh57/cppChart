#include "../platform/kiwoom_runtime_runner.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::exit(1);
    }

    void Check(bool condition, const char* message)
    {
        if (!condition) Fail(message);
    }

    template <typename Predicate>
    void WaitUntil(Predicate predicate, const char* message)
    {
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(5);

        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        Fail(message);
    }

    class FakeTransport final
        : public trading::platform::IKiwoomRuntimeTransport
    {
    public:
        trading::platform::RuntimeTransportResponse SendRest(
            const std::string&,
            const trading::RestRequest& request,
            int) override
        {
            trading::platform::RuntimeTransportResponse response;
            response.transportOk = true;
            response.statusCode = 200;

            if (request.path == "/oauth2/token") {
                response.body =
                    "{\"token_type\":\"bearer\",\"token\":\"ACCESS\","
                    "\"expires_dt\":\"20260803120000\",\"return_code\":0}";
            }
            else if (request.apiId == "ka10075") {
                response.body = "{\"oso\":[],\"return_code\":0}";
            }
            else if (request.apiId == "ka10076") {
                response.body = "{\"cntr\":[],\"return_code\":0}";
            }
            else if (request.apiId == "kt00018") {
                response.body =
                    "{\"tot_pur_amt\":\"840000\","
                    "\"tot_evlt_amt\":\"852000\","
                    "\"tot_evlt_pl\":\"12000\","
                    "\"prsm_dpst_aset_amt\":\"100000000\","
                    "\"acnt_evlt_remn_indv_tot\":[{"
                    "\"stk_cd\":\"A005930\",\"stk_nm\":\"삼성전자\","
                    "\"pur_pric\":\"70000\",\"rmnd_qty\":\"12\","
                    "\"trde_able_qty\":\"12\",\"cur_prc\":\"71000\","
                    "\"pur_amt\":\"840000\",\"evlt_amt\":\"852000\"}],"
                    "\"return_code\":0}";
            }
            else if (request.apiId == "kt10000") {
                response.body =
                    "{\"ord_no\":\"RUNNER-BUY\","
                    "\"dmst_stex_tp\":\"KRX\",\"return_code\":0}";
                PushText(
                    "{\"trnm\":\"REAL\",\"return_code\":0,\"data\":[{"
                    "\"type\":\"00\",\"item\":\"005930\","
                    "\"name\":\"주문체결\",\"values\":{"
                    "\"9203\":\"RUNNER-BUY\",\"9001\":\"A005930\","
                    "\"302\":\"삼성전자\",\"907\":\"2\","
                    "\"900\":\"1\",\"902\":\"0\","
                    "\"911\":\"1\",\"910\":\"72000\","
                    "\"909\":\"RUNNER-EXEC-1\",\"908\":\"093000\"}}]}");
            }
            else {
                response.transportOk = false;
                response.statusCode = 0;
                response.error = "unexpected fake REST request";
            }

            return response;
        }

        bool ConnectWebSocket(
            const std::string&,
            int,
            std::string& error) override
        {
            connected_.store(true, std::memory_order_release);
            connectCount_.fetch_add(1, std::memory_order_relaxed);
            error.clear();
            return true;
        }

        bool SendWebSocketText(
            const std::string& text,
            std::string& error) override
        {
            if (!connected_.load(std::memory_order_acquire)) {
                error = "fake socket is disconnected";
                return false;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                sentMessages_.push_back(text);
            }

            if (text.find("\"trnm\":\"LOGIN\"") != std::string::npos) {
                PushText("{\"trnm\":\"LOGIN\",\"return_code\":0}");
            }
            else if (text.find("\"trnm\":\"REG\"") != std::string::npos) {
                PushText("{\"trnm\":\"REG\",\"return_code\":0}");
            }

            error.clear();
            return true;
        }

        trading::platform::RuntimeReceiveResult ReceiveWebSocket() override
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] {
                return !messages_.empty() ||
                    !connected_.load(std::memory_order_acquire);
            });

            if (!messages_.empty()) {
                auto result = std::move(messages_.front());
                messages_.pop_front();
                return result;
            }

            trading::platform::RuntimeReceiveResult closed;
            closed.kind = trading::platform::RuntimeReceiveKind::Closed;
            closed.text = "fake socket closed";
            return closed;
        }

        void CloseWebSocket() override
        {
            connected_.store(false, std::memory_order_release);
            condition_.notify_all();
        }

        bool IsWebSocketConnected() const noexcept override
        {
            return connected_.load(std::memory_order_acquire);
        }

        void SimulatePhysicalDisconnect()
        {
            connected_.store(false, std::memory_order_release);
            condition_.notify_all();
        }

        int ConnectCount() const noexcept
        {
            return connectCount_.load(std::memory_order_relaxed);
        }

        int StockTradeRegistrationCount()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            int count = 0;
            for (const std::string& message : sentMessages_) {
                if (
                    message.find("\"type\":[\"0B\"]") !=
                        std::string::npos &&
                    message.find("\"refresh\":\"1\"") !=
                        std::string::npos &&
                    message.find("000660") != std::string::npos)
                {
                    ++count;
                }
            }
            return count;
        }

        int StockTradeRemovalCount()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            int count = 0;
            for (const std::string& message : sentMessages_) {
                if (
                    message.find("\"trnm\":\"REMOVE\"") !=
                        std::string::npos &&
                    message.find("\"type\":[\"0B\"]") !=
                        std::string::npos &&
                    message.find("\"refresh\"") ==
                        std::string::npos &&
                    message.find("000660") != std::string::npos)
                {
                    ++count;
                }
            }
            return count;
        }

        void PushStockTrade()
        {
            PushText(
                "{\"trnm\":\"REAL\",\"return_code\":0,\"data\":[{"
                "\"type\":\"0B\",\"item\":\"000660\","
                "\"name\":\"주식체결\",\"values\":{"
                "\"20\":\"123701\",\"10\":\"+1584000\","
                "\"15\":\"-3\",\"13\":\"552\"}}]}");
        }

    private:
        void PushText(const std::string& text)
        {
            trading::platform::RuntimeReceiveResult message;
            message.kind = trading::platform::RuntimeReceiveKind::Text;
            message.text = text;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                messages_.push_back(std::move(message));
            }
            condition_.notify_all();
        }

        std::atomic<bool> connected_{ false };
        std::atomic<int> connectCount_{ 0 };
        std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<trading::platform::RuntimeReceiveResult> messages_;
        std::vector<std::string> sentMessages_;
    };

    trading::RuntimeConfig MakeConfig()
    {
        trading::RuntimeConfig config;
        config.mode = trading::RuntimeMode::KiwoomMock;
        config.appKey = "APP";
        config.secretKey = "SECRET";
        return config;
    }

    void TestAsynchronousReadyOrderAndReconnect()
    {
        trading::TradingState state;
        trading::OrderCoordinator orders(state);
        trading::KiwoomGatewayCore gateway(state, orders);
        trading::BrokerOpenOrderRegistry brokerOpenOrders;
        trading::KiwoomRuntimeEngine engine(
            state,
            orders,
            gateway,
            brokerOpenOrders);

        auto transport = std::make_unique<FakeTransport>();
        FakeTransport* fake = transport.get();

        std::atomic<int> logCount{ 0 };
        std::atomic<int> wakeCount{ 0 };
        std::atomic<int> stockTradeCount{ 0 };
        std::atomic<bool> observe{ false };

        trading::platform::KiwoomRunnerCallbacks callbacks;
        callbacks.log = [&](const char*, const std::string&) {
            logCount.fetch_add(1, std::memory_order_relaxed);
        };
        callbacks.wakeUi = [&] {
            wakeCount.fetch_add(1, std::memory_order_relaxed);
        };
        callbacks.setObserveMode = [&](bool enabled) {
            observe.store(enabled, std::memory_order_release);
        };
        callbacks.stockTrade = [&](const trading::StockTradeTick& tick) {
            if (tick.code == "000660" && tick.priceWon == 1584000) {
                stockTradeCount.fetch_add(1, std::memory_order_relaxed);
            }
        };

        trading::platform::KiwoomRuntimeRunner runner(
            engine,
            std::move(transport),
            callbacks);

        std::string error;
        Check(runner.Start(MakeConfig(), error),
              "runner must start with fake transport");

        WaitUntil(
            [&] { return runner.Snapshot().orderSubmissionAllowed; },
            "runner did not reach ready state");
        Check(fake->ConnectCount() == 1,
              "initial flow must connect exactly once");
        Check(state.SnapshotPositions().size() == 1,
              "runner reconciliation must install broker position");

        Check(runner.SubscribeStockTrades("000660", error),
              "ready runner must accept stock trade subscription");
        WaitUntil(
            [&] { return fake->StockTradeRegistrationCount() >= 1; },
            "runner did not send 0B stock trade registration");
        fake->PushStockTrade();
        WaitUntil(
            [&] { return stockTradeCount.load(std::memory_order_relaxed) == 1; },
            "runner did not decode and deliver 0B stock trade");

        trading::OrderIntent buy;
        buy.code = "005930";
        buy.name = "삼성전자";
        buy.side = trading::StockOrderSide::Buy;
        buy.type = trading::StockOrderType::Market;
        buy.quantity = 1;

        Check(runner.SubmitOrder(buy, error),
              "ready runner must accept an order intent");
        WaitUntil(
            [&] {
                const auto positions = state.SnapshotPositions();
                return
                    positions.size() == 1 &&
                    positions[0].quantity == 13;
            },
            "asynchronous fill did not reach TradingState");

        fake->SimulatePhysicalDisconnect();
        WaitUntil(
            [&] { return !runner.Snapshot().orderSubmissionAllowed; },
            "physical disconnect did not block orders");
        WaitUntil(
            [&] { return fake->ConnectCount() >= 2; },
            "runtime did not execute scheduled reconnect");
        WaitUntil(
            [&] { return runner.Snapshot().orderSubmissionAllowed; },
            "runtime did not reconcile after reconnect");
        WaitUntil(
            [&] { return fake->StockTradeRegistrationCount() >= 2; },
            "runtime did not restore 0B subscription after reconnect");

        Check(runner.UnsubscribeStockTrades("000660", error),
              "runner must accept stock trade removal");
        WaitUntil(
            [&] { return fake->StockTradeRemovalCount() >= 1; },
            "runner did not send 0B REMOVE message");

        const int registrationsBeforeSecondReconnect =
            fake->StockTradeRegistrationCount();
        fake->SimulatePhysicalDisconnect();
        WaitUntil(
            [&] { return fake->ConnectCount() >= 3; },
            "runtime did not reconnect after removal");
        WaitUntil(
            [&] { return runner.Snapshot().orderSubmissionAllowed; },
            "runtime did not reconcile after removal reconnect");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        Check(
            fake->StockTradeRegistrationCount() ==
                registrationsBeforeSecondReconnect,
            "removed 0B subscription must not return after reconnect");

        Check(!observe.load(std::memory_order_acquire),
              "single recoverable disconnect must not force observe mode");
        Check(logCount.load(std::memory_order_relaxed) > 0,
              "runtime callbacks must receive log events");
        Check(wakeCount.load(std::memory_order_relaxed) > 0,
              "runtime callbacks must wake the UI");

        const int connectCountBeforeStop = fake->ConnectCount();
        runner.Stop();
        Check(!runner.IsRunning(),
              "runner must stop cleanly");
        Check(
            runner.Snapshot().sessionState ==
                trading::KiwoomSessionState::Stopped,
            "runner stop must leave the session stopped");

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        Check(fake->ConnectCount() == connectCountBeforeStop,
              "stop-induced socket close must not schedule a reconnect");
    }
}

int main()
{
    TestAsynchronousReadyOrderAndReconnect();
    std::puts("[PASS] kiwoom_runtime_runner_tests");
    return 0;
}
