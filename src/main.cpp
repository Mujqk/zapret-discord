#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <memory>

#include "ui.h"
#include "Theme.h"
#include "SelfInstaller.h"
#include "ZapretManager.h"
#include "AutoUpdater.h"
#include "Utils.h"

namespace fs = std::filesystem;

// Global model pointers to easily update state from background threads
static std::shared_ptr<slint::VectorModel<slint::SharedString>> g_AltsModel;
static std::shared_ptr<slint::VectorModel<slint::SharedString>> g_LogsModel;
static std::mutex g_LogMutex;

// Helper to push logs safely from any thread
void AddLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    slint::invoke_from_event_loop([msg]() {
        g_LogsModel->push_back(slint::SharedString(msg));
    });
}

// Map Theme to Slint UI properties
void ApplyTheme(AppWindow& window) {
    window.set_color_bg(slint::Color::from_rgb_uint8(Theme::COLOR_BG.r, Theme::COLOR_BG.g, Theme::COLOR_BG.b));
    window.set_color_card_bg(slint::Color::from_rgb_uint8(Theme::COLOR_CARD_BG.r, Theme::COLOR_CARD_BG.g, Theme::COLOR_CARD_BG.b));
    window.set_color_text(slint::Color::from_rgb_uint8(Theme::COLOR_TEXT.r, Theme::COLOR_TEXT.g, Theme::COLOR_TEXT.b));
    window.set_color_text_disabled(slint::Color::from_rgb_uint8(Theme::COLOR_TEXT_DISABLED.r, Theme::COLOR_TEXT_DISABLED.g, Theme::COLOR_TEXT_DISABLED.b));
    window.set_color_accent(slint::Color::from_rgb_uint8(Theme::COLOR_ACCENT.r, Theme::COLOR_ACCENT.g, Theme::COLOR_ACCENT.b));
    window.set_color_accent_hover(slint::Color::from_rgb_uint8(Theme::COLOR_ACCENT_HOVER.r, Theme::COLOR_ACCENT_HOVER.g, Theme::COLOR_ACCENT_HOVER.b));
    window.set_color_border(slint::Color::from_rgb_uint8(Theme::COLOR_BORDER.r, Theme::COLOR_BORDER.g, Theme::COLOR_BORDER.b));
    
    window.set_color_status_connected(slint::Color::from_rgb_uint8(Theme::COLOR_STATUS_CONNECTED.r, Theme::COLOR_STATUS_CONNECTED.g, Theme::COLOR_STATUS_CONNECTED.b));
    window.set_color_status_searching(slint::Color::from_rgb_uint8(Theme::COLOR_STATUS_SEARCHING.r, Theme::COLOR_STATUS_SEARCHING.g, Theme::COLOR_STATUS_SEARCHING.b));
    window.set_color_status_disconnected(slint::Color::from_rgb_uint8(Theme::COLOR_STATUS_DISCONNECTED.r, Theme::COLOR_STATUS_DISCONNECTED.g, Theme::COLOR_STATUS_DISCONNECTED.b));
    
    window.set_window_width(Theme::WINDOW_WIDTH);
    window.set_window_height(Theme::WINDOW_HEIGHT);
}

// Background thread logic for check & update
void RunUpdateThread(ZapretManager* manager, slint::ComponentWeakHandle<AppWindow> weak_window) {
    // Determine local version
    std::string version = "1.0.0";
    WCHAR pfPath[MAX_PATH];
    GetModuleFileNameW(NULL, pfPath, MAX_PATH);
    fs::path p(pfPath);
    std::ifstream vf(p.parent_path() / L"zapret_core" / L"version.txt");
    if (vf.is_open()) {
        std::getline(vf, version);
        vf.close();
        version.erase(version.find_last_not_of(" \n\r\t") + 1);
        version.erase(0, version.find_first_not_of(" \n\r\t"));
    }

    slint::invoke_from_event_loop([weak_window, version]() {
        if (auto window = weak_window.lock()) {
            (*window)->set_version_text(slint::SharedString(version));
        }
    });

    AutoUpdater::CheckAndUpdate(AddLog);

    // Re-verify version after possible update
    std::ifstream vfr(p.parent_path() / L"zapret_core" / L"version.txt");
    if (vfr.is_open()) {
        std::getline(vfr, version);
        vfr.close();
        version.erase(version.find_last_not_of(" \n\r\t") + 1);
        version.erase(0, version.find_first_not_of(" \n\r\t"));
    }

    auto w_alts = manager->GetAvailableAlts();
    
    slint::invoke_from_event_loop([weak_window, version, w_alts]() {
        if (auto window = weak_window.lock()) {
            (*window)->set_version_text(slint::SharedString(version));
        }
        g_AltsModel->clear();
        for (auto& w : w_alts) {
            g_AltsModel->push_back(slint::SharedString(WstringToUtf8(w)));
        }
    });
}

// Background thread logic for auto-searching working configs
void RunSearchThread(ZapretManager* manager, slint::ComponentWeakHandle<AppWindow> weak_window) {
    AddLog("Начат автоматический подбор конфигурации...");
    
    // Retrieve alts from model on UI thread context
    std::vector<std::string> alts;
    for (size_t i = 0; i < g_AltsModel->row_count(); ++i) {
        if (auto opt = g_AltsModel->row_data(i)) {
            alts.push_back(std::string(*opt));
        }
    }

    bool found = false;
    for (size_t i = 0; i < alts.size(); ++i) {
        if (manager->TestAlt(Utf8ToWstring(alts[i]), AddLog)) {
            found = true;
            manager->StartAlt(Utf8ToWstring(alts[i]));
            
            slint::invoke_from_event_loop([weak_window, i]() {
                if (auto window = weak_window.lock()) {
                    (*window)->set_current_alt_idx(static_cast<int>(i));
                    (*window)->set_is_connected(true);
                    (*window)->set_is_searching(false);
                    (*window)->set_status_text("АКТИВНО");
                }
            });
            
            AddLog("Найдена рабочая конфигурация: " + alts[i]);
            break;
        }
    }

    if (!found) {
        slint::invoke_from_event_loop([weak_window]() {
            if (auto window = weak_window.lock()) {
                (*window)->set_is_searching(false);
                (*window)->set_status_text("Не найдено");
            }
        });
        AddLog("Ошибка: Рабочая конфигурация не найдена.");
    }
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow) {
    if (SelfInstaller::CheckAndInstall()) {
        return 0;
    }

    // Initialize Slint window
    auto window = AppWindow::create();
    ApplyTheme(*window);

    auto manager = std::make_unique<ZapretManager>();
    
    // Setup Models
    g_AltsModel = std::make_shared<slint::VectorModel<slint::SharedString>>();
    g_LogsModel = std::make_shared<slint::VectorModel<slint::SharedString>>();
    
    // Initialize available configurations
    auto initial_alts = manager->GetAvailableAlts();
    for (const auto& alt : initial_alts) {
        g_AltsModel->push_back(slint::SharedString(WstringToUtf8(alt)));
    }
    
    window->set_available_alts(g_AltsModel);
    window->set_log_lines(g_LogsModel);

    // Thread instances
    std::thread search_thread;
    std::thread update_thread;

    AddLog("Добро пожаловать в ZAPRET.");

    // Setup Callbacks
    window->on_start_clicked([&]() {
        window->set_status_text("Запуск...");
        
        int current_idx = window->get_current_alt_idx();
        if (current_idx >= 0 && current_idx < static_cast<int>(g_AltsModel->row_count())) {
            auto alt_opt = g_AltsModel->row_data(current_idx);
            if (alt_opt) {
                std::string alt_name = std::string(*alt_opt);
                if (manager->StartAlt(Utf8ToWstring(alt_name))) {
                    window->set_is_connected(true);
                    window->set_status_text("АКТИВНО");
                    AddLog("Система успешно запущена.");
                } else {
                    window->set_status_text("Ошибка запуска");
                    AddLog("Ошибка при запуске конфигурации.");
                }
            }
        }
    });

    window->on_stop_clicked([&]() {
        manager->StopAlt();
        window->set_is_connected(false);
        window->set_status_text("Отключено");
        AddLog("Обход остановлен пользователем.");
    });

    window->on_auto_search_clicked([&]() {
        window->set_is_searching(true);
        window->set_status_text("Поиск...");
        if (search_thread.joinable()) {
            search_thread.join();
        }
        search_thread = std::thread(RunSearchThread, manager.get(), window.as_weak());
    });

    window->on_start_service_clicked([&]() {
        WCHAR pfPath[MAX_PATH];
        GetModuleFileNameW(NULL, pfPath, MAX_PATH);
        fs::path p(pfPath);
        std::wstring servicePath = (p.parent_path() / L"zapret_core" / L"service.bat").wstring();

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        std::wstring cmdLine = L"cmd.exe /c \"\"" + servicePath + L"\"\"";

        if (CreateProcessW(NULL, &cmdLine[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, (p.parent_path() / L"zapret_core").wstring().c_str(), &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            AddLog("Служба запущена.");
        } else {
            AddLog("Ошибка запуска службы.");
        }
    });

    window->on_next_alt_clicked([&]() {
        int count = static_cast<int>(g_AltsModel->row_count());
        if (count > 0) {
            int next_idx = (window->get_current_alt_idx() + 1) % count;
            window->set_current_alt_idx(next_idx);
            if (auto alt_opt = g_AltsModel->row_data(next_idx)) {
                AddLog("Переключено на: " + std::string(*alt_opt));
            }
        }
    });

    window->on_open_files_clicked([&]() {
        WCHAR pfPath[MAX_PATH];
        GetModuleFileNameW(NULL, pfPath, MAX_PATH);
        fs::path p(pfPath);
        ShellExecuteW(NULL, L"open", (p.parent_path() / L"zapret_core").wstring().c_str(), NULL, NULL, SW_SHOWNORMAL);
    });

    window->on_link_clicked([&](slint::SharedString url) {
        ShellExecuteW(NULL, L"open", Utf8ToWstring(std::string(url)).c_str(), NULL, NULL, SW_SHOWNORMAL);
    });

    window->on_close_clicked([&]() {
        slint::quit_event_loop();
    });

    window->on_minimize_clicked([&]() {
        HWND hwnd = FindWindowW(NULL, L"Zapret by Sayurin (Slint Edition)");
        if (hwnd) {
            ShowWindow(hwnd, SW_MINIMIZE);
        }
    });

    // Start background update check
    update_thread = std::thread(RunUpdateThread, manager.get(), window.as_weak());

    // Run the main GUI event loop
    window->run();

    // Clean up
    if (search_thread.joinable()) {
        search_thread.join();
    }
    if (update_thread.joinable()) {
        update_thread.join();
    }
    manager->StopAlt();

    return 0;
}
