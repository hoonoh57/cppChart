#include "runtime_config.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace trading
{
    namespace
    {
        std::string Trim(const std::string& value)
        {
            std::size_t first = 0;
            while (
                first < value.size() &&
                std::isspace(static_cast<unsigned char>(value[first])) != 0)
            {
                ++first;
            }

            std::size_t last = value.size();
            while (
                last > first &&
                std::isspace(static_cast<unsigned char>(value[last - 1])) != 0)
            {
                --last;
            }

            return value.substr(first, last - first);
        }

        bool IsValidKey(const std::string& key) noexcept
        {
            if (key.empty()) return false;

            const unsigned char first =
                static_cast<unsigned char>(key.front());

            if (
                std::isalpha(first) == 0 &&
                key.front() != '_')
            {
                return false;
            }

            for (char ch : key) {
                const unsigned char value =
                    static_cast<unsigned char>(ch);

                if (
                    std::isalnum(value) == 0 &&
                    ch != '_')
                {
                    return false;
                }
            }

            return true;
        }

        std::string Unquote(
            const std::string& value,
            bool& ok)
        {
            ok = true;
            if (value.size() < 2) return value;

            const char quote = value.front();
            if (quote != '\'' && quote != '"') return value;

            if (value.back() != quote) {
                ok = false;
                return {};
            }

            const std::string inner =
                value.substr(1, value.size() - 2);

            if (quote == '\'') return inner;

            std::string result;
            result.reserve(inner.size());

            for (std::size_t index = 0; index < inner.size(); ++index) {
                const char ch = inner[index];
                if (ch != '\\') {
                    result.push_back(ch);
                    continue;
                }

                if (index + 1 >= inner.size()) {
                    ok = false;
                    return {};
                }

                const char escaped = inner[++index];
                switch (escaped) {
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case '\\': result.push_back('\\'); break;
                case '"': result.push_back('"'); break;
                default:
                    result.push_back(escaped);
                    break;
                }
            }

            return result;
        }

        std::string ReadEnvironment(const char* name)
        {
            const char* value = std::getenv(name);
            return value != nullptr ? std::string(value) : std::string();
        }

        void OverlayEnvironment(
            std::map<std::string, std::string>& values,
            const char* key)
        {
            const std::string value = ReadEnvironment(key);
            if (!value.empty()) values[key] = value;
        }

        std::string FindValue(
            const std::map<std::string, std::string>& values,
            const char* primary,
            const char* fallback = nullptr)
        {
            auto found = values.find(primary);
            if (found != values.end() && !found->second.empty()) {
                return found->second;
            }

            if (fallback != nullptr) {
                found = values.find(fallback);
                if (found != values.end()) return found->second;
            }

            return {};
        }
    }

    bool ParseEnvText(
        const std::string& text,
        std::map<std::string, std::string>& values,
        std::string& error)
    {
        std::istringstream stream(text);
        std::string line;
        int lineNumber = 0;

        while (std::getline(stream, line)) {
            ++lineNumber;

            if (
                !line.empty() &&
                line.back() == '\r')
            {
                line.pop_back();
            }

            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed.front() == '#') continue;

            if (trimmed.rfind("export ", 0) == 0) {
                trimmed = Trim(trimmed.substr(7));
            }

            const std::size_t separator = trimmed.find('=');
            if (separator == std::string::npos) {
                error =
                    "invalid environment line " +
                    std::to_string(lineNumber);
                return false;
            }

            const std::string key =
                Trim(trimmed.substr(0, separator));

            if (!IsValidKey(key)) {
                error =
                    "invalid environment key at line " +
                    std::to_string(lineNumber);
                return false;
            }

            const std::string rawValue =
                Trim(trimmed.substr(separator + 1));

            bool quoteOk = false;
            const std::string value =
                Unquote(rawValue, quoteOk);

            if (!quoteOk) {
                error =
                    "unterminated quoted value at line " +
                    std::to_string(lineNumber);
                return false;
            }

            values[key] = value;
        }

        error.clear();
        return true;
    }

    ConfigLoadResult LoadRuntimeConfig(
        const std::string& workingDirectory)
    {
        ConfigLoadResult result;
        namespace fs = std::filesystem;

        std::vector<fs::path> candidates;
        const std::string explicitPath =
            ReadEnvironment("TRADING_CONFIG");

        if (!explicitPath.empty()) {
            candidates.emplace_back(explicitPath);
        }

        const fs::path working =
            workingDirectory.empty()
                ? fs::current_path()
                : fs::path(workingDirectory);

        candidates.push_back(working / ".env");
        candidates.push_back(working.parent_path() / ".env");

        std::map<std::string, std::string> values;
        std::string selectedPath;

        for (const fs::path& candidate : candidates) {
            std::error_code existsError;
            if (!fs::exists(candidate, existsError) || existsError) continue;

            std::ifstream stream(candidate, std::ios::binary);
            if (!stream) {
                result.error =
                    "cannot open runtime configuration file";
                return result;
            }

            std::ostringstream content;
            content << stream.rdbuf();
            if (!stream.good() && !stream.eof()) {
                result.error =
                    "cannot read runtime configuration file";
                return result;
            }

            if (!ParseEnvText(content.str(), values, result.error)) {
                return result;
            }

            selectedPath = candidate.string();
            break;
        }

        const char* overrideKeys[] = {
            "TRADING_MODE",
            "KIWOOM_MOCK_APP_KEY",
            "KIWOOM_MOCK_SECRET_KEY",
            "KIWOOM_APP_KEY",
            "KIWOOM_SECRET_KEY",
            "KIWOOM_ACCOUNT",
            "KIWOOM_REST_BASE_URL",
            "KIWOOM_WEBSOCKET_URL"
        };

        for (const char* key : overrideKeys) {
            OverlayEnvironment(values, key);
        }

        return BuildRuntimeConfig(values, selectedPath);
    }

    ConfigLoadResult BuildRuntimeConfig(
        const std::map<std::string, std::string>& values,
        const std::string& sourcePath)
    {
        ConfigLoadResult result;
        result.config.sourcePath = sourcePath;

        const std::string mode =
            FindValue(values, "TRADING_MODE");

        if (mode.empty() || mode == "LOCAL_MOCK") {
            result.config.mode = RuntimeMode::LocalMock;
        }
        else if (mode == "KIWOOM_MOCK") {
            result.config.mode = RuntimeMode::KiwoomMock;
        }
        else {
            result.error = "TRADING_MODE must be LOCAL_MOCK or KIWOOM_MOCK";
            return result;
        }

        result.config.appKey =
            FindValue(
                values,
                "KIWOOM_MOCK_APP_KEY",
                "KIWOOM_APP_KEY");

        result.config.secretKey =
            FindValue(
                values,
                "KIWOOM_MOCK_SECRET_KEY",
                "KIWOOM_SECRET_KEY");

        result.config.accountNumber =
            FindValue(values, "KIWOOM_ACCOUNT");

        const std::string restBase =
            FindValue(values, "KIWOOM_REST_BASE_URL");
        if (!restBase.empty()) {
            result.config.restBaseUrl = restBase;
        }

        const std::string webSocket =
            FindValue(values, "KIWOOM_WEBSOCKET_URL");
        if (!webSocket.empty()) {
            result.config.webSocketUrl = webSocket;
        }

        if (
            result.config.mode == RuntimeMode::KiwoomMock &&
            !result.config.HasKiwoomCredentials())
        {
            result.error =
                "KIWOOM_MOCK mode requires an App Key and App Secret";
            return result;
        }

        if (
            result.config.mode == RuntimeMode::LocalMock &&
            result.config.HasKiwoomCredentials())
        {
            result.warnings.push_back(
                "Kiwoom credentials are present but LOCAL_MOCK is active");
        }

        result.ok = true;
        return result;
    }
}
