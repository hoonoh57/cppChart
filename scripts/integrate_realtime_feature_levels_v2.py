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
subscribe_end = '''        TryQueueStockTradeSubscription();
        error.clear();
        return true;
    }

    bool KiwoomRuntimeRunner::SubmitOrder(
'''
unsubscribe = '''        TryQueueStockTradeSubscription();
        error.clear();
        return true;
    }

    bool KiwoomRuntimeRunner::UnsubscribeStockTrades(
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

    bool KiwoomRuntimeRunner::SubmitOrder(
'''
runner = replace_once(
    runner,
    subscribe_end,
    unsubscribe,
    "runner unsubscribe implementation",
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
            if (g_runtimeRunner && !previousMarket.code.empty()) {
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
registration_method = '''        int StockTradeRegistrationCount()
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

'''
registration_and_removal = registration_method + '''        int StockTradeRemovalCount()
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

'''
runner_test = replace_once(
    runner_test,
    registration_method,
    registration_and_removal,
    "fake transport removal counter",
)

reconnect_anchor = '''        WaitUntil(
            [&] { return fake->StockTradeRegistrationCount() >= 2; },
            "runtime did not restore 0B subscription after reconnect");

        Check(!observe.load(std::memory_order_acquire),
'''
reconnect_replacement = '''        WaitUntil(
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
'''
runner_test = replace_once(
    runner_test,
    reconnect_anchor,
    reconnect_replacement,
    "runner removal reconnect regression",
)
write(RUNNER_TEST, runner_test)

run_all = read(RUN_ALL)
protocol_anchor = '''cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\kiwoom_protocol_tests.cpp ^
'''
standalone = r'''cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc ^
  tests\kiwoom_realtime_subscription_tests.cpp ^
  core\json_lite.cpp ^
  core\kiwoom_realtime_subscription.cpp ^
  /Fe:kiwoom_realtime_subscription_tests.exe
if errorlevel 1 exit /b 1
kiwoom_realtime_subscription_tests.exe
if errorlevel 1 exit /b 1

'''
run_all = replace_once(
    run_all,
    protocol_anchor,
    standalone + protocol_anchor,
    "REMOVE protocol test insertion",
)
runner_link = '''  core\kiwoom_runtime_engine.cpp ^
  platform\kiwoom_runtime_runner.cpp ^
  /Fe:kiwoom_runtime_runner_tests.exe
'''
runner_link_with_remove = '''  core\kiwoom_runtime_engine.cpp ^
  core\kiwoom_realtime_subscription.cpp ^
  platform\kiwoom_runtime_runner.cpp ^
  /Fe:kiwoom_runtime_runner_tests.exe
'''
run_all = replace_once(
    run_all,
    runner_link,
    runner_link_with_remove,
    "runner removal linkage",
)
write(RUN_ALL, run_all)

ci = read(CI)
old_runner_gate = '''          if (-not $runner.Contains('SubscribeStockTrades') -or
              -not $runner.Contains('{ "0B" }')) {
            throw 'Selected-stock 0B subscription is not integrated'
          }
          if (-not $runnerTests.Contains('message.find("\"refresh\":\"1\"")')) {
            throw '0B registration does not have a keep-existing regression gate'
          }
'''
new_runner_gate = '''          if (-not $runner.Contains('SubscribeStockTrades') -or
              -not $runner.Contains('{ "0B" }')) {
            throw 'Selected-stock 0B subscription is not integrated'
          }
          if (-not $runner.Contains('UnsubscribeStockTrades') -or
              -not $runner.Contains('BuildWebSocketRemovalMessage')) {
            throw 'Selected-stock 0B removal is not integrated'
          }
          if (-not $runnerTests.Contains('StockTradeRemovalCount') -or
              -not $runnerTests.Contains('must not return after reconnect')) {
            throw '0B REMOVE and reconnect regression gate is missing'
          }
          if (-not $runnerTests.Contains('message.find("\"refresh\":\"1\"")')) {
            throw '0B registration does not have a keep-existing regression gate'
          }
'''
ci = replace_once(
    ci,
    old_runner_gate,
    new_runner_gate,
    "CI upstream removal gate",
)
write(CI, ci)

print("Integrated upstream 0B REMOVE with feature execution levels")
