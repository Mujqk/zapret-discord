#include "AutoUpdater.h"

#include <windows.h>
#include <winhttp.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace fs = std::filesystem;

namespace {

struct WinHttpHandleDeleter {
    void operator()(HINTERNET handle) const noexcept {
        if (handle) {
            WinHttpCloseHandle(handle);
        }
    }
};

using UniqueHInternet = std::unique_ptr<void, WinHttpHandleDeleter>;

struct HandleDeleter {
    void operator()(HANDLE h) const noexcept {
        if (h && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
};

using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

std::string DownloadText(const std::wstring& domain, const std::wstring& path, const std::wstring& extraHeaders = L"") {
    const UniqueHInternet hSession(WinHttpOpen(
        L"Zapret Updater/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    ));
    if (!hSession) {
        return {};
    }

    WinHttpSetTimeouts(hSession.get(), 5000, 5000, 5000, 5000);

    const UniqueHInternet hConnect(WinHttpConnect(
        hSession.get(),
        domain.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    ));
    if (!hConnect) {
        return {};
    }

    const UniqueHInternet hRequest(WinHttpOpenRequest(
        hConnect.get(),
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    ));
    if (!hRequest) {
        return {};
    }

    LPCWSTR headers = extraHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extraHeaders.c_str();
    DWORD headersLen = extraHeaders.empty() ? 0 : static_cast<DWORD>(-1L);

    if (!WinHttpSendRequest(hRequest.get(), headers, headersLen, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        return {};
    }

    if (!WinHttpReceiveResponse(hRequest.get(), nullptr)) {
        return {};
    }

    std::string result;
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;

    while (WinHttpQueryDataAvailable(hRequest.get(), &dwSize) && dwSize > 0) {
        std::vector<char> buffer(dwSize);
        if (WinHttpReadData(hRequest.get(), buffer.data(), dwSize, &dwDownloaded)) {
            result.append(buffer.data(), dwDownloaded);
        }
    }

    if (!result.empty()) {
        const size_t first = result.find_first_not_of(" \n\r\t");
        if (first == std::string::npos) {
            return {};
        }
        const size_t last = result.find_last_not_of(" \n\r\t");
        result = result.substr(first, (last - first + 1));
    }

    return result;
}

std::string ExtractJsonString(const std::string& json, const std::string& key, size_t fromPos, size_t* outPos = nullptr) {
    const std::string needle = "\"" + key + "\"";
    const size_t keyPos = json.find(needle, fromPos);
    if (keyPos == std::string::npos) {
        return {};
    }

    const size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos) {
        return {};
    }

    size_t quoteStart = json.find('"', colon);
    if (quoteStart == std::string::npos) {
        return {};
    }
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

    if (outPos) {
        *outPos = i + 1;
    }
    return value;
}

template <typename Pred>
std::string FindAssetUrl(const std::string& json, Pred matches) {
    const size_t assetsPos = json.find("\"assets\"");
    if (assetsPos == std::string::npos) {
        return {};
    }

    size_t searchPos = assetsPos;
    while (true) {
        size_t namePos = 0;
        const std::string name = ExtractJsonString(json, "name", searchPos, &namePos);
        if (name.empty()) {
            break;
        }

        if (matches(name)) {
            const std::string url = ExtractJsonString(json, "browser_download_url", namePos);
            if (!url.empty()) {
                return url;
            }
        }
        searchPos = namePos;
    }
    return {};
}

std::string FindZipAssetUrl(const std::string& json) {
    std::string url = FindAssetUrl(json, [](const std::string& name) {
        return name.size() > 4 && _stricmp(name.c_str() + name.size() - 4, ".zip") == 0;
    });
    if (!url.empty()) {
        return url;
    }
    return ExtractJsonString(json, "zipball_url", 0);
}

std::string FindChecksumsAssetUrl(const std::string& json) {
    return FindAssetUrl(json, [](const std::string& name) {
        return _stricmp(name.c_str(), "SHA256SUMS.txt") == 0;
    });
}

std::string ReadVersionFile(const fs::path& versionFile) {
    if (!fs::exists(versionFile)) {
        return "0.0.0";
    }
    std::ifstream vf(versionFile);
    if (!vf.is_open()) {
        return "0.0.0";
    }
    std::string version;
    std::getline(vf, version);
    if (!version.empty()) {
        const size_t first = version.find_first_not_of(" \n\r\t");
        if (first != std::string::npos) {
            const size_t last = version.find_last_not_of(" \n\r\t");
            version = version.substr(first, (last - first + 1));
        }
    }
    return version.empty() ? "0.0.0" : version;
}

} // namespace

bool AutoUpdater::CheckAndUpdate(LogCallback logCallback) {
    WCHAR path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    const fs::path exePath(path);
    const fs::path exeDir = exePath.parent_path();
    const fs::path corePath = exeDir / L"zapret_core";
    const fs::path versionFile = corePath / L"version.txt";
    const fs::path errFile = exeDir / L"updater_err.txt";

    if (fs::exists(errFile)) {
        fs::remove(errFile);
    }

    const bool coreExists = fs::exists(corePath) && fs::exists(corePath / L"utils");
    if (coreExists) {
        const fs::path updateFlag = corePath / L"utils" / L"check_updates.enabled";
        if (!fs::exists(updateFlag)) {
            logCallback(u8"Автообновление отключено пользователем.");
            return false;
        }
    } else {
        logCallback(u8"Ядро zapret_core не найдено или повреждено. Начинаю загрузку...");
    }

    const std::string localVersion = ReadVersionFile(versionFile);

    logCallback(u8"Проверка наличия обновлений...");
    const std::string releaseJson = DownloadText(
        L"api.github.com",
        L"/repos/Flowseal/zapret-discord-youtube/releases/latest",
        L"Accept: application/vnd.github+json\r\n"
    );

    if (releaseJson.empty()) {
        logCallback(u8"Сетевая ошибка при проверке обновлений.");
        return false;
    }

    const std::string latestTag = ExtractJsonString(releaseJson, "tag_name", 0);
    if (latestTag.empty()) {
        logCallback(u8"Не удалось разобрать ответ GitHub Releases API.");
        return false;
    }

    std::string latestVersion = latestTag;
    if (!latestVersion.empty() && (latestVersion[0] == 'v' || latestVersion[0] == 'V')) {
        latestVersion.erase(0, 1);
    }

    if (latestVersion == localVersion) {
        logCallback(u8"У вас установлена актуальная версия (" + localVersion + ").");
        return false;
    }

    const std::string zipUrl = FindZipAssetUrl(releaseJson);
    if (zipUrl.empty()) {
        logCallback(u8"Не удалось найти архив релиза для скачивания.");
        return false;
    }

    const std::string sumsUrl = FindChecksumsAssetUrl(releaseJson);
    if (sumsUrl.empty()) {
        logCallback(u8"Внимание: SHA256SUMS.txt не найден в релизе, проверка целостности будет пропущена.");
    }

    logCallback(u8"Найдена новая версия: " + latestVersion + u8". Скачивание в фоновом режиме...");

    const std::wstring corePathW = corePath.wstring();
    const std::wstring errPathW = errFile.wstring();
    const std::wstring zipUrlW(zipUrl.begin(), zipUrl.end());
    const std::wstring sumsUrlW(sumsUrl.begin(), sumsUrl.end());

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

    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, psCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, exeDir.wstring().c_str(), &si, &pi)) {
        logCallback(u8"Не удалось запустить системный процесс для обновления.");
        return false;
    }

    const UniqueHandle hProcess(pi.hProcess);
    const UniqueHandle hThread(pi.hThread);

    WaitForSingleObject(hProcess.get(), INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(hProcess.get(), &exitCode);

    if (exitCode == 0) {
        std::ofstream vf(versionFile);
        if (vf.is_open()) {
            vf << latestVersion;
        }
        logCallback(u8"Обновление завершено! Теперь используется версия " + latestVersion + ".");
        return true;
    }

    std::string exactError = u8"Неизвестная ошибка";
    if (fs::exists(errFile)) {
        std::ifstream ef(errFile);
        if (ef.is_open()) {
            std::getline(ef, exactError);
        }
    }
    logCallback(u8"Произошла ошибка при распаковке файлов обновления:");
    logCallback(exactError);
    return false;
}

