#include "stock_pool_mysql_symbol_master.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace trading::stock_pool::platform
{
    namespace
    {
        struct TemporaryFiles final
        {
            std::filesystem::path option;
            std::filesystem::path output;
            std::filesystem::path error;

            ~TemporaryFiles()
            {
                std::error_code ignored;
                if (!option.empty()) std::filesystem::remove(option, ignored);
                if (!output.empty()) std::filesystem::remove(output, ignored);
                if (!error.empty()) std::filesystem::remove(error, ignored);
            }
        };

        std::string Trim(std::string value)
        {
            const auto isSpace = [](unsigned char character) {
                return character == ' ' || character == '\t' ||
                    character == '\r' || character == '\n';
            };
            while (!value.empty() && isSpace(
                       static_cast<unsigned char>(value.front())))
            {
                value.erase(value.begin());
            }
            while (!value.empty() && isSpace(
                       static_cast<unsigned char>(value.back())))
            {
                value.pop_back();
            }
            if (value.size() >= 2U &&
                ((value.front() == '"' && value.back() == '"') ||
                 (value.front() == '\'' && value.back() == '\'')))
            {
                value = value.substr(1U, value.size() - 2U);
            }
            return value;
        }

        std::unordered_map<std::string, std::string> ReadEnvironment(
            const std::string& path,
            std::string& error)
        {
            std::unordered_map<std::string, std::string> values;
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                error = path + " 파일을 열 수 없습니다.";
                return values;
            }
            std::string line;
            bool first = true;
            while (std::getline(input, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (first && line.size() >= 3U &&
                    static_cast<unsigned char>(line[0]) == 0xEFU &&
                    static_cast<unsigned char>(line[1]) == 0xBBU &&
                    static_cast<unsigned char>(line[2]) == 0xBFU)
                {
                    line.erase(0U, 3U);
                }
                first = false;
                const std::string trimmed = Trim(line);
                if (trimmed.empty() || trimmed.front() == '#') continue;
                const std::size_t equals = trimmed.find('=');
                if (equals == std::string::npos) continue;
                const std::string key = Trim(trimmed.substr(0U, equals));
                const std::string value = Trim(trimmed.substr(equals + 1U));
                if (!key.empty()) values[key] = value;
            }
            return values;
        }

        std::string GetValue(
            const std::unordered_map<std::string, std::string>& values,
            const char* key,
            const char* fallback)
        {
            const auto found = values.find(key);
            return found != values.end() ? found->second : fallback;
        }

        std::wstring Utf8ToWide(const std::string& value)
        {
            if (value.empty()) return {};
            const int length = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            if (length <= 0) return {};
            std::wstring result(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                result.data(),
                length);
            return result;
        }

        std::string WideToUtf8(const std::wstring& value)
        {
            if (value.empty()) return {};
            const int length = WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            if (length <= 0) return {};
            std::string result(static_cast<std::size_t>(length), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                result.data(),
                length,
                nullptr,
                nullptr);
            return result;
        }

        std::wstring FindMysqlExecutable(
            const std::unordered_map<std::string, std::string>& values)
        {
            const std::string configured = GetValue(
                values,
                "MYSQL_EXE",
                "");
            if (!configured.empty()) {
                const std::wstring path = Utf8ToWide(configured);
                if (!path.empty() &&
                    GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    return path;
                }
            }

            DWORD required = SearchPathW(
                nullptr,
                L"mysql.exe",
                nullptr,
                0,
                nullptr,
                nullptr);
            if (required == 0U) return {};
            std::wstring result(static_cast<std::size_t>(required), L'\0');
            const DWORD written = SearchPathW(
                nullptr,
                L"mysql.exe",
                nullptr,
                required,
                result.data(),
                nullptr);
            if (written == 0U) return {};
            result.resize(static_cast<std::size_t>(written));
            return result;
        }

        std::string EscapeOptionValue(const std::string& value)
        {
            std::string result;
            result.reserve(value.size() + 8U);
            for (const char character : value) {
                if (character == '\\' || character == '"') {
                    result.push_back('\\');
                }
                if (character == '\r' || character == '\n') continue;
                result.push_back(character);
            }
            return result;
        }

        bool MakeTemporaryFile(
            const wchar_t* prefix,
            std::filesystem::path& path,
            std::string& error)
        {
            wchar_t directory[MAX_PATH + 1]{};
            const DWORD directoryLength = GetTempPathW(MAX_PATH, directory);
            if (directoryLength == 0U || directoryLength > MAX_PATH) {
                error = "Windows 임시 폴더를 찾지 못했습니다.";
                return false;
            }
            wchar_t file[MAX_PATH + 1]{};
            if (GetTempFileNameW(directory, prefix, 0U, file) == 0U) {
                error = "Windows 임시 파일을 만들지 못했습니다.";
                return false;
            }
            path = file;
            return true;
        }

        std::string ReadFileUtf8(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) return {};
            std::ostringstream buffer;
            buffer << input.rdbuf();
            return buffer.str();
        }

        std::wstring QuoteCommandArgument(const std::wstring& value)
        {
            std::wstring result = L"\"";
            std::size_t backslashes = 0U;
            for (const wchar_t character : value) {
                if (character == L'\\') {
                    ++backslashes;
                    continue;
                }
                if (character == L'"') {
                    result.append(backslashes * 2U + 1U, L'\\');
                    result.push_back(L'"');
                    backslashes = 0U;
                    continue;
                }
                result.append(backslashes, L'\\');
                backslashes = 0U;
                result.push_back(character);
            }
            result.append(backslashes * 2U, L'\\');
            result.push_back(L'"');
            return result;
        }

        bool RunMysql(
            const std::wstring& executable,
            const std::filesystem::path& optionFile,
            const std::filesystem::path& outputFile,
            const std::filesystem::path& errorFile,
            DWORD& exitCode,
            std::string& error)
        {
            HANDLE output = CreateFileW(
                outputFile.c_str(),
                GENERIC_WRITE,
                FILE_SHARE_READ,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_TEMPORARY,
                nullptr);
            HANDLE errors = CreateFileW(
                errorFile.c_str(),
                GENERIC_WRITE,
                FILE_SHARE_READ,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_TEMPORARY,
                nullptr);
            HANDLE input = CreateFileW(
                L"NUL",
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (output == INVALID_HANDLE_VALUE ||
                errors == INVALID_HANDLE_VALUE ||
                input == INVALID_HANDLE_VALUE)
            {
                if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
                if (errors != INVALID_HANDLE_VALUE) CloseHandle(errors);
                if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
                error = "MySQL 출력용 임시 파일을 열지 못했습니다.";
                return false;
            }

            SetHandleInformation(output, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
            SetHandleInformation(errors, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
            SetHandleInformation(input, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

            const std::wstring query =
                L"SELECT code, name, COALESCE(market, '') "
                L"FROM gate3.g3_symbol_master "
                L"WHERE delisted = 0 ORDER BY name, code";
            std::wstring command = QuoteCommandArgument(executable);
            command += L" --defaults-extra-file=" +
                QuoteCommandArgument(optionFile.wstring());
            command +=
                L" --batch --raw --skip-column-names "
                L"--default-character-set=utf8mb4 -e ";
            command += QuoteCommandArgument(query);

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = input;
            startup.hStdOutput = output;
            startup.hStdError = errors;
            PROCESS_INFORMATION process{};
            const BOOL started = CreateProcessW(
                executable.c_str(),
                command.data(),
                nullptr,
                nullptr,
                TRUE,
                CREATE_NO_WINDOW,
                nullptr,
                nullptr,
                &startup,
                &process);

            CloseHandle(output);
            CloseHandle(errors);
            CloseHandle(input);
            if (!started) {
                error = "mysql.exe 실행 실패: Windows error " +
                    std::to_string(GetLastError());
                return false;
            }

            WaitForSingleObject(process.hProcess, INFINITE);
            exitCode = 1U;
            GetExitCodeProcess(process.hProcess, &exitCode);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return true;
        }

        std::vector<import1516::SymbolMasterEntry> ParseMasterOutput(
            const std::string& text)
        {
            std::vector<import1516::SymbolMasterEntry> entries;
            std::istringstream stream(text);
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const std::size_t first = line.find('\t');
                if (first == std::string::npos) continue;
                const std::size_t second = line.find('\t', first + 1U);
                import1516::SymbolMasterEntry entry;
                entry.code = line.substr(0U, first);
                entry.name = second == std::string::npos
                    ? line.substr(first + 1U)
                    : line.substr(first + 1U, second - first - 1U);
                entry.market = second == std::string::npos
                    ? std::string{}
                    : line.substr(second + 1U);
                entry.code = Trim(entry.code);
                entry.name = Trim(entry.name);
                entry.market = Trim(entry.market);
                if (!entry.code.empty() && !entry.name.empty()) {
                    entries.push_back(std::move(entry));
                }
            }
            return entries;
        }
    }

    SymbolMasterLoadResult LoadGate3SymbolMaster(
        const std::string& environmentFilePath)
    {
        SymbolMasterLoadResult result;
        std::string environmentError;
        const auto values = ReadEnvironment(
            environmentFilePath,
            environmentError);
        if (!environmentError.empty()) {
            result.error = environmentError;
            return result;
        }

        const std::wstring executable = FindMysqlExecutable(values);
        if (executable.empty()) {
            result.error =
                "mysql.exe를 찾지 못했습니다. PATH 또는 .env의 MYSQL_EXE를 설정하십시오.";
            return result;
        }

        TemporaryFiles files;
        if (!MakeTemporaryFile(L"spc", files.option, result.error) ||
            !MakeTemporaryFile(L"spo", files.output, result.error) ||
            !MakeTemporaryFile(L"spe", files.error, result.error))
        {
            return result;
        }

        const std::string host = GetValue(values, "MYSQL_HOST", "127.0.0.1");
        const std::string port = GetValue(values, "MYSQL_PORT", "3306");
        const std::string user = GetValue(values, "MYSQL_USER", "root");
        const std::string password = GetValue(values, "MYSQL_PASSWORD", "");

        {
            std::ofstream option(files.option, std::ios::binary | std::ios::trunc);
            if (!option) {
                result.error = "MySQL 임시 client 설정 파일을 쓸 수 없습니다.";
                return result;
            }
            option << "[client]\n";
            option << "host=\"" << EscapeOptionValue(host) << "\"\n";
            option << "port=\"" << EscapeOptionValue(port) << "\"\n";
            option << "user=\"" << EscapeOptionValue(user) << "\"\n";
            option << "password=\"" << EscapeOptionValue(password) << "\"\n";
            option << "default-character-set=utf8mb4\n";
        }

        DWORD exitCode = 1U;
        if (!RunMysql(
                executable,
                files.option,
                files.output,
                files.error,
                exitCode,
                result.error))
        {
            return result;
        }

        const std::string stderrText = Trim(ReadFileUtf8(files.error));
        if (exitCode != 0U) {
            result.error = "gate3.g3_symbol_master 조회 실패";
            if (!stderrText.empty()) result.error += ": " + stderrText;
            return result;
        }

        result.entries = ParseMasterOutput(ReadFileUtf8(files.output));
        if (result.entries.empty()) {
            result.error =
                "gate3.g3_symbol_master 조회는 성공했지만 종목이 0건입니다.";
            return result;
        }

        result.ok = true;
        result.source = "gate3.g3_symbol_master via " +
            WideToUtf8(executable);
        return result;
    }
}
