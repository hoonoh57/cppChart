#pragma once

#include <map>
#include <string>
#include <vector>

namespace trading
{
    enum class RuntimeMode
    {
        Unconfigured,
        KiwoomMock
    };

    struct RuntimeConfig final
    {
        RuntimeMode mode = RuntimeMode::Unconfigured;
        std::string sourcePath;
        std::string appKey;
        std::string secretKey;
        std::string accountNumber;
        std::string restBaseUrl = "https://mockapi.kiwoom.com";
        std::string webSocketUrl =
            "wss://mockapi.kiwoom.com:10000/api/dostk/websocket";

        bool HasKiwoomCredentials() const noexcept
        {
            return !appKey.empty() && !secretKey.empty();
        }
    };

    struct ConfigLoadResult final
    {
        bool ok = false;
        RuntimeConfig config;
        std::string error;
        std::vector<std::string> warnings;
    };

    bool ParseEnvText(
        const std::string& text,
        std::map<std::string, std::string>& values,
        std::string& error);

    ConfigLoadResult LoadRuntimeConfig(
        const std::string& workingDirectory);

    ConfigLoadResult BuildRuntimeConfig(
        const std::map<std::string, std::string>& values,
        const std::string& sourcePath = {});
}
