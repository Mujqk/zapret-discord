#pragma once

#include <string>

namespace HttpTester {
    // Проверяет доступность узла через WinHTTP GET-запрос.
    [[nodiscard]] bool TestConnection(const std::wstring& domain, const std::wstring& path = L"/");
}


