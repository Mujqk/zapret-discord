#pragma once
#include <string>
#include <functional>

namespace AutoUpdater {
    // Check for updates and install in background. 
    // logCallback is called with status messages.
    // Returns true if an update was installed, false if up to date or failed.
    bool CheckAndUpdate(std::function<void(const std::string&)> logCallback);
}
