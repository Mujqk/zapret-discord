#pragma once
#include <string>
#include <vector>

class ZapretManager {
public:
    ZapretManager();
    ~ZapretManager();

    std::vector<std::wstring> GetAvailableAlts();
    
    // Starts the given batch file
    bool StartAlt(const std::wstring& altName);
    
    // Stops winws.exe
    void StopAlt();

    // Utility: test if an alt is working by starting it, waiting, testing http, and stopping.
    bool TestAlt(const std::wstring& altName);

private:
    std::wstring corePath;
};
