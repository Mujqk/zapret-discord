#include "SelfInstaller.h"
#include "ShortcutUtils.h"
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

bool SelfInstaller::CheckAndInstall() {
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    fs::path currentExePath(path);
    fs::path currentDir = currentExePath.parent_path();

    WCHAR pfPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, 0, pfPath))) {
        return false;
    }
    fs::path targetDir = fs::path(pfPath) / L"Zapret";

    // If we are already running from the target directory, do nothing.
    if (currentDir.wstring().find(targetDir.wstring()) == 0) {
        return false;
    }

    // We are not in Program Files. Perform install.
    try {
        fs::create_directories(targetDir);

        // Copy Zapret.exe
        fs::path targetExePath = targetDir / currentExePath.filename();
        fs::copy_file(currentExePath, targetExePath, fs::copy_options::overwrite_existing);

        // Copy zapret_core folder
        fs::path coreSource = currentDir / L"zapret_core";
        fs::path coreTarget = targetDir / L"zapret_core";
        if (fs::exists(coreSource)) {
            fs::copy(coreSource, coreTarget, fs::copy_options::overwrite_existing | fs::copy_options::recursive);
        }

        // Create shortcut
        ShortcutUtils::CreateDesktopShortcut(targetExePath.wstring(), L"Zapret");

        // Relaunch from the new location
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOASYNC;
        sei.lpVerb = L"open";
        sei.lpFile = targetExePath.c_str();
        sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);

        return true; // We installed and relaunched, so this instance should exit.

    } catch (const std::exception& e) {
        // Fallback
        return false;
    }
}
