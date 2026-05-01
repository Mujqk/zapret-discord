#include "HttpTester.h"
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

bool HttpTester::TestConnection(const std::wstring& domain, const std::wstring& path) {
    bool result = false;
    
    // Initialize WinHTTP
    HINTERNET hSession = WinHttpOpen(L"Zapret Tester/1.0", 
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                     WINHTTP_NO_PROXY_NAME, 
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    
    if (hSession) {
        // Set short timeouts (e.g. 2000 ms for resolve, connect, send, receive)
        WinHttpSetTimeouts(hSession, 2000, 2000, 2000, 2000);

        HINTERNET hConnect = WinHttpConnect(hSession, domain.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), 
                                                    NULL, WINHTTP_NO_REFERER, 
                                                    WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                                    WINHTTP_FLAG_SECURE);
            if (hRequest) {
                // Send the request
                if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, 
                                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                    // Wait for the response
                    if (WinHttpReceiveResponse(hRequest, NULL)) {
                        DWORD statusCode = 0;
                        DWORD dwSize = sizeof(statusCode);
                        
                        if (WinHttpQueryHeaders(hRequest, 
                                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
                                                WINHTTP_HEADER_NAME_BY_INDEX, 
                                                &statusCode, &dwSize, WINHTTP_NO_HEADER_INDEX)) {
                            // Any 2xx or 3xx code means the DPI let the request through
                            if (statusCode >= 200 && statusCode < 400) {
                                result = true;
                            }
                        }
                    }
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }
    
    return result;
}
