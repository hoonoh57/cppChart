from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "platform" / "kiwoom_runtime_runner.cpp"
SHELL = ROOT / "shell_main.cpp"
RUNNER_TEST = ROOT / "tests" / "kiwoom_runtime_runner_tests.cpp"
RUN_ALL = ROOT / "tests" / "run_all.bat"
CI = ROOT / ".github" / "workflows" / "windows-ci.yml"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


def write(path: Path, text: str) -> None:
    path.write_text("\ufeff" + text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


runner = read(RUNNER)
subscribe_block = '''    bool KiwoomRuntimeRunner::SubscribeStockTrades(
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

'''
unsubscribe_block = subscribe_block + '''    bool KiwoomRuntimeRunner::UnsubscribeStockTrades(
        const std::string& stockCode,
        std::string& error)
    {
        if (stockCode.empty()) {
            error = "stock code is required for real-time removal";
            return false;
        }

        std::string subscribedCode;
        bool removalRequired = false;
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            if (stockTradeCode_.empty()) {
                error.clear();
                return true;
            }
            if (stockTradeCode_ != stockCode) {
                error =
                    "real-time removal code does not match active subscription";
                return false;
            }

            subscribedCode = stockTradeCode_;
            removalRequired = stockTradeSubscriptionSent_;
            stockTradeCode_.clear();
            stockTradeSubscriptionSent_ = false;
        }

        if (
            removalRequired &&
            running_.load(std::memory_order_acquire) &&
            transport_->IsWebSocketConnected())
        {
            KiwoomRuntimeAction action;
            action.type = KiwoomRuntimeActionType::SendWebSocketText;
            action.text = BuildWebSocketRemovalMessage(
                "2",
                { subscribedCode },
                { "0B" });
            Enqueue({ std::move(action) });
            Log("WS", "stock trade 0B removal queued: " + subscribedCode);
        }

        error.clear();
        return true;
    }

'''
runner = replace_once(
    runner,
    subscribe_block,
    unsubscribe_block,
    "runner unsubscribe method",
)
write(RUNNER, runner)

shell = read(SHELL)
old_level = '''static bool SetFeatureLevel(
    const std::string& id,
    trading::app::FeatureLevel level,
    std::string& error)
{
    if (!g_featureRegistry.SetLevel(id, level, error)) return false;
    if (id == "market-data") {
        if (!g_marketDataModule.SetLevel(level, error)) return false;
    }
    else if (id == "chart-workspace") {
        if (!g_chartWorkspaceModule.SetLevel(level, error)) return false;
    }
    return true;
}
'''
new_level = '''static bool SetFeatureLevel(
    const std::string& id,
    trading::app::FeatureLevel level,
    std::string& error)
{
    trading::app::FeatureSnapshot previousFeature;
    if (!g_featureRegistry.Get(id, previousFeature)) {
        error = "기능이 등록되어 있지 않습니다: " + id;
        return false;
    }

    const trading::app::MarketDataSnapshot previousMarket =
        g_marketDataModule.Snapshot();

    if (!g_featureRegistry.SetLevel(id, level, error)) return false;

    if (id == "market-data") {
        if (!g_marketDataModule.SetLevel(level, error)) {
            std::string rollbackError;
            g_featureRegistry.SetLevel(
                id,
                previousFeature.level,
                rollbackError);
            return false;
        }

        const bool enabled =
            level == trading::app::FeatureLevel::Visible ||
            level == trading::app::FeatureLevel::Active;

        if (!enabled) {
            if (
                g_runtimeRunner &&
                !previousMarket.code.empty())
            {
                std::string unsubscribeError;
                if (!g_runtimeRunner->UnsubscribeStockTrades(
                        previousMarket.code,
                        unsubscribeError))
                {
                    g_log.Add(
                        "FAULT",
                        "0B 실시간 해지 실패: %s",
                        unsubscribeError.c_str());
                    std::string healthError;
                    g_featureRegistry.SetHealth(
                        "market-data",
                        false,
                        unsubscribeError,
                        healthError);
                }
            }
            g_marketDataModule.SetStockTradeSubscriptionRequested(false);
        }
        else if (
            previousMarket.state == trading::app::MarketDataState::Ready &&
            !previousMarket.code.empty() &&
            g_runtimeRunner)
        {
            std::string subscribeError;
            if (!g_runtimeRunner->SubscribeStockTrades(
                    previousMarket.code,
                    subscribeError))
            {
                g_marketDataModule.SetStockTradeSubscriptionRequested(false);
                g_log.Add(
                    "FAULT",
                    "0B 실시간 재등록 실패: %s",
                    subscribeError.c_str());
                std::string healthError;
                g_featureRegistry.SetHealth(
                    "market-data",
                    false,
                    subscribeError,
                    healthError);
            }
            else {
                g_marketDataModule.SetStockTradeSubscriptionRequested(true);
                g_log.Add(
                    "WS",
                    "0B 실시간 재등록 요청: %s",
                    previousMarket.code.c_str());
            }
        }
    }
    else if (id == "chart-workspace") {
        if (!g_chartWorkspaceModule.SetLevel(level, error)) {
            std::string rollbackError;
            g_featureRegistry.SetLevel(
                id,
                previousFeature.level,
                rollbackError);
            return false;
        }
    }

    error.clear();
    return true;
}
'''
shell = replace_once(
    shell,
    old_level,
    new_level,
    "feature-level upstream control",
)
write(SHELL, shell)

runner_test = read(RUNNER_TEST)
runner_test = replace_once(
    runner_test,
    '''        int StockTradeRegistrationCount()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            int count = 0;
            for (const std::string& message : sentMessages_) {
                if (
                    message.find("\\\"type\\\":[\\\"0B\\\"]") !=
                        std::string::npos &&
                    message.find("\\\"refresh\\\":\\\"1\\\"") !=
                        std::string::npos &&
                    message.find("000660") != std::string::npos)
                {
                    ++count;
                }
            }
            return count;
        }

''',
    '''        int StockTradeRegistrationCount()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            int count = 0;
            for (const std::string& message : sentMessages_) {
                if (
                    message.find("\\\"type\\\":[\\\"0B\\\"]") !=
                        std::string::npos &&
                    message.find("\\\"refresh\\\":\\\"1\\\"") !=
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
                    message.find("\\\"trnm\\\":\\\"REMOVE\\\"") !=
                        std::string::npos &&
                    message.find("\\\"type\\\":[\\\"0B\\\"]") !=
                        std::string::npos &&
                    message.find("\\\"refresh\\\"") ==
                        std::string::npos &&
                    message.find("000660") != std::string::npos)
                {
                    ++count;
                }
            }
            return count;
        }

''',
    "fake transport removal count",
)

runner_test = replace_once(
    runner_test,
    '''        WaitUntil(
            [&] { return fake->StockTradeRegistrationCount() >= 2; },
            "runtime did not restore 0B subscription after reconnect");

        Check(!observe.load(std::memory_order_acquire),
''',
    '''        WaitUntil(
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
''',
    "runner removal and reconnect regression",
)
write(RUNNER_TEST, runner_test)

run_all = read(RUN_ALL)
standalone = r'''cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\kiwoom_realtime_subscription_tests.cpp ^
  core\json_lite.cpp ^
  core\kiwoom_realtime_subscription.cpp ^
  /Fe:kiwoom_realtime_subscription_tests.exe
if errorlevel 1 exit /b 1
kiwoom_realtime_subscription_tests.exe
if errorlevel 1 exit /b 1

'''
anchor = '''cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\kiwoom_protocol_tests.cpp ^
'''
run_all = replace_once(
    run_all,
    anchor,
    standalone + anchor,
    "standalone REMOVE protocol test",
)
run_all = replace_once(
    run_all,
    '''  core\kiwoom_protocol.cpp ^
  core\kiwoom_market_data.cpp ^
  core\runtime_config.cpp ^
''',
    '''  core\kiwoom_protocol.cpp ^
  core\kiwoom_realtime_subscription.cpp ^
  core\kiwoom_market_data.cpp ^
  core\runtime_config.cpp ^
''',
    "runtime engine removal linkage",
)
run_all = replace_once(
    run_all,
    '''  core\kiwoom_protocol.cpp ^
  core\kiwoom_market_data.cpp ^
  core\runtime_config.cpp ^
  core\kiwoom_session.cpp ^
  core\trading_state.cpp ^
  core\order_coordinator.cpp ^
  core\kiwoom_events.cpp ^
  core\kiwoom_gateway_core.cpp ^
  core\kiwoom_reconciliation.cpp ^
  core\safe_liquidation.cpp ^
  core\kiwoom_runtime_engine.cpp ^
  platform\kiwoom_runtime_runner.cpp ^
''',
    '''  core\kiwoom_protocol.cpp ^
  core\kiwoom_realtime_subscription.cpp ^
  core\kiwoom_market_data.cpp ^
  core\runtime_config.cpp ^
  core\kiwoom_session.cpp ^
  core\trading_state.cpp ^
  core\order_coordinator.cpp ^
  core\kiwoom_events.cpp ^
  core\kiwoom_gateway_core.cpp ^
  core\kiwoom_reconciliation.cpp ^
  core\safe_liquidation.cpp ^
  core\kiwoom_runtime_engine.cpp ^
  platform\kiwoom_runtime_runner.cpp ^
''',
    "runtime runner removal linkage",
)
write(RUN_ALL, run_all)

ci = read(CI)
ci = replace_once(
    ci,
    "            'SubscribeStockTrades',\n",
    "            'SubscribeStockTrades',\n"
    "            'UnsubscribeStockTrades',\n"
    "            'BuildWebSocketRemovalMessage',\n",
    "CI removal contract",
)
ci = replace_once(
    ci,
    "          if (-not $runnerTests.Contains('message.find(\"\\\"refresh\\\":\\\"1\\\"\")')) {\n",
    "          if (-not $runnerTests.Contains('StockTradeRemovalCount')) {\n"
    "            throw '0B REMOVE and reconnect regression test is missing'\n"
    "          }\n\n"
    "          if (-not $runnerTests.Contains('message.find(\"\\\"refresh\\\":\\\"1\\\"\")')) {\n",
    "CI removal regression test",
)
write(CI, ci)

print("Integrated upstream 0B REMOVE with feature execution levels")
