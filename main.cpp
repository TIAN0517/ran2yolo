#include "game_process.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <cstdarg>
#include <clocale>

#include "memory_reader.h"
#include "bot_logic.h"
#include "gui_ranbot.h"
#include "offset_config.h"
#include "config_updater.h"
#include "license_admin_gui.h"

// ImGui
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"

// 內嵌 DLL
#include "embed_dlls.h"

// 連結 DirectX9 庫（因為無法修改 vcxproj）
#ifdef _MSC_VER
#pragma comment(lib, "d3d9.lib")
#endif

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void Dbg(const char* msg);
static void Dbgf(const char* fmt, ...);
void CleanupGameProcess();

// ============================================================
// 窗口尺寸常量
// ============================================================
static const int WINDOW_WIDTH = 720;
static const int WINDOW_HEIGHT = 900;

// ============================================================
// DirectX9 全局變量
// ============================================================
static LPDIRECT3D9              g_pD3D = nullptr;
static LPDIRECT3DDEVICE9        g_pd3dDevice = nullptr;
static D3DPRESENT_PARAMETERS    g_d3dpp = {};
HWND                            g_hWnd = nullptr;  // 非 static，供其他模組使用
static bool                     g_imguiReady = false;

// ============================================================
// GUI 全局變量
// ============================================================
bool g_guiVisible = true;             // GUI 是否可見
bool g_alwaysOnTop = false;           // 是否置頂（默認關閉，不擋遊戲）

// ============================================================
// 創建 D3D 設備
// ============================================================
static bool CreateDeviceD3D(HWND hWnd) {
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr)
        return false;

    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));

    // 🐛 修復 #1: 正確初始化 BackBuffer 尺寸
    RECT rect;
    GetClientRect(hWnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    // 確保最小尺寸
    if (width < 100) width = WINDOW_WIDTH;
    if (height < 100) height = WINDOW_HEIGHT;

    g_d3dpp.BackBufferWidth = width;
    g_d3dpp.BackBufferHeight = height;
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    // 🐛 修復 #2: 使用更兼容的深度缓冲格式
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D24X8;  // 比 D3DFMT_D16 更兼容
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
        return false;

    // ✅ 確保設備視口與窗口完全一致
    D3DVIEWPORT9 vp;
    vp.X = 0;
    vp.Y = 0;
    vp.Width = width;
    vp.Height = height;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    g_pd3dDevice->SetViewport(&vp);

    return true;
}

// ============================================================
// 清理 D3D 設備
// ============================================================
static void CleanupDeviceD3D() {
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = nullptr; }
}

// ============================================================
// 窗口處理函數
// ============================================================
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return TRUE;

    switch (msg) {
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProc(hWnd, msg, wParam, lParam);
        if (hit == HTCLIENT) {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            ScreenToClient(hWnd, &pt);
            RECT rc;
            GetClientRect(hWnd, &rc);
            const bool onHeaderButtons = pt.x >= (rc.right - 78) && pt.y >= 0 && pt.y < 48;
            if (pt.y >= 0 && pt.y < 48 && !onHeaderButtons)
                return HTCAPTION;
        }
        return hit;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = WINDOW_WIDTH;
        mmi->ptMinTrackSize.y = WINDOW_HEIGHT;
        return 0;
    }

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;

        if (g_pd3dDevice != nullptr) {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);

            if (width > 0 && height > 0) {
                g_d3dpp.BackBufferWidth = width;
                g_d3dpp.BackBufferHeight = height;
                if (g_imguiReady) {
                    ImGui_ImplDX9_InvalidateDeviceObjects();
                }

                HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
                if (hr == D3D_OK) {
                    if (g_imguiReady) {
                        ImGui_ImplDX9_CreateDeviceObjects();
                    }
                } else {
                    printf("[警告] Device Reset 失敗: 0x%08lX\n", (unsigned long)hr);
                }
            }
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;

    case WM_CLOSE:
        Dbg("WndProc received WM_CLOSE");
        g_Running = false;
        g_guiVisible = false;
        ShowWindow(hWnd, SW_HIDE);
        PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        Dbg("WndProc received WM_DESTROY");
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ============================================================
// Bot 執行緒（遊戲心跳）
// ============================================================
static DWORD WINAPI BotThread(LPVOID) {
    GameHandle gh{};

    DWORD lastFindLog = 0;
    auto SleepInterruptible = [](DWORD totalMs) -> bool {
        DWORD waited = 0;
        while (g_Running && waited < totalMs) {
            DWORD chunk = (totalMs - waited > 50) ? 50 : (totalMs - waited);
            Sleep(chunk);
            waited += chunk;
        }
        return g_Running;
    };

    while (g_Running) {
        if (!FindGameProcess(&gh)) {
            DWORD now = GetTickCount();
            if (now - lastFindLog > 3000) {
                UIAddLog("[偵測] 找不到 Game.exe，等待中...");
                lastFindLog = now;
            }
            if (!SleepInterruptible(2000)) break;
            continue;
        }

        UIAddLog("[偵測] 已附加遊戲：PID=%u Base=0x%08X HWND=%p",
            gh.pid, gh.baseAddr, gh.hWnd);
        printf("[偵測] 找到遊戲！PID=%lu Base=0x%lX\n",
            gh.pid, gh.baseAddr);

        int clientW = 0;
        int clientH = 0;
        if (GetGameClientSize(&gh, &clientW, &clientH)) {
            UIAddLog("[Detect] Game client=%dx%d", clientW, clientH);
            if (clientW != 1024 || clientH != 768) {
                if (EnsureGameClientSize(&gh, 1024, 768)) {
                    UIAddLog("[Detect] Normalized game client to 1024x768");
                } else {
                    UIAddLog("[Detect] Failed to normalize game client, current=%dx%d",
                        clientW, clientH);
                }
            }
        }

        // 更新全域句柄供 GUI 使用
        SetGameHandle(&gh);

        // DEBUG: 驗證 SetGameHandle 是否正確存儲
        {
            GameHandle verifyGh = GetGameHandle();
            UIAddLog("[DEBUG] SetGameHandle 驗證: pid=%u hProcess=%p baseAddr=0x%08X attached=%d",
                verifyGh.pid, verifyGh.hProcess, verifyGh.baseAddr, verifyGh.attached);
            printf("[DEBUG] SetGameHandle 驗證: pid=%u hProcess=%p baseAddr=0x%08X attached=%d\n",
                verifyGh.pid, verifyGh.hProcess, verifyGh.baseAddr, verifyGh.attached);
        }

        // BotTick 迴圈（同時輪詢熱鍵）
        // 改為：如果 hProcess 有效就嘗試運行，baseAddr=0 會由 BotTick 內部的刷新邏輯處理
        while (g_Running && gh.hProcess && IsGameRunning(&gh)) {
            // F11/F12 現由專屬熱鍵執行緒處理（SetWindowsHookEx）
            BotTick(&gh);

            // 如果 gh.baseAddr=0，嘗試刷新（每 10 秒一次）
            if (!gh.baseAddr && gh.hProcess) {
                static DWORD lastRefresh = 0;
                DWORD now = GetTickCount();
                if (now - lastRefresh > 10000) {
                    DWORD newBase = RefreshGameBaseAddress(&gh);
                    if (newBase) {
                        gh.baseAddr = newBase;
                        gh.attached = 1;
                        SetGameHandle(&gh);  // 更新全域句柄
                        UIAddLog("[DEBUG] 刷新 baseAddr 成功: 0x%08X", newBase);
                    }
                    lastRefresh = now;
                }
            }
        }

        CloseGameHandle(&gh);
        gh = GameHandle{};
        SetGameHandle(NULL);  // 清除全域句柄
        if (!g_Running) break;
        UIAddLog("[偵測] 遊戲已關閉，重新等待...");
        if (!SleepInterruptible(2000)) break;
    }
    return 0;
}

// ============================================================
// 設置深色科技風格主題
// ============================================================
void SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // 圓角
    style.WindowRounding = 0.0f;  // ✅ 無圓角邊框
    style.FrameRounding = 4.0f;
    style.TabRounding = 0.0f;  // ✅ 標籤無圓角
    style.GrabRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 0.0f;

    // 邊框
    style.WindowBorderSize = 0.0f;  // ✅ 無窗口邊框
    style.ChildBorderSize = 0.0f;   // ✅ 無子窗口邊框
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;   // ✅ 無框架邊框
    style.TabBorderSize = 0.0f;     // ✅ 無標籤邊框

    // 間距優化 - Tab 分頁可讀性
    style.WindowPadding = ImVec2(0, 0);  // ✅ 無窗口內邊距
    style.FramePadding = ImVec2(4, 3);   // ✅ 稍微增加框架內邊距
    style.ItemSpacing = ImVec2(8, 6);    // ✅ 增加項目間距，提高可讀性
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.TabBarBorderSize = 0.0f;       // ✅ Tab 欄無邊框
    style.TabMinWidthForCloseButton = 0.0f;  // ✅ 不顯示 Tab 關閉按鈕

    // 深色藍紫科技主題（移除所有可見邊框）
    // 背景顏色統一為 RGB(30, 30, 40) = 0.118, 0.118, 0.157
    const ImVec4 uniformBg = ImVec4(0.118f, 0.118f, 0.157f, 1.0f);  // 統一背景色

    colors[ImGuiCol_WindowBg]           = uniformBg;
    colors[ImGuiCol_ChildBg]            = uniformBg;
    colors[ImGuiCol_PopupBg]            = uniformBg;
    colors[ImGuiCol_FrameBg]            = uniformBg;  // ✅ 統一為背景色
    colors[ImGuiCol_FrameBgHovered]     = uniformBg;  // ✅ 統一為背景色
    colors[ImGuiCol_FrameBgActive]      = uniformBg;  // ✅ 統一為背景色
    colors[ImGuiCol_Border]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);  // ✅ 無邊框
    colors[ImGuiCol_BorderShadow]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);  // ✅ 無陰影
    colors[ImGuiCol_TitleBg]            = uniformBg;  // ✅ 統一為背景色
    colors[ImGuiCol_TitleBgActive]      = uniformBg;  // ✅ 統一為背景色
    colors[ImGuiCol_TitleBgCollapsed]   = uniformBg;  // ✅ 統一為背景色
    colors[ImGuiCol_Tab]                = uniformBg;  // ✅ 統一為背景色
    colors[ImGuiCol_TabHovered]         = ImVec4(0.28f, 0.35f, 0.60f, 0.80f);
    colors[ImGuiCol_TabActive]          = ImVec4(0.20f, 0.25f, 0.48f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.20f, 0.38f, 1.00f);
    colors[ImGuiCol_Button]             = ImVec4(0.18f, 0.22f, 0.38f, 1.00f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.28f, 0.35f, 0.58f, 1.00f);
    colors[ImGuiCol_ButtonActive]       = ImVec4(0.35f, 0.42f, 0.65f, 1.00f);
    colors[ImGuiCol_Header]             = ImVec4(0.18f, 0.22f, 0.38f, 0.80f);
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.28f, 0.35f, 0.58f, 0.80f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.35f, 0.42f, 0.65f, 1.00f);
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.35f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.45f, 0.55f, 0.80f, 1.00f);
    colors[ImGuiCol_CheckMark]          = ImVec4(0.45f, 0.60f, 0.90f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.06f, 0.06f, 0.10f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.25f, 0.25f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.35f, 0.55f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.45f, 0.45f, 0.65f, 1.00f);
    colors[ImGuiCol_Text]               = ImVec4(0.88f, 0.90f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]       = ImVec4(0.45f, 0.45f, 0.55f, 1.00f);
    colors[ImGuiCol_Separator]          = ImVec4(0.25f, 0.25f, 0.40f, 0.50f);
    colors[ImGuiCol_PlotHistogram]      = ImVec4(0.35f, 0.55f, 0.85f, 1.00f);
}

// ============================================================
// 主程式
// ============================================================
static void Dbg(const char* msg) {
    FILE* fp = NULL;
    char logPath[MAX_PATH];
    GetModuleFileNameA(NULL, logPath, MAX_PATH);
    char* lastSlash = strrchr(logPath, '\\');
    if (lastSlash) *lastSlash = '\0';
    strncat_s(logPath, MAX_PATH, "\\dbg_main.log", _TRUNCATE);
    fopen_s(&fp, logPath, "a");
    if (fp) { fprintf(fp, "%s\n", msg); fclose(fp); }
    HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    WriteFile(hCon, msg, (DWORD)strlen(msg), &written, NULL);
    WriteFile(hCon, "\n", 1, &written, NULL);
}

static void Dbgf(const char* fmt, ...) {
    char msg[512] = {0};
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, args);
    va_end(args);
    Dbg(msg);
}

int main() {
    // 設定控制台為 UTF-8 編碼（Win7/Win10 通用，解決中文顯示亂碼）
    SetConsoleOutputCP(65001);   // CP_UTF8
    SetConsoleCP(65001);
    setlocale(LC_ALL, ".65001");

    // 內嵌 DLL 解壓 + 載入（ONNX Runtime）
    printf("[Init] Loading embedded DLLs...\n");
    if (!LoadEmbeddedDll(IDR_ONNXRUNTIME_DLL)) {
        printf("[Init] WARNING: Failed to load onnxruntime.dll (YOLO disabled)\n");
    }
    if (!LoadEmbeddedDll(IDR_ONNXRUNTIME_PROV)) {
        printf("[Init] WARNING: Failed to load onnxruntime_providers_shared.dll\n");
    }

    // 檢查命令列參數
    bool hiddenMode = false;
    bool launchAdminGui = false;
    const char* explicitOffsetPath = NULL;
    for (int i = 1; i < __argc; i++) {
        if (!__argv[i]) continue;
        if (_stricmp(__argv[i], "-admin") == 0) {
            launchAdminGui = true;
            continue;
        }
        if (_stricmp(__argv[i], "-hidden") == 0 || _stricmp(__argv[i], "-h") == 0) {
            hiddenMode = true;
            continue;
        }
        if ((_stricmp(__argv[i], "-offsets") == 0 || _stricmp(__argv[i], "-offset-file") == 0) &&
            i + 1 < __argc && __argv[i + 1]) {
            explicitOffsetPath = __argv[++i];
        }
    }

    // -admin 模式：啟動授權管理 GUI
    if (launchAdminGui) {
        CoInitialize(NULL);
        if (InitLicenseAdminGui(GetModuleHandle(NULL), SW_SHOWNORMAL)) {
            MSG msg = {};
            while (GetMessageW(&msg, NULL, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        CoUninitialize();
        return 0;
    }

    Dbg("=== 主程式進入 ===");
    {
        wchar_t exePath[MAX_PATH] = {};
        if (GetModuleFileNameW(NULL, exePath, MAX_PATH) > 0) {
            char exePathA[MAX_PATH] = {};
            WideCharToMultiByte(CP_UTF8, 0, exePath, -1, exePathA, MAX_PATH, NULL, NULL);
            printf("[Init] EXE Path: %s\n", exePathA);
            Dbgf("[Init] EXE Path: %s", exePathA);
        }
    }
    ImGui_ImplWin32_EnableDpiAwareness();

    InitBotLogic();
    Dbg("機器人邏輯初始化完成");

    // 偏移策略：優先載入外部 offsets.dat / offsets.ini，找不到才回退內建 defaults
    printf("[Config] Loading runtime offsets...\n");
    bool offsetsLoaded = OffsetConfig::LoadFromFile(explicitOffsetPath);
    if (offsetsLoaded) {
        printf("[Config] Offsets loaded from: %s (ver: %lu)\n",
            OffsetConfig::GetLoadSource(), (unsigned long)OffsetConfig::GetConfigVersion());
    } else {
        printf("[Config] No runtime offsets found; using built-in defaults.\n");
    }
    Dbgf("[Config] offset_source=%s version=%lu explicit_offset=%s",
        OffsetConfig::GetLoadSource(),
        (unsigned long)OffsetConfig::GetConfigVersion(),
        explicitOffsetPath ? explicitOffsetPath : "(auto)");
    Dbgf("[Config] GLCharacter=0x%08X MapID=0x%08X PosX=0x%08X PosZ=0x%08X HasTarget=0x%08X",
        OffsetConfig::GLCharacterObj(),
        OffsetConfig::PlayerMapID(),
        OffsetConfig::PlayerPosX(),
        OffsetConfig::PlayerPosZ(),
        OffsetConfig::TargetHasTarget());

    // 初始化遠端更新器，但不在正常啟動時自動 reload 外部偏移
    ConfigUpdater::Init();
    printf("[Updater] Auto update on startup is disabled.\n");
    Dbg("[Updater] Auto update on startup is disabled");

    // 初始化熱鍵系統
    printf("[Init] Before InitHotkeys...\n");
    InitHotkeys();
    printf("[Init] After InitHotkeys\n");

    // NT API 記憶體初始化
    if (!InitNtMemoryFunctions()) {
        printf("[警告] NT API 初始化失敗，使用傳統模式\n");
        Dbg("NT API 初始化失敗");
    }
    Dbg("NT API 記憶體初始化完成");

    // 啟動 Bot 執行緒
    printf("[Init] Before CreateThread...\n");
    HANDLE hBot = CreateThread(NULL, 0, BotThread, NULL, 0, NULL);
    if (!hBot) {
        printf("[錯誤] 建立 Bot 執行緒失敗！\n");
        Dbg("建立 Bot 執行緒失敗");
        g_Running = false;
        return 1;
    }
    printf("[啟動] Bot 執行緒已建立\n");
    Dbg("Bot 執行緒已建立");

    // ============================================================
    // 創建 GUI 窗口
    // ============================================================
    // 創建 GUI 窗口 - 完整深色科技風主視窗
    // ============================================================
    printf("[Init] Creating window class...\n");
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = TEXT("JyTrainerWindow");
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIconSm = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_SHARED);
    // 深色背景刷，與 ImGui 主題一致
    wc.hbrBackground = CreateSolidBrush(RGB(26, 28, 38));
    RegisterClassEx(&wc);

    printf("[Init] Creating window...\n");
    DWORD dwExStyle = WS_EX_APPWINDOW;
    if (g_alwaysOnTop) dwExStyle |= WS_EX_TOPMOST;

    // 無邊框純 ImGui 窗口 - 完全由 ImGui 控制外觀
    DWORD dwStyle = WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX;

    g_hWnd = CreateWindowExW(dwExStyle, L"JyTrainerWindow", L"JyTrainer",
        dwStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, wc.hInstance, NULL);

    if (!CreateDeviceD3D(g_hWnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(L"JyTrainerWindow", wc.hInstance);
        return 1;
    }

    if (hiddenMode) {
        g_guiVisible = false;
        ShowWindow(g_hWnd, SW_HIDE);
        Dbg("=== 隱藏模式啟動 ===");
    } else {
        g_guiVisible = true;
        ShowWindow(g_hWnd, SW_SHOWDEFAULT);
    }
    UpdateWindow(g_hWnd);

    // F10 GUI 隱藏由 gui_ranbot.cpp 的 LowLevelKeyboardProc (WH_KEYBOARD_LL) 處理
    // 注意：請勿在這裡用 RegisterHotKey 註冊同一個 F10，會造成雙重 toggle 衝突

    // 初始化 ImGui
    printf("[Init] Creating ImGui context...\n");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // 加載中文字體
    printf("[Init] Loading fonts...\n");
    ImFontConfig fontCfg;
    fontCfg.FontNo = 0;  // TTC 中第一個字體

    // 預先獲取字形範圍（避免重複調用）
    static const ImWchar* glyphRanges = io.Fonts->GetGlyphRangesChineseFull();
    printf("[Init] Getting glyph ranges...\n");

    // 嘗試載入微軟正黑體
    ImFont* font = NULL;
    FILE* f = fopen("C:\\Windows\\Fonts\\msjh.ttc", "rb");
    if (f) {
        fclose(f);
        printf("[Init] Loading msjh.ttc...\n");
        font = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\msjh.ttc", 16.0f, &fontCfg, glyphRanges);
    }

    // 如果正黑體載入失敗，嘗試細明體
    if (!font) {
        FILE* f2 = fopen("C:\\Windows\\Fonts\\mingliu.ttc", "rb");
        if (f2) { fclose(f2); printf("[Init] Loading mingliu.ttc...\n"); }
        fontCfg.FontNo = 0;
        font = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\mingliu.ttc", 16.0f, &fontCfg, glyphRanges);
    }

    // 最後 fallback：使用新宋體
    if (!font) {
        FILE* f3 = fopen("C:\\Windows\\Fonts\\simsun.ttc", "rb");
        if (f3) { fclose(f3); printf("[Init] Loading simsun.ttc...\n"); }
        font = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\simsun.ttc", 16.0f, &fontCfg, glyphRanges);
    }

    printf("[Init] Font loading done. Font pointer: %p\n", (void*)font);

    SetupImGuiStyle();

    printf("[Init] Initializing Win32 ImGui...\n");
    ImGui_ImplWin32_Init(g_hWnd);
    printf("[Init] Initializing DX9 ImGui...\n");
    ImGui_ImplDX9_Init(g_pd3dDevice);
    g_imguiReady = true;

    // ✅ 確保 DisplaySize 正確設置
    RECT rect;
    GetClientRect(g_hWnd, &rect);
    io.DisplaySize = ImVec2((float)rect.right, (float)rect.bottom);

    printf("[Init] ImGui initialization complete! DisplaySize=%.0fx%.0f\n", io.DisplaySize.x, io.DisplaySize.y);

    // 消息循環
    MSG msg = {};
    while (msg.message != WM_QUIT && g_Running) {
        // ── 每回合熱鍵輪詢（F10/F11/F12，不受視窗前景影響）──
        // 注意：熱鍵現由專屬執行緒 + Low-Level Keyboard Hook 處理
        // PollHotkeys() 已停用，請使用 F10/F11/F12 測試

        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            // ⚠️ 不要 continue！需要繼續處理渲染，讓 ImGui 更新幀
        }

        // GUI 隱藏時跳過渲染，降低 CPU 佔用
        if (!g_guiVisible) {
            Sleep(50);  // 降低空轉 CPU
            continue;
        }

        // 開始 ImGui 幀（每次循環都要更新，確保滑鼠狀態正確）
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 渲染主 GUI
        RenderMainGUI();

        // 渲染
        ImGui::EndFrame();

        // 🐛 修復: 增強設備狀態檢查，處理設備丟失和重置
        HRESULT deviceStatus = g_pd3dDevice->TestCooperativeLevel();
        if (deviceStatus == D3DERR_DEVICELOST) {
            // 設備丟失，等待恢復
            Sleep(50);
            continue;
        } else if (deviceStatus == D3DERR_DEVICENOTRESET) {
            // 設備需要重置
            ImGui_ImplDX9_InvalidateDeviceObjects();
            HRESULT resetResult = g_pd3dDevice->Reset(&g_d3dpp);
            if (resetResult == D3D_OK) {
                ImGui_ImplDX9_CreateDeviceObjects();
            } else {
                printf("[警告] Device Reset 失敗: 0x%08lX\n", (unsigned long)resetResult);
                Sleep(50);
                continue;
            }
        }

        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);

        // ✅ 確保視口與窗口一致
        RECT rc;
        GetClientRect(g_hWnd, &rc);
        D3DVIEWPORT9 vp = {0, 0, (DWORD)rc.right, (DWORD)rc.bottom, 0.0f, 1.0f};
        g_pd3dDevice->SetViewport(&vp);

        // 深色背景清除
        g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 26, 28, 38), 1.0f, 0);

        if (g_pd3dDevice->BeginScene() >= 0) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
            g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
        }

        Sleep(16);  // ~60 FPS 幀率限制
    }

    Dbgf("[Main] Loop exit: g_Running=%d msg=%u", g_Running ? 1 : 0, (unsigned int)msg.message);
    g_Running = false;
    ShutdownHotkeys();
    if (g_hWnd) {
        ShowWindow(g_hWnd, SW_HIDE);
    }
    if (hBot) {
        WaitForSingleObject(hBot, INFINITE);
        CloseHandle(hBot);
        hBot = NULL;
    }

    // 清理 ImGui
    g_imguiReady = false;
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    // F10 由 WH_KEYBOARD_LL 鉤子處理，無需 UnregisterHotKey
    ShutdownBotLogic();
    CleanupGameProcess();
    CleanupDeviceD3D();
    DestroyWindow(g_hWnd);
    UnregisterClassW(L"JyTrainerWindow", wc.hInstance);

    printf("\n[關閉] 正在結束...\n");
    Dbg("=== 主程式結束 ===");
    return 0;
}
