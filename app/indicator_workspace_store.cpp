#include "indicator_workspace_store.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace trading::app
{
    namespace
    {
        bool Exists(const std::string& path)
        {
            std::error_code error;
            return !path.empty() &&
                std::filesystem::exists(std::filesystem::path(path), error);
        }

        bool LoadSavedOnly(
            const std::string& path,
            IndicatorWorkspaceState& state,
            std::string& error)
        {
            IndicatorWorkspaceSource source = IndicatorWorkspaceSource::None;
            std::string diagnostic;
            if (!LoadIndicatorWorkspaceState(
                    path,
                    std::string{},
                    state,
                    source,
                    diagnostic) ||
                source != IndicatorWorkspaceSource::Saved)
            {
                error = diagnostic.empty()
                    ? "저장 지표 JSON을 직전 저장값으로 읽지 못했습니다."
                    : diagnostic;
                return false;
            }
            error.clear();
            return true;
        }

        bool RestoreBackup(
            const std::filesystem::path& backup,
            const std::filesystem::path& target,
            std::string& error)
        {
            std::error_code fileError;
            if (!std::filesystem::exists(backup, fileError)) {
                error = "복원할 지표 JSON 백업이 없습니다.";
                return false;
            }
            std::filesystem::copy_file(
                backup,
                target,
                std::filesystem::copy_options::overwrite_existing,
                fileError);
            if (fileError) {
                error = "지표 JSON 백업 복원 실패: " + fileError.message();
                return false;
            }
            error.clear();
            return true;
        }

        bool ReadAllText(
            const std::filesystem::path& path,
            std::string& text,
            std::string& error)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                error = "복구할 지표 JSON을 열지 못했습니다.";
                return false;
            }
            std::ostringstream buffer;
            buffer << input.rdbuf();
            if (!input.good() && !input.eof()) {
                error = "복구할 지표 JSON을 읽지 못했습니다.";
                return false;
            }
            text = buffer.str();
            error.clear();
            return true;
        }

        bool WriteAllText(
            const std::filesystem::path& path,
            const std::string& text,
            std::string& error)
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) {
                error = "복구 지표 JSON 임시 파일을 만들지 못했습니다.";
                return false;
            }
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.flush();
            if (!output) {
                error = "복구 지표 JSON 임시 파일을 쓰지 못했습니다.";
                return false;
            }
            error.clear();
            return true;
        }

        bool TryRepairLegacyDoubleQuotedJson(
            const std::filesystem::path& target,
            IndicatorWorkspaceState& state,
            std::string& error)
        {
            std::string text;
            if (!ReadAllText(target, text, error)) return false;

            // The previous serializer wrapped json_lite::EscapeString(), which
            // already includes quotes. Only repair that exact known signature.
            if (text.find("\"id\":\"\"") == std::string::npos ||
                text.find("\"type\":\"\"") == std::string::npos ||
                text.find("\"level\":\"\"") == std::string::npos)
            {
                error = "알려진 이중 인용 지표 JSON 형식이 아닙니다.";
                return false;
            }

            std::string repaired;
            repaired.reserve(text.size());
            for (std::size_t index = 0; index < text.size();) {
                if (index + 1U < text.size() &&
                    text[index] == '"' && text[index + 1U] == '"')
                {
                    repaired.push_back('"');
                    index += 2U;
                }
                else {
                    repaired.push_back(text[index++]);
                }
            }

            const std::filesystem::path temporary = target.string() + ".repair";
            if (!WriteAllText(temporary, repaired, error)) return false;

            IndicatorWorkspaceState repairedState;
            std::string loadError;
            if (!LoadSavedOnly(temporary.string(), repairedState, loadError)) {
                std::error_code removeError;
                std::filesystem::remove(temporary, removeError);
                error = "이중 인용 지표 JSON 복구 검증 실패: " + loadError;
                return false;
            }

            const std::filesystem::path corrupt = target.string() + ".corrupt";
            std::error_code fileError;
            std::filesystem::copy_file(
                target,
                corrupt,
                std::filesystem::copy_options::overwrite_existing,
                fileError);
            if (fileError) {
                std::filesystem::remove(temporary, fileError);
                error = "손상 지표 JSON 보존 실패: " + fileError.message();
                return false;
            }

            fileError.clear();
            std::filesystem::remove(target, fileError);
            fileError.clear();
            std::filesystem::rename(temporary, target, fileError);
            if (fileError) {
                error = "복구 지표 JSON 교체 실패: " + fileError.message();
                return false;
            }

            state = std::move(repairedState);
            error.clear();
            return true;
        }
    }

    bool LoadVerifiedIndicatorWorkspace(
        const std::string& savedPath,
        const std::string& defaultPath,
        IndicatorWorkspaceState& state,
        IndicatorWorkspaceSource& source,
        std::string& diagnostic)
    {
        source = IndicatorWorkspaceSource::None;
        diagnostic.clear();

        const std::filesystem::path saved(savedPath);
        const std::filesystem::path backup(savedPath + ".bak");
        const bool savedExists = Exists(savedPath);
        const bool backupExists = Exists(backup.string());

        if (savedExists) {
            IndicatorWorkspaceState savedState;
            std::string savedError;
            if (LoadSavedOnly(savedPath, savedState, savedError)) {
                state = std::move(savedState);
                source = IndicatorWorkspaceSource::Saved;
                return true;
            }

            std::string repairError;
            IndicatorWorkspaceState repairedState;
            if (TryRepairLegacyDoubleQuotedJson(
                    saved,
                    repairedState,
                    repairError))
            {
                state = std::move(repairedState);
                source = IndicatorWorkspaceSource::Saved;
                diagnostic =
                    "이전 직렬화기의 이중 인용 지표 JSON을 1회 복구했습니다.";
                return true;
            }

            if (backupExists) {
                IndicatorWorkspaceState backupState;
                std::string backupError;
                if (LoadSavedOnly(backup.string(), backupState, backupError)) {
                    std::string restoreError;
                    if (!RestoreBackup(backup, saved, restoreError)) {
                        diagnostic =
                            "저장 지표 JSON 오류: " + savedError +
                            " / 복구 오류: " + repairError +
                            " / 백업은 정상이나 원본 복원 실패: " + restoreError;
                        return false;
                    }
                    state = std::move(backupState);
                    source = IndicatorWorkspaceSource::Saved;
                    diagnostic =
                        "손상된 저장 지표 JSON 대신 마지막 정상 백업을 복원했습니다.";
                    return true;
                }
                diagnostic =
                    "저장 지표 JSON 오류: " + savedError +
                    " / 복구 오류: " + repairError +
                    " / 백업 오류: " + backupError;
                return false;
            }

            diagnostic =
                "저장 지표 JSON 오류로 기본값 대체를 거부했습니다: " +
                savedError + " / 복구 오류: " + repairError;
            return false;
        }

        if (backupExists) {
            IndicatorWorkspaceState backupState;
            std::string backupError;
            if (LoadSavedOnly(backup.string(), backupState, backupError)) {
                std::string restoreError;
                if (!RestoreBackup(backup, saved, restoreError)) {
                    diagnostic =
                        "지표 JSON 백업은 정상이나 원본 복원 실패: " +
                        restoreError;
                    return false;
                }
                state = std::move(backupState);
                source = IndicatorWorkspaceSource::Saved;
                diagnostic = "지표 JSON 백업에서 직전 상태를 복원했습니다.";
                return true;
            }
            diagnostic = "지표 JSON 백업 오류: " + backupError;
            return false;
        }

        return LoadIndicatorWorkspaceState(
            std::string{},
            defaultPath,
            state,
            source,
            diagnostic);
    }

    bool SaveVerifiedIndicatorWorkspace(
        const std::string& savedPath,
        const IndicatorWorkspaceState& state,
        std::string& error)
    {
        std::string expected;
        if (!SerializeIndicatorWorkspaceState(state, expected, error)) {
            return false;
        }

        const std::filesystem::path target(savedPath);
        const std::filesystem::path backup(savedPath + ".bak");
        const bool targetExists = Exists(savedPath);

        if (targetExists) {
            IndicatorWorkspaceState current;
            std::string currentError;
            if (!LoadSavedOnly(savedPath, current, currentError)) {
                error =
                    "기존 저장 지표 JSON이 손상되어 덮어쓰기를 거부했습니다: " +
                    currentError;
                return false;
            }

            std::error_code fileError;
            std::filesystem::copy_file(
                target,
                backup,
                std::filesystem::copy_options::overwrite_existing,
                fileError);
            if (fileError) {
                error = "기존 지표 JSON 백업 실패: " + fileError.message();
                return false;
            }
        }

        if (!SaveIndicatorWorkspaceState(savedPath, state, error)) {
            return false;
        }

        IndicatorWorkspaceState reloaded;
        std::string reloadError;
        if (!LoadSavedOnly(savedPath, reloaded, reloadError)) {
            std::string restoreError;
            if (targetExists) {
                RestoreBackup(backup, target, restoreError);
            }
            error = "저장 직후 지표 JSON 재검증 실패: " + reloadError;
            if (!restoreError.empty()) error += " / " + restoreError;
            return false;
        }

        std::string actual;
        if (!SerializeIndicatorWorkspaceState(reloaded, actual, reloadError) ||
            actual != expected)
        {
            std::string restoreError;
            if (targetExists) {
                RestoreBackup(backup, target, restoreError);
            }
            error = "저장 지표 JSON 왕복 결과가 요청 상태와 다릅니다.";
            if (!reloadError.empty()) error += " " + reloadError;
            if (!restoreError.empty()) error += " / " + restoreError;
            return false;
        }

        error.clear();
        return true;
    }
}
