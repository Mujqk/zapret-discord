#pragma once
#include <string>

namespace ShortcutUtils {
    // Creates a shortcut on the Desktop pointing to the specified target path.
    bool CreateDesktopShortcut(const std::wstring& targetPath, const std::wstring& shortcutName);
}
