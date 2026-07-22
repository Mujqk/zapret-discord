#include "ZapretManager.h"
#include "HttpTester.h"
#include "Utils.h"
#include <windows.h>
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

ZapretManager::ZapretManager() {
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    fs::path exePath(path);
    corePath = (exePath.parent_path() / L"zapret_core").wstring();

    // Every process StartAlt() launches (and any children it spawns, e.g.
    // winws.exe started from inside the .bat) gets assigned to this job.
    // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE means closing the job handle also
    // cleans everything up if the app crashes/exits unexpectedly.
    hJob = CreateJobObjectW(NULL, NULL);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }
}

ZapretManager::~ZapretManager() {
    // StopAlt(); // User controls when it stops
    if (hJob) {
        CloseHandle(hJob); // KILL_ON_JOB_CLOSE takes care of cleanup
    }
}

std::vector<std::wstring> ZapretManager::GetAvailableAlts() {
    std::vector<std::wstring> alts;
    if (fs::exists(corePath)) {
        for (const auto& entry : fs::directory_iterator(corePath)) {
            if (entry.is_regular_file() && entry.path().extension() == L".bat") {
                std::wstring name = entry.path().filename().wstring();
                // Exclude service.bat
                if (name != L"service.bat") {
                    alts.push_back(name);
                }
            }
        }
    }
    return alts;
}

bool ZapretManager::StartAlt(const std::wstring& altName) {
    StopAlt(); // Ensure any previous instance is stopped

    std::wstring batPath = corePath + L"\\" + altName;
    
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    
    // Hide window
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::wstring cmdLine = L"cmd.exe /c \"\"" + batPath + L"\"\"";
    
    if (CreateProcessW(NULL, &cmdLine[0], NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, corePath.c_str(), &si, &pi)) {
        // Assign to the job BEFORE resuming, so we never miss a fast-exiting
        // cmd.exe that spawns winws.exe and returns immediately.
        if (hJob) {
            AssignProcessToJobObject(hJob, pi.hProcess);
        }
        ResumeThread(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        // Give it a moment to start the service/process
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        return true;
    }
    return false;
}

void ZapretManager::StopAlt() {
    // Terminate only the processes we ourselves launched (and their
    // children) via the job object. This replaces the previous
    // implementation, which killed every winws.exe *and every cmd.exe on
    // the whole system* by process name — including ones that had nothing
    // to do with this app.
    if (hJob) {
        TerminateJobObject(hJob, 9);
    }
}

bool ZapretManager::TestAlt(const std::wstring& altName, std::function<void(const std::string&)> logCallback) {
    if (logCallback) logCallback(u8"Проверка: " + WstringToUtf8(altName));
    if (!StartAlt(altName)) return false;
    
    // Test discord, youtube and tiktok
    bool discordWorks = HttpTester::TestConnection(L"discord.com");
    bool youtubeWorks = HttpTester::TestConnection(L"www.youtube.com");
    bool tiktokWorks = HttpTester::TestConnection(L"tiktok.com");
    
    if (logCallback) {
        std::string res = "  - Discord: " + std::string(discordWorks ? u8"OK" : u8"FAIL");
        res += " | YouTube: " + std::string(youtubeWorks ? u8"OK" : u8"FAIL");
        res += " | TikTok: " + std::string(tiktokWorks ? u8"OK" : u8"FAIL");
        logCallback(res);
    }

    StopAlt();
    
    // A strategy is considered WORKING if YouTube and Discord work.
    // TikTok is bonus because it's often blocked by IP (region lock) 
    // which DPI bypass cannot fix.
    return discordWorks && youtubeWorks;
}
