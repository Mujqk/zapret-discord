#include "ShortcutUtils.h"

#include <windows.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <filesystem>

namespace {

class ScopedComInit {
public:
    ScopedComInit() : m_hr(CoInitialize(nullptr)) {}
    ~ScopedComInit() {
        if (SUCCEEDED(m_hr)) {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool Succeeded() const noexcept {
        return SUCCEEDED(m_hr);
    }

private:
    HRESULT m_hr;
};

} // namespace

bool ShortcutUtils::CreateDesktopShortcut(const std::wstring& targetPath, const std::wstring& shortcutName) {
    const ScopedComInit comInit;
    if (!comInit.Succeeded()) {
        return false;
    }

    Microsoft::WRL::ComPtr<IShellLinkW> shellLink;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
    if (FAILED(hr)) {
        return false;
    }

    shellLink->SetPath(targetPath.c_str());

    const std::filesystem::path workingDir = std::filesystem::path(targetPath).parent_path();
    shellLink->SetWorkingDirectory(workingDir.c_str());

    WCHAR desktopPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktopPath))) {
        return false;
    }

    const std::wstring shortcutPath = std::wstring(desktopPath) + L"\\" + shortcutName + L".lnk";

    Microsoft::WRL::ComPtr<IPersistFile> persistFile;
    hr = shellLink.As(&persistFile);
    if (FAILED(hr)) {
        return false;
    }

    hr = persistFile->Save(shortcutPath.c_str(), TRUE);
    return SUCCEEDED(hr);
}

