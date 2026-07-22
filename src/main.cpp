#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_internal.h"
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <shlobj.h>

#include "SelfInstaller.h"
#include "ZapretManager.h"
#include "AutoUpdater.h"
#include <filesystem>
#include <fstream>
#include "Utils.h"


// Data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

std::string g_ZapretVersion = "1.0.0";
std::string g_StatusText = u8"Отключено";
std::vector<std::string> g_Logs;
std::mutex g_LogMutex;

void AddLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    g_Logs.push_back(msg);
}




void SetPremiumStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Restrained, corporate slate palette — flat, low-contrast surfaces
    // with a single desaturated blue accent instead of a bright purple.
    const ImVec4 bg = ImVec4(0.07f, 0.08f, 0.09f, 1.00f);
    const ImVec4 cardBg = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
    const ImVec4 accent = ImVec4(0.31f, 0.45f, 0.62f, 1.00f);
    const ImVec4 accentHover = ImVec4(0.37f, 0.52f, 0.70f, 1.00f);
    const ImVec4 text = ImVec4(0.90f, 0.91f, 0.93f, 1.00f);
    const ImVec4 textDisabled = ImVec4(0.45f, 0.46f, 0.50f, 1.00f);

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
    style.FramePadding = ImVec2(20, 14);
    style.ItemSpacing = ImVec2(16, 16);
    // Sharper, more restrained corners — this reads as a serious utility
    // rather than a consumer/gamer app.
    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.ScrollbarSize = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
}



void DrawGlowButton(const char* label, const ImVec2& size, bool active, bool disabled = false) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = style.Colors[ImGuiCol_Button];
    if (disabled) accent = ImVec4(0.22f, 0.22f, 0.25f, 1.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    // Static single-line outline instead of an animated pulsing glow —
    // steadier and more in line with a professional utility.
    if (!disabled && active) {
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
            ImColor(accent.x, accent.y, accent.z, 0.35f),
            style.FrameRounding, 0, 1.5f);
    }

    if (disabled) ImGui::BeginDisabled();
    if (ImGui::Button(label, size)) { /* handle click outside */ }
    if (disabled) ImGui::EndDisabled();
}


int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow) {
    if (SelfInstaller::CheckAndInstall()) return 0;

    // Prevent two GUI instances from running (and fighting over) the same
    // winws.exe/job object at once.
    HANDLE hSingleInstanceMutex = CreateMutexW(NULL, TRUE, L"ZapretGUI_SingleInstance_9F3B2C1A");
    if (hSingleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"Zapret уже запущен.", L"Zapret", MB_OK | MB_ICONINFORMATION);
        CloseHandle(hSingleInstanceMutex);
        return 0;
    }

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ZapretGUI", nullptr };
    ::RegisterClassExW(&wc);
    
    // Compact proportions
    int winW = 660;
    int winH = 660; 
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Zapret by Sayurin", WS_POPUP | WS_VISIBLE | WS_SYSMENU | WS_MINIMIZEBOX, 100, 100, winW, winH, nullptr, nullptr, wc.hInstance, nullptr);

    // Load custom icon from resources
    HICON hIcon = ::LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1)); // 1 is IDI_ICON1
    if (hIcon) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }



    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D(); ::UnregisterClassW(wc.lpszClassName, wc.hInstance); return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT); ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    SetPremiumStyle();

    // Get executable directory
    WCHAR exePathBuf[MAX_PATH];
    GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // ImGui writes its .ini to the CURRENT WORKING DIRECTORY by default, not
    // next to the exe. When launched via a desktop shortcut with no explicit
    // "Start in" folder, Windows uses the shortcut's own folder (Desktop) as
    // the working directory, so imgui.ini ends up on the user's desktop.
    // Pinning an absolute path here fixes it regardless of how/where the
    // app is launched from. io.IniFilename is a raw pointer ImGui keeps
    // around, so the backing string must outlive the ImGui context.
    static std::string s_iniPath = (exeDir / "imgui.ini").string();
    io.IniFilename = s_iniPath.c_str();

    // Load fonts
    ImFont* font = nullptr;
    ImFont* headerFont = nullptr;
    ImFont* subHeaderFont = nullptr;
    ImFont* titleFont = nullptr;

    // Potential paths for the fonts folder
    std::vector<std::filesystem::path> fontSearchPaths = {
        exeDir / "fonts" / "rubik-bold.ttf",
        exeDir.parent_path() / "fonts" / "rubik-bold.ttf",
        exeDir.parent_path().parent_path() / "fonts" / "rubik-bold.ttf",
        std::filesystem::current_path() / "fonts" / "rubik-bold.ttf"
    };

    std::string finalFontPath = "";
    for (const auto& p : fontSearchPaths) {
        if (std::filesystem::exists(p)) {
            finalFontPath = p.string();
            break;
        }
    }

    if (!finalFontPath.empty()) {
        // Load fonts with proper config initialization
        font = io.Fonts->AddFontFromFileTTF(finalFontPath.c_str(), 14.0f, NULL, io.Fonts->GetGlyphRangesDefault());
        {
            ImFontConfig config{};
            config.MergeMode = true;
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 14.0f, &config, io.Fonts->GetGlyphRangesCyrillic());
        }

        headerFont = io.Fonts->AddFontFromFileTTF(finalFontPath.c_str(), 31.0f, NULL, io.Fonts->GetGlyphRangesDefault());
        {
            ImFontConfig hCfg{};
            hCfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 31.0f, &hCfg, io.Fonts->GetGlyphRangesCyrillic());
        }

        subHeaderFont = io.Fonts->AddFontFromFileTTF(finalFontPath.c_str(), 15.0f, NULL, io.Fonts->GetGlyphRangesDefault());
        {
            ImFontConfig sCfg{};
            sCfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 15.0f, &sCfg, io.Fonts->GetGlyphRangesCyrillic());
        }


        titleFont = io.Fonts->AddFontFromFileTTF(finalFontPath.c_str(), 41.0f, NULL, io.Fonts->GetGlyphRangesDefault());
        {
            ImFontConfig tCfg{};
            tCfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 41.0f, &tCfg, io.Fonts->GetGlyphRangesCyrillic());
        }
    } else {
        // Fallback: Just load Segoe UI Bold if Rubik is not found
        font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 14.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());
        headerFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 51.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());
        subHeaderFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 20.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());
        titleFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 41.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());
    }
    
    // Final safety fallback
    if (!font) {
        font = io.Fonts->AddFontDefault();
        headerFont = io.Fonts->AddFontDefault();
        subHeaderFont = io.Fonts->AddFontDefault();
        titleFont = io.Fonts->AddFontDefault();
    }

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ZapretManager manager;
    auto w_alts = manager.GetAvailableAlts();
    std::vector<std::string> alts;
    for (auto& w : w_alts) alts.push_back(WstringToUtf8(w));
    
    int currentAltIdx = 0;
    bool isConnected = false;
    bool isSearching = false;
    std::thread searchThread;
    std::thread updateThread;

    AddLog(u8"Добро пожаловать в ZAPRET.");
    updateThread = std::thread([&]() {
        // Get local version
        WCHAR pfPath[MAX_PATH];
        GetModuleFileNameW(NULL, pfPath, MAX_PATH);
        std::filesystem::path p(pfPath);
        std::ifstream vf(p.parent_path() / L"zapret_core" / L"version.txt");
        if (vf.is_open()) {
            std::getline(vf, g_ZapretVersion);
            vf.close();
            g_ZapretVersion.erase(g_ZapretVersion.find_last_not_of(" \n\r\t") + 1);
            g_ZapretVersion.erase(0, g_ZapretVersion.find_first_not_of(" \n\r\t"));
        }

        AutoUpdater::CheckAndUpdate(AddLog);
        
        // Refresh version after update check
        std::ifstream vfr(p.parent_path() / L"zapret_core" / L"version.txt");
        if (vfr.is_open()) {
            std::getline(vfr, g_ZapretVersion);
            vfr.close();
            g_ZapretVersion.erase(g_ZapretVersion.find_last_not_of(" \n\r\t") + 1);
            g_ZapretVersion.erase(0, g_ZapretVersion.find_first_not_of(" \n\r\t"));
        }

        auto new_w_alts = manager.GetAvailableAlts();
        alts.clear();
        for (auto& w : new_w_alts) alts.push_back(WstringToUtf8(w));
    });


    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg); ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)winW, (float)winH));
        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        // Custom Title Bar (Aesthetics Only)
        {
            float barH = 54.0f;
            // Dragging is now handled by WM_NCHITTEST in WndProc for better stability
            ImGui::SetCursorPos(ImVec2(25, 0));


            ImGui::BeginGroup();
            ImGui::SetCursorPosY(13);
            if (headerFont) ImGui::PushFont(headerFont);
            ImGui::TextColored(ImVec4(1, 1, 1, 1), "ZAPRET");
            if (headerFont) ImGui::PopFont();
            ImGui::SameLine(0, 13);
            ImGui::SetCursorPosY(29); // Lowered more to prevent "superscript" effect
            if (subHeaderFont) ImGui::PushFont(subHeaderFont);
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.5f, 0.6f), "|  v%s Stable", g_ZapretVersion.c_str());
            if (subHeaderFont) ImGui::PopFont();
            ImGui::EndGroup();







            // Rounded Square Control Buttons
            float btnSize = 29.0f;
            float btnPadding = (barH - btnSize) / 2.0f;
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
            
            // Close Button
            ImGui::SetCursorPos(ImVec2((float)winW - btnSize - 17, btnPadding));
            ImVec2 bPos = ImGui::GetCursorScreenPos();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
            if (ImGui::Button("##Close", ImVec2(btnSize, btnSize))) done = true;
            bool hovC = ImGui::IsItemHovered();
            drawList->AddRect(bPos, ImVec2(bPos.x + btnSize, bPos.y + btnSize), ImColor(0.6f, 0.6f, 0.6f, hovC ? 0.8f : 0.4f), 8.0f, 0, 1.5f);
            ImVec2 tSizeX = ImGui::CalcTextSize("X");
            drawList->AddText(ImVec2(bPos.x + (btnSize - tSizeX.x) / 2.0f, bPos.y + (btnSize - tSizeX.y) / 2.0f), ImColor(1.0f, 1.0f, 1.0f, hovC ? 1.0f : 0.7f), "X");
            ImGui::PopStyleColor(2);

            // Minimize Button
            ImGui::SetCursorPos(ImVec2((float)winW - (btnSize * 2) - 30, btnPadding));
            ImVec2 mPos = ImGui::GetCursorScreenPos();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
            if (ImGui::Button("##Min", ImVec2(btnSize, btnSize))) ::ShowWindow(hwnd, SW_MINIMIZE);
            bool hovM = ImGui::IsItemHovered();
            drawList->AddRect(mPos, ImVec2(mPos.x + btnSize, mPos.y + btnSize), ImColor(0.6f, 0.6f, 0.6f, hovM ? 0.8f : 0.4f), 8.0f, 0, 1.5f);
            ImVec2 tSizeM = ImGui::CalcTextSize("-");
            drawList->AddText(ImVec2(mPos.x + (btnSize - tSizeM.x) / 2.0f, mPos.y + (btnSize - tSizeM.y) / 2.0f), ImColor(1.0f, 1.0f, 1.0f, hovM ? 1.0f : 0.7f), "-");
            ImGui::PopStyleColor(2);


            
            ImGui::PopStyleVar();

            
            ImGui::PopStyleVar();
        }




        float padding = 26.0f;
        float contentW = (float)winW - (padding * 2.0f);

        // --- Status Card ---
        ImGui::SetCursorPos(ImVec2(padding, 80));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
        ImGui::BeginChild("StatusSection", ImVec2(contentW, 102), true);
        {
            ImGui::SetCursorPos(ImVec2(27, 24));
            if (subHeaderFont) ImGui::PushFont(subHeaderFont);
            ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.55f, 1.0f), u8"ТЕКУЩИЙ СТАТУС ОБХОДА");
            if (subHeaderFont) ImGui::PopFont();


            
            ImGui::SetCursorPos(ImVec2(27, 49));
            if (titleFont) ImGui::PushFont(titleFont);
            ImVec4 stCol = isConnected ? ImVec4(0.4f, 0.95f, 0.5f, 1.0f) : (isSearching ? ImVec4(1.0f, 0.8f, 0.3f, 1.0f) : ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
            ImGui::TextColored(stCol, "%s", g_StatusText.c_str());
            if (titleFont) ImGui::PopFont();

            // Status indicator — a plain static dot with a thin ring.
            // No pulsing animation, matching a serious/professional tone.
            ImVec2 ledPos = ImGui::GetCursorScreenPos();
            ledPos.x += contentW - 55; ledPos.y -= 37;
            drawList->AddCircleFilled(ledPos, 7.0f, ImGui::GetColorU32(stCol));
            drawList->AddCircle(ledPos, 7.0f, ImGui::GetColorU32(ImVec4(stCol.x, stCol.y, stCol.z, 0.35f)), 24, 1.5f);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();





        // --- Configuration & Actions ---
        ImGui::SetCursorPos(ImVec2(padding, 212));
        ImGui::BeginChild("ConfigSection", ImVec2(contentW, 169), true);
        {
            ImGui::SetCursorPos(ImVec2(27, 24));
            if (subHeaderFont) ImGui::PushFont(subHeaderFont);
            ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.55f, 1.0f), u8"НАСТРОЙКИ ОБХОДА");
            if (subHeaderFont) ImGui::PopFont();

            
            ImGui::SetCursorPos(ImVec2(20, 55));



            ImGui::SetNextItemWidth(contentW - 40);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f);
            ImGui::BeginDisabled(isConnected || isSearching);
            if (ImGui::BeginCombo("##Strat", alts.empty() ? "" : alts[currentAltIdx].c_str(), ImGuiComboFlags_HeightRegular)) {
                for (int n = 0; n < alts.size(); n++) {
                    bool is_selected = (currentAltIdx == n);
                    if (ImGui::Selectable(alts[n].c_str(), is_selected)) currentAltIdx = n;
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            ImGui::PopStyleVar(2);



            // Control Buttons
            ImGui::SetCursorPos(ImVec2(17, 102));
            float btnW = (contentW - 40 - 16) / 2.0f;
            
            if (!isConnected && !isSearching) {
                if (ImGui::Button(u8"ЗАПУСТИТЬ", ImVec2(btnW, 47))) {
                    g_StatusText = u8"Запуск...";
                    if (manager.StartAlt(Utf8ToWstring(alts[currentAltIdx]))) { isConnected = true; g_StatusText = u8"АКТИВНО"; AddLog(u8"Система успешно запущена."); }
                }
            } else if (isConnected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.28f, 0.28f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.33f, 0.33f, 1.0f));
                if (ImGui::Button(u8"ОСТАНОВИТЬ", ImVec2(btnW, 47))) { manager.StopAlt(); isConnected = false; g_StatusText = u8"Отключено"; AddLog(u8"Обход остановлен пользователем."); }
                ImGui::PopStyleColor(2);
            } else {
                ImGui::BeginDisabled(); ImGui::Button(u8"В ПРОЦЕССЕ...", ImVec2(btnW, 47)); ImGui::EndDisabled();
            }

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.24f, 1.0f));
            if (ImGui::Button(u8"АВТО-ПОИСК", ImVec2(btnW, 47)) && !isSearching) {
                isSearching = true; g_StatusText = u8"Поиск...";
                if (searchThread.joinable()) searchThread.join();
                searchThread = std::thread([&]() {
                    AddLog(u8"Начат автоматический подбор конфигурации...");
                    for (size_t i = 0; i < alts.size(); ++i) {
                        if (manager.TestAlt(Utf8ToWstring(alts[i]), AddLog)) {
                            currentAltIdx = (int)i; isConnected = true; g_StatusText = u8"АКТИВНО"; manager.StartAlt(Utf8ToWstring(alts[i])); 
                            AddLog(u8"Найдена рабочая конфигурация: " + alts[i]);
                            break;
                        }
                    }
                    isSearching = false;
                    if (!isConnected) { g_StatusText = u8"Не найдено"; AddLog(u8"Ошибка: Рабочая конфигурация не найдена."); }
                });
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        // --- Logs ---
        ImGui::SetCursorPos(ImVec2(padding, 410));
        if (subHeaderFont) ImGui::PushFont(subHeaderFont);
        ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.55f, 1.0f), u8"ЖУРНАЛ СОБЫТИЙ");
        if (subHeaderFont) ImGui::PopFont();

        
        ImGui::SetCursorPos(ImVec2(padding, 436));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.04f, 0.05f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
        ImGui::BeginChild("LogsArea", ImVec2(contentW, (float)winH - 436 - 72), true, ImGuiWindowFlags_AlwaysUseWindowPadding);



        {
            std::lock_guard<std::mutex> lock(g_LogMutex);
            for (const auto& log : g_Logs) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.31f, 0.45f, 0.62f, 1.0f));
                ImGui::Text(">");
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextWrapped("%s", log.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();


        // --- Footer ---
        ImGui::SetCursorPos(ImVec2(padding, (float)winH - 51));
        
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.45f, 1.0f), u8"Базируется на:");
        ImGui::SameLine();
        ImVec4 linkCol = ImVec4(0.42f, 0.58f, 0.76f, 1.0f);
        
        // Flowseal Link
        ImGui::TextColored(linkCol, "Flowseal/zapret-discord-youtube");
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(0)) ShellExecuteW(NULL, L"open", L"https://github.com/Flowseal/zapret-discord-youtube", NULL, NULL, SW_SHOWNORMAL);
            ImVec2 m = ImGui::GetItemRectMin();
            ImVec2 x = ImGui::GetItemRectMax();
            drawList->AddLine(ImVec2(m.x, x.y), x, ImColor(linkCol));
        }

        ImGui::SetCursorPos(ImVec2(padding, (float)winH - 36));
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.45f, 1.0f), u8"Репозиторий:");
        ImGui::SameLine();

        // Mujqk Link
        ImGui::TextColored(linkCol, "Mujqk/zapret-discord-youtube");
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(0)) ShellExecuteW(NULL, L"open", L"https://github.com/Mujqk/zapret-discord-youtube", NULL, NULL, SW_SHOWNORMAL);
            ImVec2 m = ImGui::GetItemRectMin();
            ImVec2 x = ImGui::GetItemRectMax();
            drawList->AddLine(ImVec2(m.x, x.y), x, ImColor(linkCol));
        }


        
        ImGui::SetCursorPos(ImVec2((float)winW - padding - 152, (float)winH - 55));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.45f)); // Vertical center fix
        if (ImGui::Button(u8"ОТКРЫТЬ ФАЙЛЫ", ImVec2(152, 34))) {
            WCHAR pfPath[MAX_PATH];
            GetModuleFileNameW(NULL, pfPath, MAX_PATH);
            std::filesystem::path p(pfPath);
            ShellExecuteW(NULL, L"open", (p.parent_path() / L"zapret_core").wstring().c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
        ImGui::PopStyleVar();


        ImGui::End();

        ImGui::Render();
        const float cl[4] = { 0.07f, 0.08f, 0.09f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cl);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0); 
    }

    if (searchThread.joinable()) searchThread.join();
    if (updateThread.joinable()) updateThread.join();
    manager.StopAlt();
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    CleanupDeviceD3D(); ::DestroyWindow(hwnd); ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (hSingleInstanceMutex) { ReleaseMutex(hSingleInstanceMutex); CloseHandle(hSingleInstanceMutex); }
    return 0;
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2; sd.BufferDesc.Width = 0; sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl; const D3D_FEATURE_LEVEL fla[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fla, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext) != S_OK) return false;
    CreateRenderTarget(); return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) g_pSwapChain->Release(); if (g_pd3dDeviceContext) g_pd3dDeviceContext->Release(); if (g_pd3dDevice) g_pd3dDevice->Release();
}

void CreateRenderTarget() {
    ID3D11Texture2D* pb; g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pb));
    g_pd3dDevice->CreateRenderTargetView(pb, nullptr, &g_mainRenderTargetView); pb->Release();
}

void CleanupRenderTarget() { if (g_mainRenderTargetView) g_mainRenderTargetView->Release(); }

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE: if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) { g_ResizeWidth = (UINT)LOWORD(lParam); g_ResizeHeight = (UINT)HIWORD(lParam); } return 0;
    case WM_SYSCOMMAND: if ((wParam & 0xfff0) == SC_KEYMENU) return 0; break;
    case WM_DESTROY: ::PostQuitMessage(0); return 0;
    case WM_NCHITTEST: {
        POINT pt = { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
        ::ScreenToClient(hWnd, &pt);
        RECT rc; ::GetClientRect(hWnd, &rc);
        if (pt.y >= 0 && pt.y < 54 && pt.x >= 0 && pt.x < (rc.right - 110)) return HTCAPTION;
        break;
    }
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

