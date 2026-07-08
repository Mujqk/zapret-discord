#include "ZapretManager.h"
#include "HttpTester.h"
#include "Utils.h"
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

ZapretManager::ZapretManager() {
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    fs::path exePath(path);
    corePath = (exePath.parent_path() / L"zapret_core").wstring();
}

ZapretManager::~ZapretManager() {
    // StopAlt(); // User controls when it stops
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
    
    if (CreateProcessW(NULL, &cmdLine[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, corePath.c_str(), &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        // Give it a moment to start the service/process
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        return true;
    }
    return false;
}

void ZapretManager::StopAlt() {
    HANDLE hSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, NULL);
    PROCESSENTRY32W pEntry;
    pEntry.dwSize = sizeof(pEntry);

    if (Process32FirstW(hSnapShot, &pEntry)) {
        do {
            if (wcscmp(pEntry.szExeFile, L"winws.exe") == 0 || wcscmp(pEntry.szExeFile, L"cmd.exe") == 0) { // Also kill batch scripts if running
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, 0, pEntry.th32ProcessID);
                if (hProcess != NULL) {
                    TerminateProcess(hProcess, 9);
                    CloseHandle(hProcess);
                }
            }
        } while (Process32NextW(hSnapShot, &pEntry));
    }
    CloseHandle(hSnapShot);
}

bool ZapretManager::TestAlt(const std::wstring& altName, std::function<void(const std::string&)> logCallback) {
    if (logCallback) logCallback("Проверка: " + WstringToUtf8(altName));
    if (!StartAlt(altName)) return false;
    
    // Test discord, youtube and tiktok
    bool discordWorks = HttpTester::TestConnection(L"discord.com");
    bool youtubeWorks = HttpTester::TestConnection(L"www.youtube.com");
    bool tiktokWorks = HttpTester::TestConnection(L"tiktok.com");
    
    if (logCallback) {
        std::string res = "  - Discord: " + std::string(discordWorks ? "OK" : "FAIL");
        res += " | YouTube: " + std::string(youtubeWorks ? "OK" : "FAIL");
        res += " | TikTok: " + std::string(tiktokWorks ? "OK" : "FAIL");
        logCallback(res);
    }

    StopAlt();
    
    // A strategy is considered WORKING if YouTube and Discord work.
    // TikTok is bonus because it's often blocked by IP (region lock) 
    // which DPI bypass cannot fix.
    return discordWorks && youtubeWorks;
}
