#pragma once
#include <functional>
#include <string>
#include <vector>
#include <windows.h>

class ZapretManager {
public:
    ZapretManager();
    ~ZapretManager();

    std::vector<std::wstring> GetAvailableAlts();
    
    // Starts the given batch file
    bool StartAlt(const std::wstring& altName);
    
    // Stops everything launched by StartAlt (and only that — see .cpp)
    void StopAlt();

    // Utility: test if an alt is working by starting it, waiting, testing http, and stopping.
    bool TestAlt(const std::wstring& altName, std::function<void(const std::string&)> logCallback = nullptr);

private:
    std::wstring corePath;
    // All processes started via StartAlt are assigned to this Job Object.
    // Windows automatically adds their child processes too, so StopAlt can
    // terminate exactly (and only) what we launched — instead of the old
    // approach of killing every cmd.exe/winws.exe system-wide by name,
    // which could kill unrelated processes belonging to the user.
    HANDLE hJob = NULL;
};
