#include "AutoUpdater.h"
#include <windows.h>
#include <winhttp.h>
#include <filesystem>
#include <fstream>

#pragma comment(lib, "winhttp.lib")

namespace fs = std::filesystem;

std::string DownloadText(const std::wstring& domain, const std::wstring& path, const std::wstring& extraHeaders = L"") {
    std::string result = "";
    HINTERNET hSession = WinHttpOpen(L"Zapret Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession) {
        WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);
        HINTERNET hConnect = WinHttpConnect(hSession, domain.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (hRequest) {
                LPCWSTR headers = extraHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extraHeaders.c_str();
                DWORD headersLen = extraHeaders.empty() ? 0 : (DWORD)-1L;
                if (WinHttpSendRequest(hRequest, headers, headersLen, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
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

// Minimal JSON string-value extractor (avoids pulling in a full JSON library
// for the handful of fields we need from the GitHub Releases API response).
static std::string ExtractJsonString(const std::string& json, const std::string& key, size_t fromPos, size_t* outPos = nullptr) {
    std::string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle, fromPos);
    if (keyPos == std::string::npos) return "";

    size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos) return "";

    size_t quoteStart = json.find('"', colon);
    if (quoteStart == std::string::npos) return "";
    quoteStart++;

    std::string value;
    size_t i = quoteStart;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            i++;
            switch (json[i]) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                default: value += json[i]; break;
            }
        } else {
            value += json[i];
        }
        i++;
    }
    if (outPos) *outPos = i + 1;
    return value;
}

// Finds the browser_download_url of the first release asset whose name
// satisfies the given predicate.
template <typename Pred>
static std::string FindAssetUrl(const std::string& json, Pred matches) {
    size_t assetsPos = json.find("\"assets\"");
    if (assetsPos == std::string::npos) return "";

    size_t searchPos = assetsPos;
    while (true) {
        size_t namePos = 0;
        std::string name = ExtractJsonString(json, "name", searchPos, &namePos);
        if (name.empty()) break;

        if (matches(name)) {
            std::string url = ExtractJsonString(json, "browser_download_url", namePos);
            if (!url.empty()) return url;
        }
        searchPos = namePos;
    }
    return "";
}

// Finds the direct download URL of the first .zip asset attached to the
// release JSON. Falls back to the source zipball if no asset is found.
static std::string FindZipAssetUrl(const std::string& json) {
    std::string url = FindAssetUrl(json, [](const std::string& name) {
        return name.size() > 4 && _stricmp(name.c_str() + name.size() - 4, ".zip") == 0;
    });
    if (!url.empty()) return url;
    // Fallback: use the auto-generated "Source code (zip)" archive for the tag.
    return ExtractJsonString(json, "zipball_url", 0);
}

// Flowseal publishes a SHA256SUMS.txt asset alongside every release. If
// present, we download it and verify extracted files against it before
// copying anything into zapret_core, so a corrupted/tampered download never
// gets installed silently.
static std::string FindChecksumsAssetUrl(const std::string& json) {
    return FindAssetUrl(json, [](const std::string& name) {
        return _stricmp(name.c_str(), "SHA256SUMS.txt") == 0;
    });
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
    std::string releaseJson = DownloadText(L"api.github.com", L"/repos/Flowseal/zapret-discord-youtube/releases/latest",
        L"Accept: application/vnd.github+json\r\n");

    if (releaseJson.empty()) {
        logCallback(u8"Сетевая ошибка при проверке обновлений.");
        return false;
    }

    std::string latestTag = ExtractJsonString(releaseJson, "tag_name", 0);
    if (latestTag.empty()) {
        logCallback(u8"Не удалось разобрать ответ GitHub Releases API.");
        return false;
    }

    // Normalize "v1.9.9d" -> "1.9.9d" so it matches the local version.txt format.
    std::string latestVersion = latestTag;
    if (!latestVersion.empty() && (latestVersion[0] == 'v' || latestVersion[0] == 'V')) {
        latestVersion.erase(0, 1);
    }

    if (latestVersion == localVersion) {
        logCallback(u8"У вас установлена актуальная версия (" + localVersion + ").");
        return false;
    }

    std::string zipUrl = FindZipAssetUrl(releaseJson);
    if (zipUrl.empty()) {
        logCallback(u8"Не удалось найти архив релиза для скачивания.");
        return false;
    }
    std::string sumsUrl = FindChecksumsAssetUrl(releaseJson);
    if (sumsUrl.empty()) {
        logCallback(u8"Внимание: SHA256SUMS.txt не найден в релизе, проверка целостности будет пропущена.");
    }

    logCallback(u8"Найдена новая версия: " + latestVersion + u8". Скачивание в фоновом режиме...");

    std::wstring corePathW = corePath.wstring();
    std::wstring errPathW = errFile.wstring();
    std::wstring zipUrlW(zipUrl.begin(), zipUrl.end());
    std::wstring sumsUrlW(sumsUrl.begin(), sumsUrl.end());

    // Downloads the release archive and extracts it into zapret_core,
    // overwriting outdated files while preserving the bin/lists/utils
    // subfolder structure. NOTE: we deliberately use robocopy instead of
    // "Copy-Item -Path X\* -Destination Y -Recurse" — that wildcard+recurse
    // combo is known to flatten nested subfolders (their contents get
    // dumped straight into the destination root instead of keeping the
    // bin/, lists/, utils/ layout), which broke winws.exe/WinDivert paths.
    //
    // If SHA256SUMS.txt is present in the release, every extracted file it
    // lists is hashed and compared before robocopy runs — a mismatch aborts
    // the update instead of installing a corrupted/tampered download.
    std::wstring psCommand = L"powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command \""
        L"try { "
        L"  [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; "
        L"  $url = '" + zipUrlW + L"'; "
        L"  $sumsUrl = '" + sumsUrlW + L"'; "
        L"  $zipFile = 'update.zip'; "
        L"  $extPath = 'update_tmp'; "
        L"  Invoke-WebRequest -Uri $url -OutFile $zipFile -UserAgent 'Zapret Updater/1.0'; "
        L"  if (Test-Path $extPath) { Remove-Item $extPath -Recurse -Force; } "
        L"  Expand-Archive -Path $zipFile -DestinationPath $extPath -Force; "
        L"  $items = Get-ChildItem -Path $extPath; "
        L"  $sourceRoot = $extPath; "
        L"  if ($items.Count -eq 1 -and $items[0].PSIsContainer) { $sourceRoot = $items[0].FullName; } "
        L"  if ($sumsUrl -ne '') { "
        L"    Invoke-WebRequest -Uri $sumsUrl -OutFile 'SHA256SUMS.txt' -UserAgent 'Zapret Updater/1.0'; "
        L"    foreach ($line in Get-Content 'SHA256SUMS.txt') { "
        L"      if ($line -match '^([0-9a-fA-F]{64})\\s+\\*?(.+)$') { "
        L"        $expected = $matches[1].ToUpper(); "
        L"        $relPath = $matches[2].Trim(); "
        L"        $filePath = Join-Path $sourceRoot $relPath; "
        L"        if (Test-Path $filePath -PathType Leaf) { "
        L"          $actual = (Get-FileHash -Path $filePath -Algorithm SHA256).Hash; "
        L"          if ($actual -ne $expected) { throw \\\"Checksum verification failed: $relPath\\\"; } "
        L"        } "
        L"      } "
        L"    } "
        L"    Remove-Item 'SHA256SUMS.txt' -Force; "
        L"  } "
        L"  robocopy $sourceRoot '" + corePathW + L"' /E /R:2 /W:2 /NFL /NDL /NJH /NJS | Out-Null; "
        L"  if ($LASTEXITCODE -ge 8) { throw \\\"robocopy failed with exit code $LASTEXITCODE\\\"; } "
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
