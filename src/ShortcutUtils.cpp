#include "ShortcutUtils.h"
#include <windows.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <shlobj.h>

bool ShortcutUtils::CreateDesktopShortcut(const std::wstring& targetPath, const std::wstring& shortcutName) {
    HRESULT hres;
    IShellLinkW* psl;

    // Initialize COM
    CoInitialize(NULL);

    // Get a pointer to the IShellLink interface.
    hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl);
    if (SUCCEEDED(hres)) {
        IPersistFile* ppf;

        // Set the path to the shortcut target
        psl->SetPath(targetPath.c_str());

        // Get the desktop folder path
        WCHAR desktopPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath))) {
            std::wstring shortcutPath = std::wstring(desktopPath) + L"\\" + shortcutName + L".lnk";

            // Query IShellLink for the IPersistFile interface
            hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
            if (SUCCEEDED(hres)) {
                // Save the link
                hres = ppf->Save(shortcutPath.c_str(), TRUE);
                ppf->Release();
            }
        }
        psl->Release();
    }
    
    return SUCCEEDED(hres);
}
