#include "../core/runtime_config.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

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

    void TestEnvParser()
    {
        const std::string text =
            "# comment\r\n"
            "TRADING_MODE=KIWOOM_MOCK\r\n"
            "export KIWOOM_MOCK_APP_KEY=app-key\r\n"
            "KIWOOM_MOCK_SECRET_KEY=\"secret\\tvalue\"\r\n"
            "KIWOOM_ACCOUNT='12345678'\r\n";

        std::map<std::string, std::string> values;
        std::string error;

        Check(trading::ParseEnvText(text, values, error),
              "valid .env text must parse");
        Check(values.at("TRADING_MODE") == "KIWOOM_MOCK",
              "TRADING_MODE parse mismatch");
        Check(values.at("KIWOOM_MOCK_APP_KEY") == "app-key",
              "App Key parse mismatch");
        Check(values.at("KIWOOM_MOCK_SECRET_KEY") == "secret\tvalue",
              "quoted secret parse mismatch");
        Check(values.at("KIWOOM_ACCOUNT") == "12345678",
              "single quoted account parse mismatch");

        values.clear();
        Check(!trading::ParseEnvText("INVALID LINE", values, error),
              "invalid .env line must fail");
        Check(error.find("line 1") != std::string::npos,
              "invalid .env error must identify the line");
    }

    void TestRuntimeConfig()
    {
        std::map<std::string, std::string> emptyValues;
        trading::ConfigLoadResult missingMode =
            trading::BuildRuntimeConfig(emptyValues, "missing.env");

        Check(!missingMode.ok,
              "missing TRADING_MODE must fail closed");
        Check(missingMode.config.mode ==
                  trading::RuntimeMode::Unconfigured,
              "missing mode must remain unconfigured");

        std::map<std::string, std::string> removedLocalValues;
        removedLocalValues["TRADING_MODE"] = "LOCAL_MOCK";
        trading::ConfigLoadResult removedLocal =
            trading::BuildRuntimeConfig(
                removedLocalValues,
                "local.env");

        Check(!removedLocal.ok,
              "removed LOCAL_MOCK mode must be rejected");
        Check(removedLocal.error.find("removed") != std::string::npos,
              "LOCAL_MOCK rejection must explain its removal");

        std::map<std::string, std::string> mockValues;
        mockValues["TRADING_MODE"] = "KIWOOM_MOCK";
        mockValues["KIWOOM_MOCK_APP_KEY"] = "mock-app";
        mockValues["KIWOOM_MOCK_SECRET_KEY"] = "mock-secret";
        mockValues["KIWOOM_ACCOUNT"] = "12345678";

        trading::ConfigLoadResult mock =
            trading::BuildRuntimeConfig(mockValues, "mock.env");

        Check(mock.ok, "KIWOOM_MOCK configuration must be valid");
        Check(mock.config.mode == trading::RuntimeMode::KiwoomMock,
              "KIWOOM_MOCK mode mismatch");
        Check(mock.config.appKey == "mock-app",
              "mock App Key mismatch");
        Check(mock.config.secretKey == "mock-secret",
              "mock App Secret mismatch");
        Check(mock.config.accountNumber == "12345678",
              "mock account mismatch");
        Check(mock.config.restBaseUrl ==
                  "https://mockapi.kiwoom.com",
              "default mock REST URL mismatch");
        Check(mock.config.webSocketUrl ==
                  "wss://mockapi.kiwoom.com:10000/api/dostk/websocket",
              "default mock WebSocket URL mismatch");

        mockValues.erase("KIWOOM_MOCK_SECRET_KEY");
        trading::ConfigLoadResult missingCredential =
            trading::BuildRuntimeConfig(mockValues);

        Check(!missingCredential.ok,
              "KIWOOM_MOCK without credentials must fail");
        Check(missingCredential.error.find("App Key") !=
                  std::string::npos,
              "missing credentials error mismatch");
    }
}

int main()
{
    TestEnvParser();
    TestRuntimeConfig();

    std::puts("[PASS] runtime_config_tests");
    return 0;
}
