#include "stock_pool_mysql_symbol_master.h"

#include <mysql.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>

namespace trading::stock_pool::platform
{
    namespace
    {
        class MysqlHandle final
        {
        public:
            MysqlHandle() : value_(mysql_init(nullptr)) {}

            ~MysqlHandle()
            {
                if (value_ != nullptr) mysql_close(value_);
            }

            MysqlHandle(const MysqlHandle&) = delete;
            MysqlHandle& operator=(const MysqlHandle&) = delete;

            MYSQL* get() const noexcept { return value_; }

        private:
            MYSQL* value_ = nullptr;
        };

        class MysqlResult final
        {
        public:
            explicit MysqlResult(MYSQL_RES* value) : value_(value) {}

            ~MysqlResult()
            {
                if (value_ != nullptr) mysql_free_result(value_);
            }

            MysqlResult(const MysqlResult&) = delete;
            MysqlResult& operator=(const MysqlResult&) = delete;

            MYSQL_RES* get() const noexcept { return value_; }

        private:
            MYSQL_RES* value_ = nullptr;
        };

        std::once_flag g_mysqlLibraryInitOnce;
        int g_mysqlLibraryInitResult = 1;

        std::string Trim(std::string value)
        {
            const auto isSpace = [](unsigned char character) {
                return character == ' ' || character == '\t' ||
                    character == '\r' || character == '\n';
            };
            while (!value.empty() &&
                   isSpace(static_cast<unsigned char>(value.front())))
            {
                value.erase(value.begin());
            }
            while (!value.empty() &&
                   isSpace(static_cast<unsigned char>(value.back())))
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

        bool ParsePort(const std::string& text, unsigned int& port)
        {
            errno = 0;
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
            if (end == text.c_str() || errno == ERANGE || parsed == 0UL ||
                parsed > 65535UL)
            {
                return false;
            }
            while (end != nullptr && *end != '\0') {
                if (*end != ' ' && *end != '\t') return false;
                ++end;
            }
            port = static_cast<unsigned int>(parsed);
            return true;
        }

        bool InitializeMysqlLibrary(std::string& error)
        {
            std::call_once(g_mysqlLibraryInitOnce, [] {
                g_mysqlLibraryInitResult = mysql_library_init(0, nullptr, nullptr);
            });
            if (g_mysqlLibraryInitResult == 0) return true;
            error = "libmysql 초기화에 실패했습니다.";
            return false;
        }

        std::string FieldText(
            MYSQL_ROW row,
            const unsigned long* lengths,
            unsigned int index)
        {
            if (row == nullptr || row[index] == nullptr || lengths == nullptr) {
                return {};
            }
            return std::string(row[index], row[index] + lengths[index]);
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

        if (!InitializeMysqlLibrary(result.error)) return result;

        const std::string host = GetValue(values, "MYSQL_HOST", "127.0.0.1");
        const std::string user = GetValue(values, "MYSQL_USER", "root");
        const std::string password = GetValue(values, "MYSQL_PASSWORD", "");
        const std::string portText = GetValue(values, "MYSQL_PORT", "3306");
        unsigned int port = 3306U;
        if (!ParsePort(portText, port)) {
            result.error = "MYSQL_PORT 값이 올바르지 않습니다: " + portText;
            return result;
        }

        MysqlHandle connection;
        if (connection.get() == nullptr) {
            result.error = "mysql_init()이 null을 반환했습니다.";
            return result;
        }

        unsigned int connectTimeoutSeconds = 5U;
        mysql_options(
            connection.get(),
            MYSQL_OPT_CONNECT_TIMEOUT,
            &connectTimeoutSeconds);
        mysql_options(
            connection.get(),
            MYSQL_SET_CHARSET_NAME,
            "utf8mb4");
        mysql_protocol_type protocol = MYSQL_PROTOCOL_TCP;
        mysql_options(connection.get(), MYSQL_OPT_PROTOCOL, &protocol);

        if (mysql_real_connect(
                connection.get(),
                host.c_str(),
                user.c_str(),
                password.c_str(),
                "gate3",
                port,
                nullptr,
                0UL) == nullptr)
        {
            result.error =
                "gate3 MySQL 접속 실패 [" + host + ":" +
                std::to_string(port) + "]: " +
                mysql_error(connection.get());
            return result;
        }

        if (mysql_set_character_set(connection.get(), "utf8mb4") != 0) {
            result.error =
                "MySQL utf8mb4 설정 실패: " +
                std::string(mysql_error(connection.get()));
            return result;
        }

        constexpr const char* query =
            "SELECT code, name, COALESCE(market, '') "
            "FROM g3_symbol_master "
            "WHERE delisted = 0 ORDER BY name, code";
        if (mysql_real_query(
                connection.get(),
                query,
                static_cast<unsigned long>(std::char_traits<char>::length(query)))
            != 0)
        {
            result.error =
                "gate3.g3_symbol_master 조회 실패: " +
                std::string(mysql_error(connection.get()));
            return result;
        }

        MysqlResult rows(mysql_store_result(connection.get()));
        if (rows.get() == nullptr) {
            if (mysql_field_count(connection.get()) == 0U) {
                result.error =
                    "gate3.g3_symbol_master 조회 결과 필드가 없습니다.";
            }
            else {
                result.error =
                    "gate3.g3_symbol_master 결과 수신 실패: " +
                    std::string(mysql_error(connection.get()));
            }
            return result;
        }

        if (mysql_num_fields(rows.get()) < 3U) {
            result.error =
                "gate3.g3_symbol_master 조회 열 수가 3개보다 적습니다.";
            return result;
        }

        MYSQL_ROW row = nullptr;
        while ((row = mysql_fetch_row(rows.get())) != nullptr) {
            const unsigned long* lengths = mysql_fetch_lengths(rows.get());
            if (lengths == nullptr) {
                result.error =
                    "gate3.g3_symbol_master 행 길이 수신에 실패했습니다.";
                result.entries.clear();
                return result;
            }

            import1516::SymbolMasterEntry entry;
            entry.code = Trim(FieldText(row, lengths, 0U));
            entry.name = Trim(FieldText(row, lengths, 1U));
            entry.market = Trim(FieldText(row, lengths, 2U));
            if (!entry.code.empty() && !entry.name.empty()) {
                result.entries.push_back(std::move(entry));
            }
        }

        if (mysql_errno(connection.get()) != 0U) {
            result.error =
                "gate3.g3_symbol_master 행 순회 실패: " +
                std::string(mysql_error(connection.get()));
            result.entries.clear();
            return result;
        }

        if (result.entries.empty()) {
            result.error =
                "gate3.g3_symbol_master 조회는 성공했지만 종목이 0건입니다.";
            return result;
        }

        result.ok = true;
        result.source =
            "gate3.g3_symbol_master via libmysql " +
            std::string(mysql_get_client_info()) + " @ " + host + ":" +
            std::to_string(port);
        return result;
    }
}
