#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_internal.h"
#include <d3d11.h>
#include <tchar.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "AutoUpdater.h"
#include "SelfInstaller.h"
#include "Utils.h"
#include "ZapretManager.h"

// Глобальные объекты DirectX 11
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0;
static UINT                     g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Глобальное состояние приложения
static std::string g_ZapretVersion = "1.0.0";
static std::string g_StatusText = u8"Отключено";
static std::vector<std::string> g_Logs;
static std::mutex g_LogMutex;

void AddLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    g_Logs.push_back(msg);
}

// Предварительные объявления функций
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


struct AppFonts {
    ImFont* regular{nullptr};
    ImFont* header{nullptr};
    ImFont* subHeader{nullptr};
    ImFont* title{nullptr};
};

static void SetPremiumStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4 bg(0.07f, 0.08f, 0.09f, 1.00f);
    const ImVec4 cardBg(0.10f, 0.11f, 0.13f, 1.00f);
    const ImVec4 accent(0.31f, 0.45f, 0.62f, 1.00f);
    const ImVec4 accentHover(0.37f, 0.52f, 0.70f, 1.00f);
    const ImVec4 text(0.92f, 0.93f, 0.95f, 1.00f);
    const ImVec4 textDisabled(0.55f, 0.56f, 0.60f, 1.00f);

    colors[ImGuiCol_WindowBg] = bg;
    colors[ImGuiCol_ChildBg] = cardBg;
    colors[ImGuiCol_PopupBg] = cardBg;
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.21f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.17f, 0.18f, 0.21f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.21f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_Button] = accent;
    colors[ImGuiCol_ButtonHovered] = accentHover;
    colors[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.39f, 0.55f, 1.00f);
    colors[ImGuiCol_Header] = accent;
    colors[ImGuiCol_HeaderHovered] = accentHover;
    colors[ImGuiCol_HeaderActive] = accent;
    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = textDisabled;
    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.21f, 0.24f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.07f, 0.08f, 0.09f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.22f, 0.23f, 0.26f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.27f, 0.28f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = accent;

    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(16, 8);
    style.ItemSpacing = ImVec2(16, 16);
    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.ScrollbarSize = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
}

static AppFonts LoadApplicationFonts(ImGuiIO& io, const std::filesystem::path& exeDir) {
    AppFonts fonts;
    io.Fonts->Clear();

    const std::vector<std::filesystem::path> fontSearchPaths = {
        exeDir / "fonts" / "rubik-bold.ttf",
        exeDir.parent_path() / "fonts" / "rubik-bold.ttf",
        exeDir.parent_path().parent_path() / "fonts" / "rubik-bold.ttf",
        std::filesystem::current_path() / "fonts" / "rubik-bold.ttf"
    };

    std::string finalFontPath;
    for (const auto& p : fontSearchPaths) {
        if (std::filesystem::exists(p)) {
            finalFontPath = p.string();
            break;
        }
    }

    if (!finalFontPath.empty()) {
        fonts.regular = io.Fonts->AddFontFromFileTTF(finalFontPath.c_str(), 16.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
        {
            ImFontConfig config{};
            config.MergeMode = true;
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 16.0f, &config, io.Fonts->GetGlyphRangesCyrillic());
        }

        fonts.header = io.Fonts->AddFontFromFileTTF(finalFontPath.c_str(), 32.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
        {
            ImFontConfig hCfg{};
            hCfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 32.0f, &hCfg, io.Fonts->GetGlyphRangesCyrillic());
        }

        fonts.subHeader = io.Fonts->AddFontFromFileTTF(finalFontPath.c_str(), 17.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
        {
            ImFontConfig sCfg{};
            sCfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 17.0f, &sCfg, io.Fonts->GetGlyphRangesCyrillic());
        }

        fonts.title = io.Fonts->AddFontFromFileTTF(finalFontPath.c_str(), 42.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
        {
            ImFontConfig tCfg{};
            tCfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 42.0f, &tCfg, io.Fonts->GetGlyphRangesCyrillic());
        }
    } else {
        fonts.regular = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 16.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
        fonts.header = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 52.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
        fonts.subHeader = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 22.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
        fonts.title = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 42.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    }

    if (!fonts.regular) {
        fonts.regular = io.Fonts->AddFontDefault();
        fonts.header = io.Fonts->AddFontDefault();
        fonts.subHeader = io.Fonts->AddFontDefault();
        fonts.title = io.Fonts->AddFontDefault();
    }

    return fonts;
}

static void RenderTitleBar(HWND hwnd, bool& done, const AppFonts& fonts, float winW) {
    const float barH = 54.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImGui::SetCursorPos(ImVec2(25, 0));
    ImGui::BeginGroup();
    ImGui::SetCursorPosY(13);
    if (fonts.header) ImGui::PushFont(fonts.header);
    ImGui::TextColored(ImVec4(1, 1, 1, 1), "ZAPRET");
    if (fonts.header) ImGui::PopFont();

    ImGui::SameLine(0, 13);
    ImGui::SetCursorPosY(27);
    if (fonts.subHeader) ImGui::PushFont(fonts.subHeader);
    ImGui::TextColored(ImVec4(0.65f, 0.68f, 0.76f, 0.85f), "|  v%s Stable", g_ZapretVersion.c_str());
    if (fonts.subHeader) ImGui::PopFont();
    ImGui::EndGroup();

    const float btnSize = 29.0f;
    const float btnPadding = (barH - btnSize) / 2.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);

    // Кнопка закрытия окна
    ImGui::SetCursorPos(ImVec2(winW - btnSize - 17, btnPadding));
    const ImVec2 bPos = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
    if (ImGui::Button("##Close", ImVec2(btnSize, btnSize))) {
        done = true;
    }
    const bool hovC = ImGui::IsItemHovered();
    drawList->AddRect(bPos, ImVec2(bPos.x + btnSize, bPos.y + btnSize), ImColor(0.6f, 0.6f, 0.6f, hovC ? 0.8f : 0.4f), 8.0f, 0, 1.5f);
    const ImVec2 tSizeX = ImGui::CalcTextSize("X");
    drawList->AddText(ImVec2(bPos.x + (btnSize - tSizeX.x) / 2.0f, bPos.y + (btnSize - tSizeX.y) / 2.0f), ImColor(1.0f, 1.0f, 1.0f, hovC ? 1.0f : 0.7f), "X");
    ImGui::PopStyleColor(2);

    // Кнопка свертывания окна
    ImGui::SetCursorPos(ImVec2(winW - (btnSize * 2) - 30, btnPadding));
    const ImVec2 mPos = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
    if (ImGui::Button("##Min", ImVec2(btnSize, btnSize))) {
        ::ShowWindow(hwnd, SW_MINIMIZE);
    }
    const bool hovM = ImGui::IsItemHovered();
    drawList->AddRect(mPos, ImVec2(mPos.x + btnSize, mPos.y + btnSize), ImColor(0.6f, 0.6f, 0.6f, hovM ? 0.8f : 0.4f), 8.0f, 0, 1.5f);
    const ImVec2 tSizeM = ImGui::CalcTextSize("-");
    drawList->AddText(ImVec2(mPos.x + (btnSize - tSizeM.x) / 2.0f, mPos.y + (btnSize - tSizeM.y) / 2.0f), ImColor(1.0f, 1.0f, 1.0f, hovM ? 1.0f : 0.7f), "-");
    ImGui::PopStyleColor(2);

    ImGui::PopStyleVar();
}


static void RenderStatusCard(const AppFonts& fonts, bool isConnected, bool isSearching, float padding, float contentW) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImGui::SetCursorPos(ImVec2(padding, 80));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
    ImGui::BeginChild("StatusSection", ImVec2(contentW, 102), true);
    {
        ImGui::SetCursorPos(ImVec2(27, 22));
        if (fonts.subHeader) ImGui::PushFont(fonts.subHeader);
        ImGui::TextColored(ImVec4(0.65f, 0.68f, 0.76f, 1.0f), u8"ТЕКУЩИЙ СТАТУС ОБХОДА");
        if (fonts.subHeader) ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(27, 49));
        if (fonts.title) ImGui::PushFont(fonts.title);
        const ImVec4 stCol = isConnected ? ImVec4(0.4f, 0.95f, 0.5f, 1.0f) : (isSearching ? ImVec4(1.0f, 0.8f, 0.3f, 1.0f) : ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
        ImGui::TextColored(stCol, "%s", g_StatusText.c_str());
        if (fonts.title) ImGui::PopFont();

        ImVec2 ledPos = ImGui::GetCursorScreenPos();
        ledPos.x += contentW - 55;
        ledPos.y -= 37;
        drawList->AddCircleFilled(ledPos, 7.0f, ImGui::GetColorU32(stCol));
        drawList->AddCircle(ledPos, 7.0f, ImGui::GetColorU32(ImVec4(stCol.x, stCol.y, stCol.z, 0.35f)), 24, 1.5f);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void RenderConfigSection(ZapretManager& manager, const std::vector<std::string>& alts, int& currentAltIdx,
                                bool& isConnected, bool& isSearching, std::thread& searchThread,
                                const AppFonts& fonts, float padding, float contentW) {
    ImGui::SetCursorPos(ImVec2(padding, 212));
    ImGui::BeginChild("ConfigSection", ImVec2(contentW, 169), true);
    {
        ImGui::SetCursorPos(ImVec2(27, 22));
        if (fonts.subHeader) ImGui::PushFont(fonts.subHeader);
        ImGui::TextColored(ImVec4(0.65f, 0.68f, 0.76f, 1.0f), u8"НАСТРОЙКИ ОБХОДА");
        if (fonts.subHeader) ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(20, 55));

        ImGui::SetNextItemWidth(contentW - 40);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f);
        ImGui::BeginDisabled(isConnected || isSearching);
        if (ImGui::BeginCombo("##Strat", alts.empty() ? "" : alts[currentAltIdx].c_str(), ImGuiComboFlags_HeightRegular)) {
            for (int n = 0; n < static_cast<int>(alts.size()); n++) {
                const bool is_selected = (currentAltIdx == n);
                if (ImGui::Selectable(alts[n].c_str(), is_selected)) {
                    currentAltIdx = n;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        ImGui::PopStyleVar(2);

        ImGui::SetCursorPos(ImVec2(17, 102));
        const float btnW = (contentW - 40 - 16) / 2.0f;

        if (!isConnected && !isSearching) {
            if (ImGui::Button(u8"ЗАПУСТИТЬ", ImVec2(btnW, 47))) {
                g_StatusText = u8"Запуск...";
                if (manager.StartAlt(Utf8ToWstring(alts[currentAltIdx]))) {
                    isConnected = true;
                    g_StatusText = u8"АКТИВНО";
                    AddLog(u8"Система успешно запущена.");
                }
            }
        } else if (isConnected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.28f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.33f, 0.33f, 1.0f));
            if (ImGui::Button(u8"ОСТАНОВИТЬ", ImVec2(btnW, 47))) {
                manager.StopAlt();
                isConnected = false;
                g_StatusText = u8"Отключено";
                AddLog(u8"Обход остановлен пользователем.");
            }
            ImGui::PopStyleColor(2);
        } else {
            ImGui::BeginDisabled();
            ImGui::Button(u8"В ПРОЦЕССЕ...", ImVec2(btnW, 47));
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.24f, 1.0f));
        if (ImGui::Button(u8"АВТО-ПОИСК", ImVec2(btnW, 47)) && !isSearching) {
            isSearching = true;
            g_StatusText = u8"Поиск...";
            if (searchThread.joinable()) {
                searchThread.join();
            }
            searchThread = std::thread([&manager, alts, &currentAltIdx, &isConnected, &isSearching]() {
                AddLog(u8"Начат автоматический подбор конфигурации...");
                for (size_t i = 0; i < alts.size(); ++i) {
                    if (manager.TestAlt(Utf8ToWstring(alts[i]), AddLog)) {
                        currentAltIdx = static_cast<int>(i);
                        isConnected = true;
                        g_StatusText = u8"АКТИВНО";
                        manager.StartAlt(Utf8ToWstring(alts[i]));
                        AddLog(u8"Найдена рабочая конфигурация: " + alts[i]);
                        break;
                    }
                }
                isSearching = false;
                if (!isConnected) {
                    g_StatusText = u8"Не найдено";
                    AddLog(u8"Ошибка: Рабочая конфигурация не найдена.");
                }
            });
        }
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

static void RenderLogSection(const AppFonts& fonts, float padding, float contentW, float winH) {
    ImGui::SetCursorPos(ImVec2(padding, 410));
    if (fonts.subHeader) ImGui::PushFont(fonts.subHeader);
    ImGui::TextColored(ImVec4(0.65f, 0.68f, 0.76f, 1.0f), u8"ЖУРНАЛ СОБЫТИЙ");
    if (fonts.subHeader) ImGui::PopFont();

    ImGui::SetCursorPos(ImVec2(padding, 436));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.04f, 0.05f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
    ImGui::BeginChild("LogsArea", ImVec2(contentW, winH - 436.0f - 72.0f), true, ImGuiWindowFlags_AlwaysUseWindowPadding);
    {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        for (const auto& log : g_Logs) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.38f, 0.58f, 0.82f, 1.0f));
            ImGui::Text(">");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", log.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

static void RenderFooter(float padding, float winW, float winH) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImGui::SetCursorPos(ImVec2(padding, winH - 51.0f));
    ImGui::TextColored(ImVec4(0.65f, 0.68f, 0.75f, 1.0f), u8"Базируется на:");
    ImGui::SameLine();

    const ImVec4 linkCol(0.48f, 0.68f, 0.92f, 1.0f);

    // Ссылка на оригинальный репозиторий Flowseal
    ImGui::TextColored(linkCol, "Flowseal/zapret-discord-youtube");
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(0)) {
            ShellExecuteW(nullptr, L"open", L"https://github.com/Flowseal/zapret-discord-youtube", nullptr, nullptr, SW_SHOWNORMAL);
        }
        const ImVec2 m = ImGui::GetItemRectMin();
        const ImVec2 x = ImGui::GetItemRectMax();
        drawList->AddLine(ImVec2(m.x, x.y), x, ImColor(linkCol));
    }

    ImGui::SetCursorPos(ImVec2(padding, winH - 34.0f));
    ImGui::TextColored(ImVec4(0.65f, 0.68f, 0.75f, 1.0f), u8"Репозиторий:");
    ImGui::SameLine();

    // Ссылка на репозиторий Mujqk
    ImGui::TextColored(linkCol, "Mujqk/zapret-discord-youtube");
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(0)) {
            ShellExecuteW(nullptr, L"open", L"https://github.com/Mujqk/zapret-discord-youtube", nullptr, nullptr, SW_SHOWNORMAL);
        }
        const ImVec2 m = ImGui::GetItemRectMin();
        const ImVec2 x = ImGui::GetItemRectMax();
        drawList->AddLine(ImVec2(m.x, x.y), x, ImColor(linkCol));
    }


    const float btnH = 34.0f;
    const float serviceBtnW = 110.0f;
    const float filesBtnW = 145.0f;
    const float spacing = 10.0f;
    const float totalBtnsW = serviceBtnW + spacing + filesBtnW;

    ImGui::SetCursorPos(ImVec2(winW - padding - totalBtnsW, winH - 55.0f));

    // Кнопка "СЛУЖБА" (открывает service.bat)
    if (ImGui::Button(u8"СЛУЖБА", ImVec2(serviceBtnW, btnH))) {
        WCHAR pfPath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, pfPath, MAX_PATH);
        const std::filesystem::path p(pfPath);
        const std::filesystem::path coreDir = p.parent_path() / L"zapret_core";
        const std::wstring serviceBat = (coreDir / L"service.bat").wstring();
        ShellExecuteW(nullptr, L"open", serviceBat.c_str(), nullptr, coreDir.wstring().c_str(), SW_SHOWNORMAL);
    }

    ImGui::SameLine(0, spacing);

    // Кнопка "ОТКРЫТЬ ФАЙЛЫ" (открывает папку zapret_core)
    if (ImGui::Button(u8"ОТКРЫТЬ ФАЙЛЫ", ImVec2(filesBtnW, btnH))) {
        WCHAR pfPath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, pfPath, MAX_PATH);
        const std::filesystem::path p(pfPath);
        ShellExecuteW(nullptr, L"open", (p.parent_path() / L"zapret_core").wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}



int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow) {
    if (SelfInstaller::CheckAndInstall()) {
        return 0;
    }

    const HANDLE hSingleInstanceMutex = CreateMutexW(nullptr, TRUE, L"ZapretGUI_SingleInstance_9F3B2C1A");
    if (hSingleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Zapret уже запущен.", L"Zapret", MB_OK | MB_ICONINFORMATION);
        CloseHandle(hSingleInstanceMutex);
        return 0;
    }

    const WNDCLASSEXW wc = {
        sizeof(wc),
        CS_CLASSDC,
        WndProc,
        0L,
        0L,
        GetModuleHandle(nullptr),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        L"ZapretGUI",
        nullptr
    };
    ::RegisterClassExW(&wc);

    const int winW = 660;
    const int winH = 660;
    HWND hwnd = ::CreateWindowW(
        wc.lpszClassName,
        L"Zapret by Sayurin",
        WS_POPUP | WS_VISIBLE | WS_SYSMENU | WS_MINIMIZEBOX,
        100,
        100,
        winW,
        winH,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );

    const HICON hIcon = ::LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    if (hIcon) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
    }

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        if (hSingleInstanceMutex) {
            ReleaseMutex(hSingleInstanceMutex);
            CloseHandle(hSingleInstanceMutex);
        }
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    SetPremiumStyle();

    WCHAR exePathBuf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
    const std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();

    ImGuiIO& io = ImGui::GetIO();
    static std::string s_iniPath = (exeDir / "imgui.ini").string();
    io.IniFilename = s_iniPath.c_str();

    const AppFonts fonts = LoadApplicationFonts(io, exeDir);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ZapretManager manager;
    const auto w_alts = manager.GetAvailableAlts();
    std::vector<std::string> alts;
    alts.reserve(w_alts.size());
    for (const auto& w : w_alts) {
        alts.push_back(WstringToUtf8(w));
    }

    int currentAltIdx = 0;
    bool isConnected = false;
    bool isSearching = false;
    std::thread searchThread;
    std::thread updateThread;

    AddLog(u8"Добро пожаловать в ZAPRET.");

    updateThread = std::thread([&manager, &alts]() {
        WCHAR pfPath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, pfPath, MAX_PATH);
        const std::filesystem::path p(pfPath);

        std::ifstream vf(p.parent_path() / L"zapret_core" / L"version.txt");
        if (vf.is_open()) {
            std::getline(vf, g_ZapretVersion);
            vf.close();
            g_ZapretVersion.erase(g_ZapretVersion.find_last_not_of(" \n\r\t") + 1);
            g_ZapretVersion.erase(0, g_ZapretVersion.find_first_not_of(" \n\r\t"));
        }

        const bool updateInstalled = AutoUpdater::CheckAndUpdate(AddLog);
        (void)updateInstalled;


        std::ifstream vfr(p.parent_path() / L"zapret_core" / L"version.txt");
        if (vfr.is_open()) {
            std::getline(vfr, g_ZapretVersion);
            vfr.close();
            g_ZapretVersion.erase(g_ZapretVersion.find_last_not_of(" \n\r\t") + 1);
            g_ZapretVersion.erase(0, g_ZapretVersion.find_first_not_of(" \n\r\t"));
        }

        const auto new_w_alts = manager.GetAvailableAlts();
        alts.clear();
        for (const auto& w : new_w_alts) {
            alts.push_back(WstringToUtf8(w));
        }
    });

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }
        if (done) {
            break;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(winW), static_cast<float>(winH)));
        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

        RenderTitleBar(hwnd, done, fonts, static_cast<float>(winW));

        const float padding = 26.0f;
        const float contentW = static_cast<float>(winW) - (padding * 2.0f);

        RenderStatusCard(fonts, isConnected, isSearching, padding, contentW);
        RenderConfigSection(manager, alts, currentAltIdx, isConnected, isSearching, searchThread, fonts, padding, contentW);
        RenderLogSection(fonts, padding, contentW, static_cast<float>(winH));
        RenderFooter(padding, static_cast<float>(winW), static_cast<float>(winH));

        ImGui::End();

        ImGui::Render();
        const float cl[4] = { 0.07f, 0.08f, 0.09f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cl);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    if (searchThread.joinable()) {
        searchThread.join();
    }
    if (updateThread.joinable()) {
        updateThread.join();
    }

    manager.StopAlt();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    if (hSingleInstanceMutex) {
        ReleaseMutex(hSingleInstanceMutex);
        CloseHandle(hSingleInstanceMutex);
    }
    return 0;
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL fla[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fla, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext) != S_OK) {
        return false;
    }
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pb = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pb));
    if (pb) {
        g_pd3dDevice->CreateRenderTargetView(pb, nullptr, &g_mainRenderTargetView);
        pb->Release();
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return true;
    }
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            g_ResizeWidth = static_cast<UINT>(LOWORD(lParam));
            g_ResizeHeight = static_cast<UINT>(HIWORD(lParam));
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) {
            return 0;
        }
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    case WM_NCHITTEST: {
        POINT pt = { static_cast<int>(static_cast<short>(LOWORD(lParam))), static_cast<int>(static_cast<short>(HIWORD(lParam))) };
        ::ScreenToClient(hWnd, &pt);
        RECT rc;
        ::GetClientRect(hWnd, &rc);
        if (pt.y >= 0 && pt.y < 54 && pt.x >= 0 && pt.x < (rc.right - 110)) {
            return HTCAPTION;
        }
        break;
    }
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}


