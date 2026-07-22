#include "HttpTester.h"

#include <windows.h>
#include <winhttp.h>
#include <memory>

#pragma comment(lib, "winhttp.lib")

namespace {

struct WinHttpHandleDeleter {
    void operator()(HINTERNET handle) const noexcept {
        if (handle) {
            WinHttpCloseHandle(handle);
        }
    }
};

using UniqueHInternet = std::unique_ptr<void, WinHttpHandleDeleter>;

} // namespace

bool HttpTester::TestConnection(const std::wstring& domain, const std::wstring& path) {
    const UniqueHInternet hSession(WinHttpOpen(
        L"Zapret Tester/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    ));
    if (!hSession) {
        return false;
    }

    WinHttpSetTimeouts(hSession.get(), 5000, 5000, 5000, 5000);

    const UniqueHInternet hConnect(WinHttpConnect(
        hSession.get(),
        domain.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    ));
    if (!hConnect) {
        return false;
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
        return false;
    }

    if (!WinHttpSendRequest(hRequest.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        return false;
    }

    if (!WinHttpReceiveResponse(hRequest.get(), nullptr)) {
        return false;
    }

    DWORD statusCode = 0;
    DWORD dwSize = sizeof(statusCode);
    const bool queried = WinHttpQueryHeaders(
        hRequest.get(),
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &dwSize,
        WINHTTP_NO_HEADER_INDEX
    );

    return queried && (statusCode >= 200 && statusCode < 400);
}

