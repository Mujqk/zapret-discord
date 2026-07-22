#include "ZapretManager.h"
#include "HttpTester.h"
#include "Utils.h"

#include <windows.h>
#include <chrono>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

ZapretManager::ZapretManager() {
    WCHAR path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    const fs::path exePath(path);
    m_corePath = (exePath.parent_path() / L"zapret_core").wstring();

    m_hJob = CreateJobObjectW(nullptr, nullptr);
    if (m_hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(m_hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }
}

ZapretManager::~ZapretManager() {
    if (m_hJob) {
        CloseHandle(m_hJob);
    }
}

std::vector<std::wstring> ZapretManager::GetAvailableAlts() const {
    std::vector<std::wstring> alts;
    if (!fs::exists(m_corePath)) {
        return alts;
    }

    for (const auto& entry : fs::directory_iterator(m_corePath)) {
        if (entry.is_regular_file() && entry.path().extension() == L".bat") {
            const std::wstring name = entry.path().filename().wstring();
            if (name != L"service.bat") {
                alts.push_back(name);
            }
        }
    }
    return alts;
}

bool ZapretManager::StartAlt(const std::wstring& altName) {
    StopAlt();

    const std::wstring batPath = m_corePath + L"\\" + altName;
    std::wstring cmdLine = L"cmd.exe /c \"\"" + batPath + L"\"\"";

    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, m_corePath.c_str(), &si, &pi)) {
        return false;
    }

    if (m_hJob) {
        AssignProcessToJobObject(m_hJob, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    return true;
}

void ZapretManager::StopAlt() {
    if (m_hJob) {
        TerminateJobObject(m_hJob, 9);
    }
}

bool ZapretManager::TestAlt(const std::wstring& altName, LogCallback logCallback) {
    if (logCallback) {
        logCallback(u8"Проверка: " + WstringToUtf8(altName));
    }
    
    if (!StartAlt(altName)) {
        return false;
    }

    const bool discordWorks = HttpTester::TestConnection(L"discord.com");
    const bool youtubeWorks = HttpTester::TestConnection(L"www.youtube.com");

    if (logCallback) {
        std::string res = "  - Discord: " + std::string(discordWorks ? u8"OK" : u8"FAIL");
        res += " | YouTube: " + std::string(youtubeWorks ? u8"OK" : u8"FAIL");
        logCallback(res);
    }

    StopAlt();

    return discordWorks && youtubeWorks;
}


