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
            "\xEF\xBB\xBF"
            "PORT=5007\r\n"
            "# comment\r\n"
            "KIWOOM_MOCK=true\r\n"
            "export KIWOOM_APP_KEY=app-key\r\n"
            "KIWOOM_SECRET_KEY=\"secret\\tvalue\"\r\n"
            "KIWOOM_ACCOUNT='12345678'\r\n";

        std::map<std::string, std::string> values;
        std::string error;

        Check(trading::ParseEnvText(text, values, error),
              "UTF-8 BOM .env text must parse");
        Check(values.at("PORT") == "5007",
              "first BOM-prefixed key parse mismatch");
        Check(values.at("KIWOOM_MOCK") == "true",
              "legacy KIWOOM_MOCK parse mismatch");
        Check(values.at("KIWOOM_APP_KEY") == "app-key",
              "existing App Key parse mismatch");
        Check(values.at("KIWOOM_SECRET_KEY") == "secret\tvalue",
              "quoted secret parse mismatch");
        Check(values.at("KIWOOM_ACCOUNT") == "12345678",
              "single quoted account parse mismatch");

        values.clear();
        Check(!trading::ParseEnvText("INVALID LINE", values, error),
              "invalid .env line must fail");
        Check(error.find("line 1") != std::string::npos,
              "invalid .env error must identify the line");
    }

    void TestLegacyExistingRuntimeConfig()
    {
        std::map<std::string, std::string> values;
        values["PORT"] = "5007";
        values["KIWOOM_MOCK"] = "true";
        values["KIWOOM_APP_KEY"] = "existing-app";
        values["KIWOOM_SECRET_KEY"] = "existing-secret";

        const trading::ConfigLoadResult config =
            trading::BuildRuntimeConfig(
                values,
                "E:\\2026\\gpt\\cpp\\.env");

        Check(config.ok,
              "existing KIWOOM_MOCK=true env contract must work");
        Check(config.config.mode == trading::RuntimeMode::KiwoomMock,
              "legacy mock mode mismatch");
        Check(config.config.appKey == "existing-app",
              "existing KIWOOM_APP_KEY mismatch");
        Check(config.config.secretKey == "existing-secret",
              "existing KIWOOM_SECRET_KEY mismatch");
        Check(!config.warnings.empty(),
              "legacy configuration should produce a compatibility warning");
    }

    void TestCanonicalRuntimeConfig()
    {
        std::map<std::string, std::string> values;
        values["TRADING_MODE"] = "kiwoom_mock";
        values["KIWOOM_MOCK_APP_KEY"] = "mock-app";
        values["KIWOOM_MOCK_SECRET_KEY"] = "mock-secret";
        values["KIWOOM_ACCOUNT"] = "12345678";

        const trading::ConfigLoadResult config =
            trading::BuildRuntimeConfig(values, "mock.env");

        Check(config.ok,
              "canonical KIWOOM_MOCK configuration must be valid");
        Check(config.config.mode == trading::RuntimeMode::KiwoomMock,
              "canonical KIWOOM_MOCK mode mismatch");
        Check(config.config.appKey == "mock-app",
              "mock App Key mismatch");
        Check(config.config.secretKey == "mock-secret",
              "mock App Secret mismatch");
        Check(config.config.accountNumber == "12345678",
              "mock account mismatch");
        Check(config.config.restBaseUrl ==
                  "https://mockapi.kiwoom.com",
              "default mock REST URL mismatch");
        Check(config.config.webSocketUrl ==
                  "wss://mockapi.kiwoom.com:10000/api/dostk/websocket",
              "default mock WebSocket URL mismatch");
    }

    void TestFailureCases()
    {
        std::map<std::string, std::string> emptyValues;
        trading::ConfigLoadResult missingMode =
            trading::BuildRuntimeConfig(emptyValues, "missing.env");

        Check(!missingMode.ok,
              "missing mock mode must fail closed");
        Check(missingMode.config.mode ==
                  trading::RuntimeMode::Unconfigured,
              "missing mode must remain unconfigured");
        Check(missingMode.error.find("missing.env") != std::string::npos,
              "configuration error must identify its source");

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

        std::map<std::string, std::string> disabledValues;
        disabledValues["KIWOOM_MOCK"] = "false";
        disabledValues["KIWOOM_APP_KEY"] = "app";
        disabledValues["KIWOOM_SECRET_KEY"] = "secret";
        const trading::ConfigLoadResult disabled =
            trading::BuildRuntimeConfig(disabledValues);
        Check(!disabled.ok,
              "KIWOOM_MOCK=false must fail closed");

        std::map<std::string, std::string> invalidFlagValues;
        invalidFlagValues["KIWOOM_MOCK"] = "maybe";
        invalidFlagValues["KIWOOM_APP_KEY"] = "app";
        invalidFlagValues["KIWOOM_SECRET_KEY"] = "secret";
        const trading::ConfigLoadResult invalidFlag =
            trading::BuildRuntimeConfig(invalidFlagValues);
        Check(!invalidFlag.ok,
              "invalid KIWOOM_MOCK flag must fail");

        std::map<std::string, std::string> missingCredentialValues;
        missingCredentialValues["KIWOOM_MOCK"] = "true";
        missingCredentialValues["KIWOOM_APP_KEY"] = "app";
        const trading::ConfigLoadResult missingCredential =
            trading::BuildRuntimeConfig(missingCredentialValues);

        Check(!missingCredential.ok,
              "mock mode without both credentials must fail");
        Check(missingCredential.error.find("App Key") !=
                  std::string::npos,
              "missing credentials error mismatch");
    }
}

int main()
{
    TestEnvParser();
    TestLegacyExistingRuntimeConfig();
    TestCanonicalRuntimeConfig();
    TestFailureCases();

    std::puts("[PASS] runtime_config_tests");
    return 0;
}
