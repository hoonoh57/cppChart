#include <windows.h>

#include <filesystem>
#include <system_error>

namespace
{
    struct M92WorkspaceBootstrapMigration final
    {
        M92WorkspaceBootstrapMigration()
        {
            wchar_t modulePath[32768]{};
            const DWORD length = GetModuleFileNameW(
                nullptr,
                modulePath,
                32768U);
            if (length == 0U || length >= 32768U) return;

            const std::filesystem::path directory =
                std::filesystem::path(modulePath).parent_path() / L"data";
            const std::filesystem::path primary =
                directory / L"chart_workspace.json";
            const std::filesystem::path bootstrap =
                directory / L"chart_workspace_bootstrap.json";

            std::error_code error;
            if (!std::filesystem::exists(primary, error) ||
                std::filesystem::exists(bootstrap, error))
            {
                return;
            }

            std::filesystem::create_directories(directory, error);
            error.clear();
            std::filesystem::copy_file(
                primary,
                bootstrap,
                std::filesystem::copy_options::skip_existing,
                error);
        }
    };

    M92WorkspaceBootstrapMigration g_m92WorkspaceBootstrapMigration;
}

#include "shell_main_m91.cpp"
