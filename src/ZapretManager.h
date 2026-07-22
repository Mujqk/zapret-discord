#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

class ZapretManager {
public:
    using LogCallback = std::function<void(const std::string&)>;

    ZapretManager();
    ~ZapretManager();

    ZapretManager(const ZapretManager&) = delete;
    ZapretManager& operator=(const ZapretManager&) = delete;
    ZapretManager(ZapretManager&&) noexcept = default;
    ZapretManager& operator=(ZapretManager&&) noexcept = default;

    // Возвращает список доступных конфигурационных скриптов (.bat) из директории ядра.
    [[nodiscard]] std::vector<std::wstring> GetAvailableAlts() const;
    
    // Запускает выбранную альтернативную конфигурацию обхода.
    bool StartAlt(const std::wstring& altName);

    // Останавливает только процессы, запущенные данной системой (через Job Object).
    void StopAlt();

    // Проверяет работоспособность конкретной конфигурации.
    bool TestAlt(const std::wstring& altName, LogCallback logCallback = nullptr);

private:
    std::wstring m_corePath;
    HANDLE m_hJob{nullptr};
};


