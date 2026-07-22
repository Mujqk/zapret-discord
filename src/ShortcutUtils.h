#pragma once

#include <string>

namespace ShortcutUtils {
    // Создает ярлык (.lnk) на Рабочем столе с указанием рабочей директории.
    bool CreateDesktopShortcut(const std::wstring& targetPath, const std::wstring& shortcutName);
}



