#include "SelfInstaller.h"
#include "ShortcutUtils.h"

#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

bool SelfInstaller::CheckAndInstall() {
    WCHAR path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
        return false;
    }

    const fs::path currentExePath(path);
    const fs::path currentDir = currentExePath.parent_path();

    WCHAR docsPath[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docsPath))) {
        return false;
    }

    const fs::path targetDir = fs::path(docsPath) / L"Zapret";

    // If running from target directory, no install is needed.
    if (currentDir.wstring().find(targetDir.wstring()) == 0) {
        return false;
    }

    try {
        fs::create_directories(targetDir);

        const fs::path targetExePath = targetDir / currentExePath.filename();
        fs::copy_file(currentExePath, targetExePath, fs::copy_options::overwrite_existing);

        const fs::path coreSource = currentDir / L"zapret_core";
        const fs::path coreTarget = targetDir / L"zapret_core";
        if (fs::exists(coreSource)) {
            fs::copy(coreSource, coreTarget, fs::copy_options::overwrite_existing | fs::copy_options::recursive);
        }

        ShortcutUtils::CreateDesktopShortcut(targetExePath.wstring(), L"Zapret");

        SHELLEXECUTEINFOW sei{ sizeof(sei) };
        sei.fMask = SEE_MASK_NOASYNC;
        sei.lpVerb = L"open";
        sei.lpFile = targetExePath.c_str();
        sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

