#pragma once

#include <functional>
#include <string>

namespace AutoUpdater {
    using LogCallback = std::function<void(const std::string&)>;

    // Проверяет наличие новых релизов на GitHub и выполняет автоматическое обновление.
    bool CheckAndUpdate(LogCallback logCallback);
}



