#include "comparison_workspace_store.h"

#include <filesystem>
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
            ComparisonWorkspaceState& state,
            std::string& error)
        {
            ComparisonWorkspaceSource source = ComparisonWorkspaceSource::None;
            std::string diagnostic;
            if (!LoadComparisonWorkspaceState(
                    path,
                    std::string{},
                    state,
                    source,
                    diagnostic) ||
                source != ComparisonWorkspaceSource::Saved)
            {
                error = diagnostic.empty()
                    ? "저장 비교 JSON을 직전 저장값으로 읽지 못했습니다."
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
                error = "복원할 비교 JSON 백업이 없습니다.";
                return false;
            }
            std::filesystem::copy_file(
                backup,
                target,
                std::filesystem::copy_options::overwrite_existing,
                fileError);
            if (fileError) {
                error = "비교 JSON 백업 복원 실패: " + fileError.message();
                return false;
            }
            error.clear();
            return true;
        }
    }

    bool LoadVerifiedComparisonWorkspace(
        const std::string& savedPath,
        const std::string& defaultPath,
        ComparisonWorkspaceState& state,
        ComparisonWorkspaceSource& source,
        std::string& diagnostic)
    {
        source = ComparisonWorkspaceSource::None;
        diagnostic.clear();

        const std::filesystem::path saved(savedPath);
        const std::filesystem::path backup(savedPath + ".bak");
        const bool savedExists = Exists(savedPath);
        const bool backupExists = Exists(backup.string());

        if (savedExists) {
            ComparisonWorkspaceState savedState;
            std::string savedError;
            if (LoadSavedOnly(savedPath, savedState, savedError)) {
                state = std::move(savedState);
                source = ComparisonWorkspaceSource::Saved;
                return true;
            }

            if (backupExists) {
                ComparisonWorkspaceState backupState;
                std::string backupError;
                if (LoadSavedOnly(backup.string(), backupState, backupError)) {
                    std::string restoreError;
                    if (!RestoreBackup(backup, saved, restoreError)) {
                        diagnostic =
                            "저장 비교 JSON 오류: " + savedError +
                            " / 백업은 정상이나 원본 복원 실패: " + restoreError;
                        return false;
                    }
                    state = std::move(backupState);
                    source = ComparisonWorkspaceSource::Saved;
                    diagnostic =
                        "손상된 저장 비교 JSON 대신 마지막 정상 백업을 복원했습니다.";
                    return true;
                }
                diagnostic =
                    "저장 비교 JSON 오류: " + savedError +
                    " / 백업 오류: " + backupError;
                return false;
            }

            diagnostic =
                "저장 비교 JSON 오류로 기본값 대체를 거부했습니다: " +
                savedError;
            return false;
        }

        if (backupExists) {
            ComparisonWorkspaceState backupState;
            std::string backupError;
            if (LoadSavedOnly(backup.string(), backupState, backupError)) {
                std::string restoreError;
                if (!RestoreBackup(backup, saved, restoreError)) {
                    diagnostic =
                        "비교 JSON 백업은 정상이나 원본 복원 실패: " +
                        restoreError;
                    return false;
                }
                state = std::move(backupState);
                source = ComparisonWorkspaceSource::Saved;
                diagnostic = "비교 JSON 백업에서 직전 상태를 복원했습니다.";
                return true;
            }
            diagnostic = "비교 JSON 백업 오류: " + backupError;
            return false;
        }

        return LoadComparisonWorkspaceState(
            std::string{},
            defaultPath,
            state,
            source,
            diagnostic);
    }

    bool SaveVerifiedComparisonWorkspace(
        const std::string& savedPath,
        const ComparisonWorkspaceState& state,
        std::string& error)
    {
        std::string expected;
        if (!SerializeComparisonWorkspaceState(state, expected, error)) {
            return false;
        }

        const std::filesystem::path target(savedPath);
        const std::filesystem::path backup(savedPath + ".bak");
        const bool targetExists = Exists(savedPath);

        if (targetExists) {
            ComparisonWorkspaceState current;
            std::string currentError;
            if (!LoadSavedOnly(savedPath, current, currentError)) {
                error =
                    "기존 저장 비교 JSON이 손상되어 덮어쓰기를 거부했습니다: " +
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
                error = "기존 비교 JSON 백업 실패: " + fileError.message();
                return false;
            }
        }

        if (!SaveComparisonWorkspaceState(savedPath, state, error)) {
            return false;
        }

        ComparisonWorkspaceState reloaded;
        std::string reloadError;
        if (!LoadSavedOnly(savedPath, reloaded, reloadError)) {
            std::string restoreError;
            if (targetExists) RestoreBackup(backup, target, restoreError);
            error = "저장 직후 비교 JSON 재검증 실패: " + reloadError;
            if (!restoreError.empty()) error += " / " + restoreError;
            return false;
        }

        std::string actual;
        if (!SerializeComparisonWorkspaceState(reloaded, actual, reloadError) ||
            actual != expected)
        {
            std::string restoreError;
            if (targetExists) RestoreBackup(backup, target, restoreError);
            error = "저장 비교 JSON 왕복 결과가 요청 상태와 다릅니다.";
            if (!reloadError.empty()) error += " " + reloadError;
            if (!restoreError.empty()) error += " / " + restoreError;
            return false;
        }

        error.clear();
        return true;
    }
}
