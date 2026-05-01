#include "AutoUpdater.h"
#include <windows.h>
#include <winhttp.h>
#include <filesystem>
#include <fstream>

#pragma comment(lib, "winhttp.lib")

namespace fs = std::filesystem;

std::string DownloadText(const std::wstring& domain, const std::wstring& path) {
    std::string result = "";
    HINTERNET hSession = WinHttpOpen(L"Zapret Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession) {
        WinHttpSetTimeouts(hSession, 3000, 3000, 3000, 3000);
        HINTERNET hConnect = WinHttpConnect(hSession, domain.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (hRequest) {
                if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                    if (WinHttpReceiveResponse(hRequest, NULL)) {
                        DWORD dwSize = 0;
                        DWORD dwDownloaded = 0;
                        do {
                            dwSize = 0;
                            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                            if (dwSize == 0) break;
                            
                            char* pszOutBuffer = new char[dwSize + 1];
                            if (!pszOutBuffer) break;
                            
                            ZeroMemory(pszOutBuffer, dwSize + 1);
                            if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                                result.append(pszOutBuffer, dwDownloaded);
                            }
                            delete[] pszOutBuffer;
                        } while (dwSize > 0);
                    }
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }
    
    if (!result.empty()) {
        result.erase(result.find_last_not_of(" \n\r\t") + 1);
        result.erase(0, result.find_first_not_of(" \n\r\t"));
    }
    return result;
}

bool AutoUpdater::CheckAndUpdate(std::function<void(const std::string&)> logCallback) {
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    fs::path exePath(path);
    fs::path corePath = exePath.parent_path() / L"zapret_core";
    fs::path versionFile = corePath / L"version.txt";
    fs::path errFile = exePath.parent_path() / L"updater_err.txt";

    if (fs::exists(errFile)) {
        fs::remove(errFile);
    }

    // Only check the flag if the core actually exists and seems complete.
    // If core is missing or empty, we MUST download it.
    bool coreExists = fs::exists(corePath) && fs::exists(corePath / L"utils");
    
    if (coreExists) {
        fs::path updateFlag = corePath / L"utils" / L"check_updates.enabled";
        if (!fs::exists(updateFlag)) {
            logCallback(u8"Автообновление отключено пользователем.");
            return false;
        }
    } else {
        logCallback(u8"Ядро zapret_core не найдено или повреждено. Начинаю загрузку...");
    }

    std::string localVersion = "0.0.0";
    if (fs::exists(versionFile)) {
        std::ifstream vf(versionFile);
        if (vf.is_open()) {
            std::getline(vf, localVersion);
            vf.close();
            localVersion.erase(localVersion.find_last_not_of(" \n\r\t") + 1);
            localVersion.erase(0, localVersion.find_first_not_of(" \n\r\t"));
        }
    }

    logCallback(u8"Проверка наличия обновлений...");
    std::string latestVersion = DownloadText(L"raw.githubusercontent.com", L"/Flowseal/zapret-discord-youtube/main/.service/version.txt");
    
    if (latestVersion.empty() || latestVersion.find("404") != std::string::npos || latestVersion.length() > 20) {
        logCallback(u8"Сетевая ошибка при проверке обновлений.");
        return false;
    }

    if (latestVersion == localVersion) {
        logCallback(u8"У вас установлена актуальная версия (" + localVersion + ").");
        return false;
    }

    logCallback(u8"Найдена новая версия: " + latestVersion + u8". Скачивание в фоновом режиме...");
    
    std::wstring corePathW = corePath.wstring();
    std::wstring errPathW = errFile.wstring();
    
    std::wstring psCommand = L"powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command \""
        L"try { "
        L"  [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; "
        L"  $url = 'https://github.com/Flowseal/zapret-discord-youtube/archive/refs/heads/main.zip'; "
        L"  $zipFile = 'update.zip'; "
        L"  $extPath = 'update_tmp'; "
        L"  Invoke-WebRequest -Uri $url -OutFile $zipFile; "
        L"  Expand-Archive -Path $zipFile -DestinationPath $extPath -Force; "
        L"  $subFolder = Get-ChildItem -Path $extPath -Directory | Select-Object -First 1; "
        L"  if ($subFolder) { Copy-Item -Path \\\"$($subFolder.FullName)\\*\\\" -Destination '" + corePathW + L"' -Recurse -Force; } "
        L"  else { Copy-Item -Path \\\"$extPath\\*\\\" -Destination '" + corePathW + L"' -Recurse -Force; } "
        L"  Remove-Item $zipFile -Force; "
        L"  Remove-Item $extPath -Recurse -Force; "
        L"  exit 0; "
        L"} catch { "
        L"  $err = $_.Exception.Message; "
        L"  [IO.File]::WriteAllText('" + errPathW + L"', $err); "
        L"  exit 1; "
        L"}\"";

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (CreateProcessW(NULL, &psCommand[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, exePath.parent_path().wstring().c_str(), &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exitCode == 0) {
            std::ofstream vf(versionFile);
            if (vf.is_open()) {
                vf << latestVersion;
                vf.close();
            }
            logCallback(u8"Обновление завершено! Теперь используется версия " + latestVersion + ".");
            return true;
        } else {
            std::string exactError = u8"Неизвестная ошибка";
            if (fs::exists(errFile)) {
                std::ifstream ef(errFile);
                if (ef.is_open()) {
                    std::getline(ef, exactError);
                    ef.close();
                }
            }
            logCallback(u8"Произошла ошибка при распаковке файлов обновления:");
            logCallback(exactError);
            return false;
        }
    } else {
        logCallback(u8"Не удалось запустить системный процесс для обновления.");
        return false;
    }
}
