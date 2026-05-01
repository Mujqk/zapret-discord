#pragma once

namespace SelfInstaller {
    // Checks if the app is in Program Files, if not, installs it there and restarts.
    // Returns true if it initiated an install and the current process should exit.
    bool CheckAndInstall();
}
