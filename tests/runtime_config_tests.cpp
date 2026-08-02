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
        std::map<std::string, std::string> localValues;
        localValues["TRADING_MODE"] = "LOCAL_MOCK";

        trading::ConfigLoadResult local =
            trading::BuildRuntimeConfig(localValues, "local.env");

        Check(local.ok, "LOCAL_MOCK configuration must be valid");
        Check(local.config.mode == trading::RuntimeMode::LocalMock,
              "LOCAL_MOCK mode mismatch");
        Check(local.config.sourcePath == "local.env",
              "configuration source path mismatch");

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
        trading::ConfigLoadResult missing =
            trading::BuildRuntimeConfig(mockValues);

        Check(!missing.ok,
              "KIWOOM_MOCK without credentials must fail");
        Check(missing.error.find("App Key") != std::string::npos,
              "missing credentials error mismatch");

        mockValues["TRADING_MODE"] = "UNKNOWN";
        trading::ConfigLoadResult invalidMode =
            trading::BuildRuntimeConfig(mockValues);

        Check(!invalidMode.ok, "unknown runtime mode must fail");
    }
}

int main()
{
    TestEnvParser();
    TestRuntimeConfig();

    std::puts("[PASS] runtime_config_tests");
    return 0;
}
