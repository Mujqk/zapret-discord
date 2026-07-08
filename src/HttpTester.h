#pragma once
#include <string>

namespace HttpTester {
    // Tests connection to a URL using WinHTTP. Returns true if successful.
    bool TestConnection(const std::wstring& domain, const std::wstring& path = L"/");
}
