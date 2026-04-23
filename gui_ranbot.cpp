#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include <windows.h>
#include <winuser.h>

// GET_X_LPARAM / GET_Y_LPARAM 宏定義（Windows SDK 可能未定義）
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

#include "gui_ranbot.h"
#include "bot_logic.h"
#include "offset_config.h"
#include "config_updater.h"
#include "offline_license.h"
#include "game_process.h"
#include "attack_packet.h"
#include "coords.h"
#include "coord_calib.h"
#include "imgui/imgui.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

// ------------------------------------------------------------
// 外部全域
// ------------------------------------------------------------
extern bool g_guiVisible;
extern HWND g_hWnd;
void UIAddLog(const char* fmt, ...);
void ToggleBotActive();    // ✅ BUG-008 FIX: 移動到頂部宣告
void ForceStopBot();       // ✅ BUG-008 FIX: 移動到頂部宣告

// ------------------------------------------------------------
// UI Theme
// ------------------------------------------------------------
struct JyTheme {
    ImVec4 bg0         = ImVec4(0.05f, 0.06f, 0.10f, 1.0f);
    ImVec4 bg1         = ImVec4(0.07f, 0.09f, 0.14f, 1.0f);
    ImVec4 bg2         = ImVec4(0.09f, 0.11f, 0.17f, 1.0f);
    ImVec4 panel       = ImVec4(0.10f, 0.12f, 0.18f, 1.0f);
    ImVec4 panel2      = ImVec4(0.13f, 0.16f, 0.24f, 1.0f);
    ImVec4 border      = ImVec4(0.20f, 0.28f, 0.42f, 1.0f);
    ImVec4 accent      = ImVec4(0.24f, 0.56f, 0.98f, 1.0f);
    ImVec4 accent2     = ImVec4(0.34f, 0.66f, 1.00f, 1.0f);
    ImVec4 text        = ImVec4(0.90f, 0.93f, 0.98f, 1.0f);
    ImVec4 textDim     = ImVec4(0.60f, 0.66f, 0.78f, 1.0f);
    ImVec4 btn         = ImVec4(0.16f, 0.22f, 0.34f, 1.0f);
    ImVec4 btnHover    = ImVec4(0.22f, 0.31f, 0.48f, 1.0f);
    ImVec4 btnActive   = ImVec4(0.28f, 0.40f, 0.62f, 1.0f);
    ImVec4 tab         = ImVec4(0.11f, 0.14f, 0.21f, 1.0f);
    ImVec4 tabHover    = ImVec4(0.18f, 0.25f, 0.39f, 1.0f);
    ImVec4 tabActive   = ImVec4(0.24f, 0.36f, 0.58f, 1.0f);
    ImVec4 green       = ImVec4(0.30f, 0.85f, 0.45f, 1.0f);
    ImVec4 red         = ImVec4(0.95f, 0.32f, 0.32f, 1.0f);
};

static JyTheme T;

// ------------------------------------------------------------
// 狀態
// ------------------------------------------------------------
static char s_licenseKey[4096] = "";
static char s_licenseDays[32] = "---";
static char s_licenseMsg[256] = "未驗證";
static char s_licensePerms[512] = "";
static bool s_licenseChecked = false;

static void NormalizeLicenseKey(char* key) {
    if (!key) return;
    char tmp[4096] = {0};
    size_t out = 0;
    for (size_t i = 0; key[i] && out < sizeof(tmp) - 1; i++) {
        unsigned char ch = (unsigned char)key[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 'a' + 'A');
        tmp[out++] = (char)ch;
    }
    tmp[out] = '\0';
    strcpy_s(key, 4096, tmp);
}

static DWORD WINAPI LicenseVerifyThreadProc(LPVOID param) {
    char* token = (char*)param;
    if (!token) return 0;

    OfflineLicenseInfo info = {};
    bool success = OfflineLicenseVerifyToken(token, NULL, &info);

    if (success && info.valid) {
        if (info.days_left >= 0) {
            sprintf_s(s_licenseDays, "%d 天", info.days_left);
        } else {
            strcpy_s(s_licenseDays, "未知");
        }
        strcpy_s(s_licenseMsg, "驗證成功");
        SetLicenseValid(true);

        // 保存到本地緩存
        OfflineLicenseSaveCached(token);

        if (!info.permissions.empty()) {
            char permBuf[512] = "";
            for (size_t i = 0; i < info.permissions.size(); i++) {
                if (i > 0) strcat_s(permBuf, ", ");
                strcat_s(permBuf, info.permissions[i].c_str());
            }
            strcpy_s(s_licensePerms, permBuf);
            UIAddLog("[License] 權限: %s", s_licensePerms);
        } else {
            strcpy_s(s_licensePerms, "無特殊權限");
        }

        printf("[License] 驗證成功，剩餘 %s\n", s_licenseDays);
        UIAddLog("[License] 驗證成功，剩餘 %s", s_licenseDays);
    } else {
        strcpy_s(s_licenseDays, "---");
        strcpy_s(s_licensePerms, "");
        strcpy_s(s_licenseMsg, info.message.empty() ? "驗證失敗" : info.message.c_str());
        SetLicenseValid(false);
        printf("[License] 驗證失敗: %s\n", s_licenseMsg);
        UIAddLog("[License] 驗證失敗: %s", s_licenseMsg);
    }

    s_licenseChecked = true;
    delete[] token;
    return 0;
}

static void StartLicenseVerifyAsync() {
    NormalizeLicenseKey(s_licenseKey);

    if (strlen(s_licenseKey) == 0) {
        strcpy_s(s_licenseMsg, "請輸入卡密");
        strcpy_s(s_licenseDays, "---");
        SetLicenseValid(false);
        UIAddLog("[License] 請先輸入卡密");
        return;
    }

    if (!OfflineLicenseLooksLikeToken(s_licenseKey)) {
        strcpy_s(s_licenseMsg, "卡密格式無效");
        strcpy_s(s_licenseDays, "---");
        s_licenseChecked = true;
        UIAddLog("[License] 卡密格式無效");
        return;
    }

    strcpy_s(s_licenseMsg, "驗證中...");
    UIAddLog("[License] 開始驗證離線卡密...");

    char* tokenCopy = new char[strlen(s_licenseKey) + 1];
    strcpy_s(tokenCopy, strlen(s_licenseKey) + 1, s_licenseKey);

    HANDLE hThread = CreateThread(NULL, 0, LicenseVerifyThreadProc, tokenCopy, 0, NULL);
    if (!hThread) {
        strcpy_s(s_licenseMsg, "建立驗證執行緒失敗");
        UIAddLog("[License] 建立驗證執行緒失敗");
        delete[] tokenCopy;
        return;
    }
    CloseHandle(hThread);
}

static bool s_autoLoadTried = false;

static void TryAutoLoadLicense() {
    if (s_autoLoadTried) return;
    s_autoLoadTried = true;

    if (OfflineLicenseLoadCached(s_licenseKey, sizeof(s_licenseKey))) {
        printf("[License] 載入本地緩存卡密，自動驗證...\n");
        UIAddLog("[License] 載入本地緩存，自動驗證中");
        StartLicenseVerifyAsync();
    } else {
        printf("[License] 無本地緩存卡密\n");
    }
}

// ------------------------------------------------------------
// 日誌
// ------------------------------------------------------------
static char s_logBuf[64][512];
static int s_logHead = 0, s_logCount = 0;
static CRITICAL_SECTION s_logCs;
static volatile bool s_logReady = false;
static volatile LONG s_logCsInit = 0;  // Win7兼容

// Win7兼容的延遲初始化
static void EnsureLogCsReady() {
    if (InterlockedCompareExchange(&s_logCsInit, 1, 0) == 0) {
        InitializeCriticalSection(&s_logCs);
        s_logReady = true;
    }
}

void UIAddLog(const char* fmt, ...) {
    EnsureLogCsReady();
    EnterCriticalSection(&s_logCs);
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_logBuf[s_logHead % 64], 512, fmt, args);
    va_end(args);
    printf("%s\n", s_logBuf[s_logHead % 64]);
    s_logHead = (s_logHead + 1) % 64;
    if (s_logCount < 64) s_logCount++;
    LeaveCriticalSection(&s_logCs);
}

void FlushAllLogs() {
    if (!s_logReady) return;
    EnterCriticalSection(&s_logCs);
    int start = (s_logHead - s_logCount + 64 * 100) % 64;
    for (int i = 0; i < s_logCount; i++)
        puts(s_logBuf[(start + i) % 64]);
    s_logHead = s_logCount = 0;
    LeaveCriticalSection(&s_logCs);
}

// ------------------------------------------------------------
// 熱鍵
// ------------------------------------------------------------
static HHOOK s_hKeyboardHook = NULL;
static HANDLE s_hHotkeyThread = NULL;
static HANDLE s_hHotkeyReadyEvent = NULL;
static DWORD s_hotkeyThreadId = 0;
static volatile bool s_hotkeysRunning = false;
static volatile bool s_f10Down = false, s_f11Down = false, s_f12Down = false;
static volatile bool s_f7Down = false;
static HHOOK s_hMouseHook = NULL;
static HWND s_gameHwndForCalib = NULL;  // 遊戲視窗 HWND（用於校正）

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKbd = (KBDLLHOOKSTRUCT*)lParam;
        bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool isUp   = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
        if (!isDown && !isUp)
            return CallNextHookEx(s_hKeyboardHook, nCode, wParam, lParam);

        if (pKbd->vkCode == VK_F10 && isDown && !s_f10Down) {
            s_f10Down = true;
            g_guiVisible = !g_guiVisible;
            ShowWindow(g_hWnd, g_guiVisible ? SW_SHOW : SW_HIDE);
        } else if (pKbd->vkCode == VK_F10 && isUp) {
            s_f10Down = false;
        } else if (pKbd->vkCode == VK_F11 && isDown && !s_f11Down) {
            s_f11Down = true;
            UIAddLog("[Hotkey] F11 開始/暫停");
            ToggleBotActive();
        } else if (pKbd->vkCode == VK_F11 && isUp) {
            s_f11Down = false;
        } else if (pKbd->vkCode == VK_F12 && isDown && !s_f12Down) {
            s_f12Down = true;
            UIAddLog("[Hotkey] F12 強制停止");
            ForceStopBot();
        } else if (pKbd->vkCode == VK_F12 && isUp) {
            s_f12Down = false;
        } else if (pKbd->vkCode == VK_F7 && isDown && !s_f7Down) {
            s_f7Down = true;
            CoordCalibrator& calib = CoordCalibrator::Instance();
            CalibIndex sel = calib.GetSelected();
            if (sel != CalibIndex::NONE) {
                calib.SetActive(true);  // 鎖定模式：滑鼠點擊遊戲視窗時設定
                const char* lbl = calib.GetLabel(sel);
                UIAddLog("[校正] F7 鎖定 %s，點擊遊戲內目標設定座標", lbl);
            }
        } else if (pKbd->vkCode == VK_F7 && isUp) {
            s_f7Down = false;
        } else if (pKbd->vkCode == VK_ESCAPE) {
            CoordCalibrator::Instance().SetActive(false);
        }
    }
    return CallNextHookEx(s_hKeyboardHook, nCode, wParam, lParam);
}

// ------------------------------------------------------------
// 滑鼠攔截（校正模式用）
// ------------------------------------------------------------
static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        CoordCalibrator& calib = CoordCalibrator::Instance();

        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN) {
            if (calib.IsActive()) {
                // WH_MOUSE_LL：lParam 是 MSLLHOOKSTRUCT*，非 LPARAM 本身
                MSLLHOOKSTRUCT* pMouse = (MSLLHOOKSTRUCT*)lParam;
                calib.OnScreenClick(pMouse->pt.x, pMouse->pt.y);
                calib.SetGameHwnd(s_gameHwndForCalib);
                return 1;
            }
        }
    }
    return CallNextHookEx(s_hMouseHook, nCode, wParam, lParam);
}

static DWORD WINAPI HotkeyThreadProc(LPVOID) {
    MSG msg;
    PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE); // 強制建立 message queue
    s_hotkeyThreadId = GetCurrentThreadId();

    s_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    s_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, NULL, 0);
    if (s_hHotkeyReadyEvent) {
        SetEvent(s_hHotkeyReadyEvent);
    }
    if (!s_hKeyboardHook) {
        s_hotkeyThreadId = 0;
        return 1;
    }

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (!s_hotkeysRunning) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (s_hMouseHook) {
        UnhookWindowsHookEx(s_hMouseHook);
        s_hMouseHook = NULL;
    }
    if (s_hKeyboardHook) {
        UnhookWindowsHookEx(s_hKeyboardHook);
        s_hKeyboardHook = NULL;
    }
    s_hotkeyThreadId = 0;
    return 0;
}

void SetGameHwndForCalib(HWND hwnd) {
    s_gameHwndForCalib = hwnd;
    CoordCalibrator::Instance().SetGameHwnd(hwnd);
}

void InitHotkeys() {
    if (s_hotkeysRunning) return;
    s_hotkeysRunning = true;
    s_hHotkeyReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    s_hHotkeyThread = CreateThread(NULL, 0, HotkeyThreadProc, NULL, 0, NULL);
    if (s_hHotkeyThread && s_hHotkeyReadyEvent) {
        WaitForSingleObject(s_hHotkeyReadyEvent, 3000);
    }
    if (s_hHotkeyReadyEvent) {
        CloseHandle(s_hHotkeyReadyEvent);
        s_hHotkeyReadyEvent = NULL;
    }
}

void ShutdownHotkeys() {
    s_hotkeysRunning = false;
    if (s_hHotkeyThread) {
        if (s_hotkeyThreadId != 0) {
            PostThreadMessage(s_hotkeyThreadId, WM_QUIT, 0, 0);
        }
        WaitForSingleObject(s_hHotkeyThread, 3000);
        CloseHandle(s_hHotkeyThread);
        s_hHotkeyThread = NULL;
    }
}

// ------------------------------------------------------------
// 小工具
// ------------------------------------------------------------
static void UiSeparatorGlow() {
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.20f, 0.36f, 0.66f, 0.85f));
    ImGui::Separator();
    ImGui::PopStyleColor();
}

static void UiSectionTitle(const char* title) {
    ImGui::Spacing();
    ImGui::TextColored(T.accent, "%s", title);
    UiSeparatorGlow();
}

static void UiRowText(const char* label, const char* value, const ImVec4& color = ImVec4(0.90f, 0.93f, 0.98f, 1.0f)) {
    ImGui::TextColored(T.textDim, "%s", label);
    ImGui::SameLine(110);
    ImGui::TextColored(color, "%s", value);
}

static void UiBeginPanel(const char* id, float height = 0.0f) {
    const float paddedHeight = (height > 0.0f) ? (height + 28.0f) : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, T.panel);
    ImGui::PushStyleColor(ImGuiCol_Border, T.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
    ImGui::BeginChild(id, ImVec2(0, paddedHeight), true);
}

static void UiEndPanel() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

static ImGuiTableFlags UiPageTableFlags() {
    return ImGuiTableFlags_SizingStretchSame |
           ImGuiTableFlags_NoSavedSettings |
           ImGuiTableFlags_BordersInnerV;
}

static void ApplyRightStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 0.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(8, 8);
    style.ItemInnerSpacing = ImVec2(6, 6);

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]         = T.bg0;
    c[ImGuiCol_ChildBg]          = T.panel;
    c[ImGuiCol_Border]           = T.border;
    c[ImGuiCol_Text]             = T.text;
    c[ImGuiCol_TextDisabled]     = T.textDim;
    c[ImGuiCol_FrameBg]          = ImVec4(0.07f, 0.09f, 0.14f, 1.0f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.09f, 0.12f, 0.18f, 1.0f);
    c[ImGuiCol_FrameBgActive]     = ImVec4(0.10f, 0.14f, 0.21f, 1.0f);
    c[ImGuiCol_Button]           = T.btn;
    c[ImGuiCol_ButtonHovered]    = T.btnHover;
    c[ImGuiCol_ButtonActive]    = T.btnActive;
    c[ImGuiCol_Tab]              = T.tab;
    c[ImGuiCol_TabHovered]       = T.tabHover;
    c[ImGuiCol_TabActive]        = T.tabActive;
    c[ImGuiCol_TabUnfocused]     = T.tab;
    c[ImGuiCol_TabUnfocusedActive]= T.tabActive;
    c[ImGuiCol_Separator]        = T.border;
    c[ImGuiCol_Header]           = T.tab;
    c[ImGuiCol_HeaderHovered]    = T.tabHover;
    c[ImGuiCol_HeaderActive]    = T.tabActive;
}

// ------------------------------------------------------------
// Helper: Slider with label
// ------------------------------------------------------------
static void UiSliderInt(const char* label, int* value, int min, int max, const char* format = "%d") {
    ImGui::TextColored(T.textDim, "%s", label);
    ImGui::SameLine(100);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.10f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, T.accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, T.accent2);
    char id[64];
    sprintf_s(id, "##%s", label);
    ImGui::SliderInt(id, value, min, max, format);
    ImGui::PopStyleColor(3);
}

static void UiSliderFloat(const char* label, float* value, float min, float max, const char* format = "%.0f") {
    ImGui::TextColored(T.textDim, "%s", label);
    ImGui::SameLine(100);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.10f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, T.accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, T.accent2);
    char id[64];
    sprintf_s(id, "##%s", label);
    ImGui::SliderFloat(id, value, min, max, format);
    ImGui::PopStyleColor(3);
}

static void UiCheckbox(const char* label, bool* value) {
    ImGui::PushStyleColor(ImGuiCol_CheckMark, T.accent2);
    ImGui::Checkbox(label, value);
    ImGui::PopStyleColor();
}

static void UiCombo(const char* label, int* value, const char* items[], int count) {
    ImGui::TextColored(T.textDim, "%s", label);
    ImGui::SameLine(100);
    ImGui::PushItemWidth(-1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.10f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, T.btn);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, T.btnHover);
    char id[64];
    sprintf_s(id, "##c_%s", label);
    ImGui::Combo(id, value, items, count);
    ImGui::PopStyleColor(3);
    ImGui::PopItemWidth();
}

// ------------------------------------------------------------
// 各頁面
// ------------------------------------------------------------
static void PageLicense() {
    TryAutoLoadLicense();
    bool licenseValid = IsLicenseValid();

    UiBeginPanel("LicenseStatusPanel", 160.0f);
    UiSectionTitle("[ 授權狀態 ]");
    UiRowText("狀態:", licenseValid ? "[ 已啟用 ]" : "[ 未啟用 ]",
        licenseValid ? T.green : T.red);
    UiRowText("剩餘:", s_licenseDays);
    if (licenseValid && strlen(s_licensePerms) > 0) {
        ImGui::TextColored(T.textDim, "權限:");
        ImGui::SameLine();
        ImGui::TextColored(T.accent, "%s", s_licensePerms);
    }
    UiEndPanel();

    ImGui::Spacing();

    UiBeginPanel("HwidPanel", 80.0f);
    UiSectionTitle("[ 機器碼 (一卡一機) ]");
    ImGui::TextColored(T.textDim, "HWID:");
    ImGui::SameLine();
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::TextColored(T.accent2, "%s", GetMachineHWID());
    ImGui::PopFont();
    ImGui::SameLine();
    if (ImGui::SmallButton("複製 HWID")) {
        ImGui::SetClipboardText(GetMachineHWID());
        UIAddLog("[License] HWID 已複製到剪貼簿");
    }
    ImGui::TextColored(T.textDim, "將此 HWID 提供給管理員以生成卡密");
    UiEndPanel();

    ImGui::Spacing();

    UiBeginPanel("LicenseInputPanel", 100.0f);
    UiSectionTitle("[ 卡密輸入 ]");
    ImGui::TextColored(T.textDim, "離線卡密:");
    ImGui::PushItemWidth(-1);
    bool enterPressed = ImGui::InputText("##license", s_licenseKey, sizeof(s_licenseKey),
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    UiEndPanel();

    ImGui::Spacing();

    UiBeginPanel("LicenseActionPanel", 116.0f);
    UiSectionTitle("[ 操作 ]");

    float w = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
    bool btnClicked = ImGui::Button("驗  證", ImVec2(w, 34.0f));
    if (btnClicked || enterPressed) {
        printf("[PageLicense] 驗證按鈕點擊！\n");
        StartLicenseVerifyAsync();
    }

    ImGui::SameLine();

    if (ImGui::Button("刷  新", ImVec2(w, 34.0f))) {
        printf("[PageLicense] 刷新按鈕點擊！\n");
        StartLicenseVerifyAsync();
    }

    if (s_licenseChecked && strlen(s_licenseMsg) > 0) {
        ImGui::Spacing();
        ImGui::TextColored(licenseValid ? T.green : T.red, "%s", s_licenseMsg);
    }

    UiEndPanel();
}

// ------------------------------------------------------------
// PageStatus - Bot狀態與玩家屬性
// ------------------------------------------------------------
static void PageStatus() {
    extern BotState GetBotState();
    extern PlayerState GetCachedPlayerState();
    extern bool HasCachedPlayerStateData();

    BotState st = GetBotState();
    const char* stStr = "未知";
    ImVec4 stColor = T.textDim;
    switch (st) {
        case BotState::IDLE:        stStr = "閒置";     stColor = T.textDim; break;
        case BotState::HUNTING:     stStr = "戰鬥中";   stColor = T.green; break;
        case BotState::DEAD:        stStr = "死亡";     stColor = T.red; break;
        case BotState::RETURNING:   stStr = "返回城鎮"; stColor = T.accent; break;
        case BotState::TOWN_SUPPLY: stStr = "城鎮補給"; stColor = T.accent2; break;
        case BotState::BACK_TO_FIELD: stStr = "返回野外"; stColor = ImVec4(0.90f, 0.70f, 0.30f, 1.0f); break;
        case BotState::TRAVELING:   stStr = "移動中";   stColor = ImVec4(0.60f, 0.80f, 0.90f, 1.0f); break;
        case BotState::PAUSED:      stStr = "已暫停";   stColor = ImVec4(0.78f, 0.62f, 0.95f, 1.0f); break;
    }

    extern BotConfig g_cfg;
    PlayerState ps = GetCachedPlayerState();
    bool hasPlayerData = HasCachedPlayerStateData();
    bool hasHp = ps.maxHp > 1;
    bool hasMp = ps.maxMp > 1;
    bool hasSp = ps.maxSp > 1;
    bool hasLevel = ps.level > 0;
    bool hasGold = ps.gold > 0;
    bool hasArrow = ps.arrowCount > 0;
    bool hasTalisman = ps.talismanCount > 0;
    bool hasExp = ps.exp > 0 || ps.expMax > 0;
    bool hasCoords = (ps.x != 0.0f || ps.z != 0.0f);
    bool hasMap = ps.mapId > 0;
    bool hasTarget = (ps.targetId != 0 && ps.targetId != 0xFFFFFFFF);
    bool hasCombatPower = ps.combatPower > 0;
    bool hasPhysAtk = ps.physAtkMin > 0;
    bool hasSprAtk = ps.sprAtkMin > 0;
    bool hasAttrs = (ps.str > 0 || ps.vit > 0);
    char buf[128];

    if (ImGui::BeginTable("StatusLayout", 2, UiPageTableFlags())) {
        ImGui::TableNextColumn();

        UiBeginPanel("BotStatusPanel", 126.0f);
        UiSectionTitle("[ Bot 狀態 ]");
        UiRowText("狀態:", stStr, stColor);

        bool authValid = IsLicenseValid();
        UiRowText("驗證:", authValid ? "[ 已啟用 ]" : "[ 未啟用 ]",
            authValid ? T.green : T.red);

        bool isActive = g_cfg.active.load();
        UiRowText("運行:", isActive ? "[ 運行中 ]" : "[ 已停止 ]",
            isActive ? T.green : T.red);
        UiEndPanel();

        UiBeginPanel("PlayerPanel", 190.0f);
        UiSectionTitle("[ 玩家屬性 ]");
        if (hasPlayerData) {
            if (hasHp) {
                sprintf_s(buf, "%d / %d", ps.hp, ps.maxHp);
                UiRowText("HP:", buf, (ps.hp < ps.maxHp * 0.3f) ? T.red : T.green);
            } else {
                UiRowText("HP:", "N/A", T.textDim);
            }
            if (hasMp) {
                sprintf_s(buf, "%d / %d", ps.mp, ps.maxMp);
                UiRowText("MP:", buf, (ps.mp < ps.maxMp * 0.3f) ? ImVec4(0.60f, 0.60f, 0.90f, 1.0f) : T.text);
            } else {
                UiRowText("MP:", "N/A", T.textDim);
            }
            if (hasSp) {
                sprintf_s(buf, "%d / %d", ps.sp, ps.maxSp);
                UiRowText("SP:", buf, (ps.sp < ps.maxSp * 0.3f) ? ImVec4(0.90f, 0.80f, 0.40f, 1.0f) : T.text);
            } else {
                UiRowText("SP:", "N/A", T.textDim);
            }
            if (hasLevel) {
                sprintf_s(buf, "%d", ps.level);
                UiRowText("等級:", buf);
            } else {
                UiRowText("等級:", "N/A", T.textDim);
            }
            if (hasGold) {
                sprintf_s(buf, "%lu", (unsigned long)ps.gold);
                UiRowText("金幣:", buf);
            } else {
                UiRowText("金幣:", "N/A", T.textDim);
            }
            if (hasArrow) {
                sprintf_s(buf, "%d", ps.arrowCount);
                UiRowText("箭矢:", buf);
            } else {
                UiRowText("箭矢:", "N/A", T.textDim);
            }
            if (hasTalisman) {
                sprintf_s(buf, "%d", ps.talismanCount);
                UiRowText("符咒:", buf);
            } else {
                UiRowText("符咒:", "N/A", T.textDim);
            }
            if (hasExp) {
                sprintf_s(buf, "%d / %d", ps.exp, ps.expMax);
                UiRowText("經驗:", buf);
            } else {
                UiRowText("經驗:", "N/A", T.textDim);
            }
        } else {
            UiRowText("HP:", "N/A", T.textDim);
            UiRowText("MP:", "N/A", T.textDim);
            UiRowText("SP:", "N/A", T.textDim);
            UiRowText("等級:", "N/A", T.textDim);
            UiRowText("金幣:", "N/A", T.textDim);
            UiRowText("箭矢:", "N/A", T.textDim);
            UiRowText("符咒:", "N/A", T.textDim);
            UiRowText("經驗:", "N/A", T.textDim);
        }
        UiEndPanel();

        ImGui::TableNextColumn();

        UiBeginPanel("CoordPanel", 136.0f);
        UiSectionTitle("[ 座標 / 地圖 ]");
        if (hasPlayerData) {
            if (hasCoords) {
                sprintf_s(buf, "(%.1f, %.1f)", ps.x, ps.z);
                UiRowText("世界座標:", buf);
                sprintf_s(buf, "%.1f", ps.y);
                UiRowText("高度(Y):", buf);
            } else {
                UiRowText("世界座標:", "N/A", T.textDim);
                UiRowText("高度(Y):", "N/A", T.textDim);
            }
            if (hasMap) {
                sprintf_s(buf, "%d", ps.mapId);
                UiRowText("地圖ID:", buf);
            } else {
                UiRowText("地圖ID:", "N/A", T.textDim);
            }
            if (hasTarget) {
                sprintf_s(buf, "%u", ps.targetId);
                UiRowText("目標ID:", buf);
            } else {
                UiRowText("目標ID:", "N/A", T.textDim);
            }
        } else {
            UiRowText("世界座標:", "N/A", T.textDim);
            UiRowText("高度(Y):", "N/A", T.textDim);
            UiRowText("地圖ID:", "N/A", T.textDim);
            UiRowText("目標ID:", "N/A", T.textDim);
        }
        UiEndPanel();

        UiBeginPanel("StatsPanel", 136.0f);
        UiSectionTitle("[ 戰鬥屬性 ]");
        if (hasPlayerData) {
            if (hasCombatPower) {
                sprintf_s(buf, "%d", ps.combatPower);
                UiRowText("戰鬥力:", buf);
            } else {
                UiRowText("戰鬥力:", "N/A", T.textDim);
            }
            if (hasPhysAtk) {
                sprintf_s(buf, "%d", ps.physAtkMin);
                UiRowText("物攻:", buf);
            } else {
                UiRowText("物攻:", "N/A", T.textDim);
            }
            if (hasSprAtk) {
                sprintf_s(buf, "%d", ps.sprAtkMin);
                UiRowText("精攻:", buf);
            } else {
                UiRowText("精攻:", "N/A", T.textDim);
            }
            if (hasAttrs) {
                sprintf_s(buf, "STR:%d VIT:%d", ps.str, ps.vit);
                UiRowText("屬性:", buf);
            } else {
                UiRowText("屬性:", "N/A", T.textDim);
            }
        } else {
            UiRowText("戰鬥力:", "N/A", T.textDim);
            UiRowText("物攻:", "N/A", T.textDim);
            UiRowText("精攻:", "N/A", T.textDim);
            UiRowText("屬性:", "N/A", T.textDim);
        }
        UiEndPanel();

        ImGui::EndTable();
    }
}

// ------------------------------------------------------------
// PageCombat - 戰鬥數據與鎖怪設定
// ------------------------------------------------------------
static void PageCombat() {
    extern int GetAttackCount(GameHandle* gh);
    extern int GetMonsterCount(GameHandle* gh);
    extern int GetCachedInvCount();
    extern DWORD GetKillCount();
    extern BotConfig g_cfg;
    extern bool IsEntityPoolWorking();
    extern bool IsAttackSenderConnected();
    extern bool IsRelativeOnlyCombatMode();

    GameHandle gh = GetGameHandle();

    char buf[64];
    char nameBuf[32];
    extern BotState GetBotState();
    extern int GetCurrentSkillIndex();
    extern int GetHuntPointIndex();
    extern int GetHuntPointCount();
    extern int GetCombatIntentState();
    extern void GetPlayerName(char* outName, int maxLen);

    if (ImGui::BeginTable("CombatLayout", 2, UiPageTableFlags())) {
        ImGui::TableNextColumn();

        UiBeginPanel("CombatDataPanel", 162.0f);
        UiSectionTitle("[ 戰鬥數據 ]");
        GetPlayerName(nameBuf, sizeof(nameBuf));
        if (nameBuf[0]) {
            UiRowText("角色:", nameBuf);
        } else {
            UiRowText("角色:", "N/A", T.textDim);
        }
        sprintf_s(buf, "%d", GetMonsterCount(&gh));     UiRowText("怪物:", buf);
        sprintf_s(buf, "%d", GetCachedInvCount());      UiRowText("背包:", buf);
        sprintf_s(buf, "%d", GetAttackCount(&gh));      UiRowText("攻擊:", buf);
        sprintf_s(buf, "%lu", GetKillCount());          UiRowText("擊殺:", buf);
        ImGui::Spacing();
        UiEndPanel();

        UiBeginPanel("LockTargetPanel", 236.0f);
        UiSectionTitle("[ 鎖怪設定 ]");
        ImGui::TextColored(T.accent, "攻擊模式:");
        ImGui::TextColored(T.textDim, "只使用固定掃打點，不再使用圓周半徑");
        ImGui::TextColored(T.textDim, "依序輪轉你指定的 8 個相對座標");
        ImGui::Spacing();
        UiRowText("圓心:", "(500, 370)");
        UiRowText("掃打點:", "8 個固定點");
        ImGui::Spacing();
        ImGui::TextColored(T.accent, "攻擊策略:");
        ImGui::TextColored(T.textDim, "1. 掃描範圍內最近怪物");
        ImGui::TextColored(T.textDim, "2. 技能施放後依序點擊固定座標");
        ImGui::TextColored(T.textDim, "3. 只保留你指定的 8 個點位");
        ImGui::Spacing();
        UiEndPanel();

        ImGui::TableNextColumn();

        UiBeginPanel("RealTimePanel", 166.0f);
        UiSectionTitle("[ 實時狀態 ]");
        BotState st = GetBotState();
        switch (st) {
            case BotState::IDLE:          sprintf_s(buf, "IDLE"); break;
            case BotState::HUNTING:       sprintf_s(buf, "HUNTING"); break;
            case BotState::DEAD:          sprintf_s(buf, "DEAD"); break;
            case BotState::RETURNING:     sprintf_s(buf, "RETURNING"); break;
            case BotState::TRAVELING:     sprintf_s(buf, "TRAVELING"); break;
            case BotState::TOWN_SUPPLY:   sprintf_s(buf, "TOWN_SUPPLY"); break;
            case BotState::BACK_TO_FIELD: sprintf_s(buf, "BACK_TO_FIELD"); break;
            case BotState::PAUSED:        sprintf_s(buf, "PAUSED"); break;
            default:                      sprintf_s(buf, "未知"); break;
        }
        UiRowText("狀態:", buf);
        sprintf_s(buf, "%d", GetCurrentSkillIndex() + 1);
        UiRowText("技能:", buf);
        sprintf_s(buf, "%d/%d", GetHuntPointIndex() + 1, GetHuntPointCount());
        UiRowText("煉功點:", buf);
        const char* intentNames[] = { "尋找中", "戰鬥中", "撿物中" };
        int intent = GetCombatIntentState();
        sprintf_s(buf, "%s", (intent >= 0 && intent < 3) ? intentNames[intent] : "未知");
        UiRowText("意圖:", buf);
        ImGui::Spacing();
        UiEndPanel();

        UiBeginPanel("SysStatusPanel", 158.0f);
        UiSectionTitle("[ 系統狀態 ]");
        UiRowText("實體池:", IsEntityPoolWorking() ? "正常" : "失效",
            IsEntityPoolWorking() ? T.green : T.red);
        UiRowText("封包:", IsAttackSenderConnected() ? "已連線" : "未連線",
            IsAttackSenderConnected() ? T.green : T.red);
        UiRowText("模式:", IsRelativeOnlyCombatMode() ? "純相對固定點" : "一般鎖怪",
            IsRelativeOnlyCombatMode() ? T.accent2 : T.green);
        bool authValid = IsLicenseValid();
        UiRowText("授權:", authValid ? "已驗證" : "未驗證",
            authValid ? T.green : T.red);
        ImGui::Spacing();
        UiEndPanel();

        ImGui::EndTable();
    }
}

// ------------------------------------------------------------
// PageConfig - 喝水/回城/技能設定
// ------------------------------------------------------------
static void PageCalibration();
static void PageConfig() {
    extern BotConfig g_cfg;

    int hpPct = g_cfg.hp_potion_pct.load();
    bool useVisualMode = g_cfg.use_visual_mode.load();
    int mpPct = g_cfg.mp_potion_pct.load();
    int spPct = g_cfg.sp_potion_pct.load();
    int hpReturn = g_cfg.hp_return_pct.load();
    bool invReturn = g_cfg.inventory_return.load();
    int invFullPct = g_cfg.inventory_full_pct.load();
    int attackRange = g_cfg.attack_range.load();
    int attackInterval = g_cfg.attack_interval_ms.load();
    int pickupRange = g_cfg.pickup_range.load();
    bool autoPickup = g_cfg.auto_pickup.load();
    int skillCount = g_cfg.attackSkillCount.load();
    int skillInterval = g_cfg.attackSkillInterval.load();
    bool autoSupport = g_cfg.auto_support.load();
    if (ImGui::BeginTable("ConfigLayout", 2, UiPageTableFlags())) {
        ImGui::TableNextColumn();

        UiBeginPanel("PotionPanel", 176.0f);
        UiSectionTitle("[ 自動喝水 ]");
        if (ImGui::CollapsingHeader("喝水設定", ImGuiTreeNodeFlags_DefaultOpen)) {
            UiSliderInt("HP < %%", &hpPct, 0, 100);
            UiSliderInt("MP < %%", &mpPct, 0, 100);
            UiSliderInt("SP < %%", &spPct, 0, 100);
            g_cfg.hp_potion_pct.store(hpPct);
            g_cfg.mp_potion_pct.store(mpPct);
            g_cfg.sp_potion_pct.store(spPct);
        }
        ImGui::Spacing();
        UiEndPanel();

        UiBeginPanel("CombatPanel2", 230.0f);
        UiSectionTitle("[ 戰鬥設定 ]");
        UiSliderInt("攻擊範圍 (格)", &attackRange, 1, 30);
        UiSliderInt("攻擊間隔 (ms)", &attackInterval, 50, 2000);
        UiCheckbox("自動撿物", &autoPickup);
        if (autoPickup) {
            UiSliderInt("撿物範圍 (格)", &pickupRange, 1, 10);
        }
        ImGui::Spacing();
        UiEndPanel();

        UiBeginPanel("VisionPanel", 270.0f);
        UiSectionTitle("[ 視覺模式 ]");
        // ── YOLO 模式 ──
        extern bool GetYoloMode();
        extern void SetYoloMode(bool enabled);
        extern float GetYoloConfidence();
        extern void SetYoloConfidence(float conf);
        bool useYolo = GetYoloMode();
        UiCheckbox("YOLO 模式", &useYolo);
        if (useYolo) {
            SetYoloMode(true);
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "  ONNX 模型視覺辨識（與視覺模式互斥）");
            ImGui::Spacing();
            ImGui::Indent();
            float conf = GetYoloConfidence();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.10f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.3f, 0.9f, 0.5f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.4f, 1.0f, 0.6f, 1.0f));
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##YoloConf", &conf, 0.0f, 1.0f, "信心度: %.2f")) {
                SetYoloConfidence(conf);
            }
            ImGui::PopStyleColor(3);
            ImGui::Unindent();
            // ── 寫入 INI（每幀檢查是否變更）──
            static bool s_yoloChanged = false;
            static bool s_lastYoloMode = false;
            static float s_lastConf = 0.0f;
            if (useYolo != s_lastYoloMode || fabs(conf - s_lastConf) > 0.01f) {
                s_lastYoloMode = useYolo;
                s_lastConf = conf;
                s_yoloChanged = true;
            }
            if (s_yoloChanged) {
                s_yoloChanged = false;
                // 延遲寫入，避免每幀都寫（每 2 秒寫一次）
                static DWORD s_lastWrite = 0;
                DWORD now = GetTickCount();
                if (now - s_lastWrite > 2000) {
                    s_lastWrite = now;
                    OffsetConfig::SaveYoloSettings(
                        g_cfg.use_yolo_mode.load(),
                        g_cfg.yolo_confidence.load(),
                        g_cfg.yolo_nms_threshold.load()
                    );
                }
            }
        } else {
            g_cfg.use_yolo_mode.store(false);
        }
        ImGui::Spacing();
        ImGui::Separator();
        // ── 像素視覺模式 ──
        UiCheckbox("視覺模式 (Vision)", &useVisualMode);
        if (useVisualMode) {
            g_cfg.use_visual_mode.store(true);
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "  像素掃描血條辨識");
        } else {
            // 如果沒有手動開啟且 YOLO 也關閉，才關閉視覺模式
            if (!GetYoloMode()) {
                g_cfg.use_visual_mode.store(false);
            }
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  記憶體讀取為主");
        }
        ImGui::Spacing();
        UiEndPanel();

        ImGui::TableNextColumn();

        UiBeginPanel("ReturnPanel", 170.0f);
        UiSectionTitle("[ 回城設定 ]");
        UiSliderInt("HP < %% 回城", &hpReturn, 0, 100);
        UiCheckbox("背包滿時回城", &invReturn);
        g_cfg.hp_return_pct.store(hpReturn);
        g_cfg.inventory_return.store(invReturn);
        if (invReturn) {
            UiSliderInt("背包 > %% 滿", &invFullPct, 50, 100);
            g_cfg.inventory_full_pct.store(invFullPct);
        }
        ImGui::Spacing();
        UiEndPanel();

        UiBeginPanel("GameTimePanel", 210.0f);
        UiSectionTitle("[ 遊戲時間自動 ]");
        bool autoGameTime = g_cfg.auto_game_time.load();
        UiCheckbox("啟用遊戲時間", &autoGameTime);
        g_cfg.auto_game_time.store(autoGameTime);
        if (autoGameTime) {
            int returnHour = g_cfg.game_time_hour.load();
            int returnMin = g_cfg.game_time_min.load();
            ImGui::TextColored(T.text, "回城時間:");
            ImGui::SameLine();
            ImGui::PushItemWidth(60);
            ImGui::PushID("return_hour");
            ImGui::SliderInt("##H", &returnHour, 0, 23);
            ImGui::PopID();
            ImGui::SameLine();
            ImGui::Text(":");
            ImGui::SameLine();
            ImGui::PushID("return_min");
            ImGui::SliderInt("##M", &returnMin, 0, 59);
            ImGui::PopID();
            ImGui::PopItemWidth();
            g_cfg.game_time_hour.store(returnHour);
            g_cfg.game_time_min.store(returnMin);

            bool autoReturnToField = g_cfg.auto_return_to_field.load();
            UiCheckbox("自動返回野外", &autoReturnToField);
            g_cfg.auto_return_to_field.store(autoReturnToField);
            if (autoReturnToField) {
                int fieldHour = g_cfg.game_time_return_hour.load();
                int fieldMin = g_cfg.game_time_return_min.load();
                ImGui::TextColored(T.text, "返回野外:");
                ImGui::SameLine();
                ImGui::PushItemWidth(60);
                ImGui::PushID("field_hour");
                ImGui::SliderInt("##H2", &fieldHour, 0, 23);
                ImGui::PopID();
                ImGui::SameLine();
                ImGui::Text(":");
                ImGui::SameLine();
                ImGui::PushID("field_min");
                ImGui::SliderInt("##M2", &fieldMin, 0, 59);
                ImGui::PopID();
                ImGui::PopItemWidth();
                g_cfg.game_time_return_hour.store(fieldHour);
                g_cfg.game_time_return_min.store(fieldMin);
            }
            ImGui::TextColored(T.textDim, "例: 06:30 回城, 08:00 返回");
        }
        UiEndPanel();

        UiBeginPanel("SkillPanel", 216.0f);
        UiSectionTitle("[ 技能設定 ]");
        UiSliderInt("攻擊技能數", &skillCount, 1, 10);
        UiSliderInt("技能間隔 (ms)", &skillInterval, 10, 500);
        UiCheckbox("自動施放輔助", &autoSupport);
        g_cfg.attackSkillCount.store(skillCount);
        g_cfg.attackSkillInterval.store(skillInterval);
        g_cfg.auto_support.store(autoSupport);
        if (autoSupport) {
            int buffCount = g_cfg.buffSkillCount.load();
            int buffInterval = g_cfg.buffCastInterval.load();
            UiSliderInt("輔助技能數", &buffCount, 1, 5);
            UiSliderInt("輔助間隔 (秒)", &buffInterval, 5, 120);
            g_cfg.buffSkillCount.store(buffCount);
            g_cfg.buffCastInterval.store(buffInterval);
        }
        UiEndPanel();

        ImGui::EndTable();

        // ── 敵人接近偵測 ──
        UiBeginPanel("ApproachPanel", 260.0f);
        UiSectionTitle("[ 敵人接近偵測 ]");
        bool enemyApproachDetect = g_cfg.enemy_approach_detect.load();
        UiCheckbox("啟用敵人接近偵測", &enemyApproachDetect);
        g_cfg.enemy_approach_detect.store(enemyApproachDetect);
        if (enemyApproachDetect) {
            float approachSpeed = g_cfg.enemy_approach_speed.load();
            float approachDist = g_cfg.enemy_approach_dist.load();
            int approachTrigger = g_cfg.enemy_approach_trigger.load();
            UiSliderFloat("速度閾值", &approachSpeed, 20.0f, 100.0f);
            UiSliderFloat("偵測距離 (格)", &approachDist, 10.0f, 50.0f);
            ImGui::TextColored(T.textDim, "觸發動作:");
            ImGui::SameLine();
            ImGui::RadioButton("回城", &approachTrigger, 0);
            ImGui::SameLine();
            ImGui::RadioButton("暫停", &approachTrigger, 1);
            g_cfg.enemy_approach_speed.store(approachSpeed);
            g_cfg.enemy_approach_dist.store(approachDist);
            g_cfg.enemy_approach_trigger.store(approachTrigger);
        }
        UiEndPanel();
    }
}

// ------------------------------------------------------------
// PageSupply - 補給/復活設定
// ------------------------------------------------------------
static void PageSupply() {
    extern BotConfig g_cfg;

    bool autoRevive = g_cfg.auto_revive.load();
    int reviveMode = g_cfg.revive_mode.load();
    int reviveDelay = g_cfg.revive_delay_ms.load();
    bool autoSupply = g_cfg.auto_supply.load();
    bool autoSell = g_cfg.auto_sell.load();
    bool autoBuy = g_cfg.auto_buy.load();
    bool potionCheck = g_cfg.potion_check_enable.load();
    int buyHp = g_cfg.buy_hp_qty.load();
    int buyMp = g_cfg.buy_mp_qty.load();
    int buySp = g_cfg.buy_sp_qty.load();
    int buyArrow = g_cfg.buy_arrow_qty.load();
    int buyCharm = g_cfg.buy_charm_qty.load();
    int townIdx = g_cfg.town_index.load();
    int teleportDelay = g_cfg.teleport_delay_ms.load();
    if (ImGui::BeginTable("SupplyLayout", 2, UiPageTableFlags())) {
        ImGui::TableNextColumn();

        UiBeginPanel("RevivePanel", 178.0f);
        UiSectionTitle("[ 自動復活 ]");
        UiCheckbox("自動復活", &autoRevive);
        g_cfg.auto_revive.store(autoRevive);
        if (autoRevive) {
            const char* reviveItems[] = { "歸魂珠優先", "原地復活", "基本復活" };
            UiCombo("復活方式", &reviveMode, reviveItems, 3);
            g_cfg.revive_mode.store(reviveMode);
            UiSliderInt("復活延遲 (ms)", &reviveDelay, 500, 5000);
            g_cfg.revive_delay_ms.store(reviveDelay);
        }
        ImGui::Spacing();
        UiEndPanel();

        UiBeginPanel("BuyPanel", 206.0f);
        UiSectionTitle("[ 購買數量 ]");
        UiSliderInt("HP藥水", &buyHp, 0, 999);
        UiSliderInt("MP藥水", &buyMp, 0, 999);
        UiSliderInt("SP藥水", &buySp, 0, 999);
        g_cfg.buy_hp_qty.store(buyHp);
        g_cfg.buy_mp_qty.store(buyMp);
        g_cfg.buy_sp_qty.store(buySp);
        UiSliderInt("箭矢", &buyArrow, 0, 999);
        UiSliderInt("符咒", &buyCharm, 0, 999);
        g_cfg.buy_arrow_qty.store(buyArrow);
        g_cfg.buy_charm_qty.store(buyCharm);
        UiEndPanel();

        ImGui::TableNextColumn();

        UiBeginPanel("SupplyPanel2", 246.0f);
        UiSectionTitle("[ 自動補給 ]");
        UiCheckbox("啟用自動補給", &autoSupply);
        g_cfg.auto_supply.store(autoSupply);
        if (autoSupply) {
            UiCheckbox("自動賣垃圾", &autoSell);
            UiCheckbox("自動買藥水", &autoBuy);
            UiCheckbox("藥水不足檢測", &potionCheck);
            g_cfg.auto_sell.store(autoSell);
            g_cfg.auto_buy.store(autoBuy);
            g_cfg.potion_check_enable.store(potionCheck);
            if (potionCheck) {
                int potionStart = g_cfg.potion_slot_start.load();
                int potionEnd = g_cfg.potion_slot_end.load();
                int minSlots = g_cfg.min_potion_slots.load();
                UiSliderInt("藥水格起始", &potionStart, 0, 77);
                UiSliderInt("藥水格結束", &potionEnd, 0, 77);
                UiSliderInt("最低藥水格", &minSlots, 1, 10);
                g_cfg.potion_slot_start.store(potionStart);
                g_cfg.potion_slot_end.store(potionEnd);
                g_cfg.min_potion_slots.store(minSlots);
            }
        }
        ImGui::Spacing();
        UiEndPanel();

        UiBeginPanel("TownPanel", 142.0f);
        UiSectionTitle("[ 城鎮選擇 ]");
        const char* towns[] = { "聖門", "商洞", "玄巖", "鳳凰" };
        UiCombo("返回城鎮", &townIdx, towns, 4);
        g_cfg.town_index.store(townIdx);
        UiSliderInt("傳送延遲 (ms)", &teleportDelay, 1000, 10000);
        g_cfg.teleport_delay_ms.store(teleportDelay);
        UiEndPanel();

        ImGui::EndTable();
    }
}

// ------------------------------------------------------------
// PageProtect - 物品/列保護
// ------------------------------------------------------------
static void PageProtect() {
    extern BotConfig g_cfg;
    extern int GetProtectedItemCount();
    extern int GetProtectedItemId(int index);
    extern int GetProtectedRowCount();
    extern bool IsRowProtected(int row);
    extern void SetRowProtected(int row, bool protect);

    if (ImGui::BeginTable("ProtectLayout", 2, UiPageTableFlags())) {
        ImGui::TableNextColumn();

        UiBeginPanel("RowProtectPanel", 190.0f);
        UiSectionTitle("[ 列保護 ]");

        int protectedRows = GetProtectedRowCount();
        ImGui::TextColored(T.text, "已保護: %d / 13 列", protectedRows);

        for (int row = 0; row < 13; row++) {
            bool isProtected = IsRowProtected(row);
            char label[16];
            _itoa_s(row + 1, label, 16, 10);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
            ImGui::PushID(row);
            if (ImGui::Checkbox(label, &isProtected)) {
                SetRowProtected(row, isProtected);
            }
            ImGui::PopID();
            ImGui::PopStyleVar();
            if (row % 2 == 0 && row < 12) ImGui::SameLine();
        }

        ImGui::Spacing();
        UiEndPanel();

        UiBeginPanel("ProtectTipPanel", 136.0f);
        UiSectionTitle("[ 使用說明 ]");
        ImGui::TextColored(T.textDim, "1. 列保護: 勾選該列不自動出售");
        ImGui::TextColored(T.textDim, "2. 物品保護: 需掃描背包取得ID");
        ImGui::TextColored(T.textDim, "3. 建議: 藥水列設為保護");
        UiEndPanel();

        ImGui::TableNextColumn();

        UiBeginPanel("ItemProtectPanel", 270.0f);
        UiSectionTitle("[ 物品保護 ]");

        int protectedCount = GetProtectedItemCount();
        ImGui::TextColored(T.text, "已保護: %d 個物品", protectedCount);
        if (protectedCount > 0) {
            int showCount = protectedCount > 20 ? 20 : protectedCount;
            for (int i = 0; i < showCount; i++) {
                int itemId = GetProtectedItemId(i);
                ImGui::BulletText("ID: %d", itemId);
            }
            if (protectedCount > 20) {
                ImGui::TextColored(T.textDim, "... 共 %d 個物品", protectedCount);
            }
        } else {
            ImGui::TextColored(T.textDim, "尚無保護物品");
        }

        ImGui::Spacing();
        UiEndPanel();

        ImGui::EndTable();
    }
}

static void PageControl() {
    extern BotConfig g_cfg;
    UiBeginPanel("ControlPanel", 208.0f);
    UiSectionTitle("[ 控制 ]");

    bool isActive = g_cfg.active.load();
    float w = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;

    ImGui::PushStyleColor(ImGuiCol_Button, isActive ? ImVec4(0.72f, 0.34f, 0.18f, 1.0f) : ImVec4(0.16f, 0.54f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isActive ? ImVec4(0.85f, 0.42f, 0.22f, 1.0f) : ImVec4(0.20f, 0.66f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, isActive ? ImVec4(0.92f, 0.48f, 0.25f, 1.0f) : ImVec4(0.24f, 0.74f, 0.34f, 1.0f));
    if (ImGui::Button(isActive ? "暫  停" : "開  始", ImVec2(w, 36.0f))) {
        UIAddLog("[UI] 控制頁按下 %s", isActive ? "暫停" : "開始");
        ToggleBotActive();
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.86f, 0.24f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.92f, 0.30f, 0.30f, 1.0f));
    if (ImGui::Button("停  止", ImVec2(w, 36.0f))) {
        UIAddLog("[UI] 控制頁按下停止");
        ForceStopBot();
    }
    ImGui::PopStyleColor(3);

    ImGui::Spacing();
    UiSectionTitle("[ 快捷鍵 ]");
    UiRowText("F10", "顯示 / 隱藏");
    UiRowText("F11", "開始 / 暫停");
    UiRowText("F12", "強制停止");

    UiEndPanel();
}

// ------------------------------------------------------------
// PageCalibration - 座標校正工具
// ------------------------------------------------------------
static void PageCalibration() {
    CoordCalibrator& calib = CoordCalibrator::Instance();

    // 說明面板
    {
        UiBeginPanel("CalibIntro", 0.0f);
        ImGui::TextColored(T.accent2, "座標校正工具");
        ImGui::TextWrapped(
            "本工具用於微調 RAN2 客戶端的相對座標。\n"
            "座標以 0-1000 相對值儲存，適用於 Win7 / Win10 / Win11。\n\n"
            "■ 可校正的項目：\n"
            "  · 復活（歸魂珠 / 原地 / 基本）\n"
            "  · 各城鎮 NPC 對話框與購買位置（聖門 / 商洞 / 玄巖 / 鳳凰）\n"
            "  · 寵物餵食（飼料 / 符咒）\n"
            "  · 中心點（煉功點圓心）+ 8 個攻擊定點\n\n"
            "■ 使用方式：\n"
            "  1. 選取要校正的項目\n"
            "  2. 在遊戲內將角色移到目標位置，按 F7 或點擊「套用」\n"
            "  3. 設定完成後直接生效，無需重啟\n\n"
            "■ 注意：\n"
            "  攻擊定點預設值已含 Win7 偏移 (+20,+20)。\n"
            "  只有在定點明顯跑掉時才需個別微調，否則請保持預設。");
        UiEndPanel();
        ImGui::Spacing();
    }

    // 操作提示
    if (!calib.IsActive()) {
        UiBeginPanel("CalibHint", 0.0f);
        ImGui::TextColored(T.accent2, "校正流程");
        ImGui::TextColored(T.textDim,
            "1. 從下方列表選擇要校正的項目\n"
            "2. 按 F7 鎖定該項目\n"
            "3. 在遊戲內將角色移到目標位置，點擊即可設定\n"
            "（座標自動轉為相對值 0-1000）");
        UiEndPanel();
        ImGui::Spacing();
    }

    // 下拉選單選擇要校正的項目
    UiBeginPanel("CalibSelect", 0.0f);
    UiSectionTitle("[ 選擇校正項目 ]");

    static int s_selectedCalibIdx = 0;
    static char s_inputX[32] = "";
    static char s_inputZ[32] = "";

    CalibIndex sel = calib.GetSelected();
    s_selectedCalibIdx = (int)sel;

    // 用 ListBox 取代 Combo（更穩定可見）
    ImGui::PushItemWidth(-1);
    if (ImGui::BeginListBox("##CalibListBox", ImVec2(-1, 200))) {
        for (int i = 0; i < (int)CalibIndex::COUNT; i++) {
            CalibIndex idx = (CalibIndex)i;
            bool isSelected = (s_selectedCalibIdx == i);
            char line[64];
            snprintf(line, sizeof(line), "%s%s",
                calib.GetLabel(idx),
                calib.IsCalibrated(idx) ? " *" : "");
            if (ImGui::Selectable(line, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                s_selectedCalibIdx = i;
                calib.SetSelected(idx);
            }
        }
        ImGui::EndListBox();
    }
    ImGui::PopItemWidth();

    CalibIndex curIdx = (CalibIndex)s_selectedCalibIdx;
    int curX = calib.GetX(curIdx);
    int curZ = calib.GetZ(curIdx);

    ImGui::Spacing();
    UiSectionTitle("[ 目前座標 ]");
    char buf[64];
    sprintf(buf, "%d", curX);
    ImGui::TextColored(T.text, "X:"); ImGui::SameLine();
    ImGui::TextColored(T.accent2, buf);
    sprintf(buf, "%d", curZ);
    ImGui::TextColored(T.text, "Z:"); ImGui::SameLine();
    ImGui::TextColored(T.accent2, buf);

    ImGui::Spacing();
    UiSectionTitle("[ 手動輸入座標 ]");

    ImGui::PushItemWidth(120);
    ImGui::TextColored(T.text, "X:"); ImGui::SameLine();
    ImGui::InputText("##CalibX", s_inputX, sizeof(s_inputX), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    ImGui::TextColored(T.text, "  Z:"); ImGui::SameLine();
    ImGui::InputText("##CalibZ", s_inputZ, sizeof(s_inputZ), ImGuiInputTextFlags_CharsDecimal);
    ImGui::PopItemWidth();

    if (ImGui::Button("套用座標", ImVec2(120, 28))) {
        int nx = atoi(s_inputX);
        int nz = atoi(s_inputZ);
        if (nx >= 0 && nx <= 1000 && nz >= 0 && nz <= 1000) {
            calib.Set(curIdx, nx, nz);
            UIAddLog("[校正] %s -> (%d, %d)", calib.GetLabel(curIdx), nx, nz);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("重置", ImVec2(80, 28))) {
        calib.Reset(curIdx);
        UIAddLog("[校正] %s 已重置為預設值", calib.GetLabel(curIdx));
    }
    if (ImGui::Button("重置全部", ImVec2(-1, 28))) {
        calib.ResetAll();
        UIAddLog("[校正] 全部座標已重置為預設值");
    }

    UiEndPanel();
}

// ------------------------------------------------------------
// 主 GUI
// ------------------------------------------------------------
void RenderMainGUI() {
    if (!g_guiVisible) return;

    // ── 校正 Overlay（置於右上角，不遮擋主內容）──
    {
        CoordCalibrator& calib = CoordCalibrator::Instance();
        if (calib.IsActive()) {
            ImGuiViewport* vp = ImGui::GetMainViewport();
            ImVec2 ovSize(320.0f, 0.0f);
            // 右上角
            ImVec2 ovPos(vp->Pos.x + vp->Size.x - ovSize.x - 8.0f, vp->Pos.y + 8.0f);
            ImGui::SetNextWindowPos(ovPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ovSize, ImGuiCond_Always);

            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.08f, 0.15f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.9f, 0.5f, 0.1f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);

            char overlayTitle[64];
            snprintf(overlayTitle, sizeof(overlayTitle),
                "F7 校正模式 ###CalibOverlay");

            if (ImGui::Begin(overlayTitle,
                NULL,
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings)) {

                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "[F7 鎖定中]");
                ImGui::SameLine();
                CalibIndex locked = calib.GetSelected();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                    "%s", calib.GetLabel(locked));
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                    "在遊戲內點擊目標位置即可設定");
                ImGui::Separator();

                CalibIndex sel = calib.GetSelected();
                for (int i = 0; i < (int)CalibIndex::COUNT; i++) {
                    CalibIndex idx = (CalibIndex)i;
                    bool isSel = (idx == sel);
                    ImGuiSelectableFlags flags = ImGuiSelectableFlags_SpanAllColumns;
                    if (ImGui::Selectable(calib.GetLabel(idx), isSel, flags)) {
                        calib.SetSelected(idx);
                    }
                    if (isSel) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), " <-- F7鎖定");
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                            "(%d, %d)", calib.GetX(idx), calib.GetZ(idx));
                    }
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                    "ESC / 再按 F7 離開校正模式");
            }
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }
    }

    ApplyRightStyle();

    ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->Size, ImGuiCond_Always);
    // SetNextWindowViewport 需要新版 ImGui，舊版移除此行

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    if (!ImGui::Begin("JyTrainerRoot", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus))
    {
        ImGui::End();
        ImGui::PopStyleVar(3);
        return;
    }

    const float headerH = 48.0f;
    // Header
    ImGui::PushStyleColor(ImGuiCol_ChildBg, T.bg1);
    ImGui::BeginChild("##header", ImVec2(0, headerH), false);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetWindowPos();
        ImVec2 p1 = ImVec2(p0.x + ImGui::GetWindowSize().x, p0.y + ImGui::GetWindowSize().y);

        dl->AddRectFilledMultiColor(
            p0, p1,
            IM_COL32(8, 12, 24, 255),
            IM_COL32(10, 18, 36, 255),
            IM_COL32(8, 12, 24, 255),
            IM_COL32(6, 10, 18, 255));

        dl->AddLine(ImVec2(p0.x, p1.y - 1), ImVec2(p1.x, p1.y - 1), IM_COL32(55, 110, 200, 120), 1.0f);

        ImGui::SetCursorPos(ImVec2(12, 12));
        ImGui::TextColored(T.accent2, "JyTrainer");
        ImGui::SameLine();
        ImGui::TextColored(T.textDim, "- RAN2 功能");

        // 右側按鈕 - 使用絕對座標
        float winW = ImGui::GetWindowWidth();
        float btnW = 30.0f;
        float btnH = 24.0f;
        float btnY = 12.0f;
        float gap = 4.0f;

        // 最小化按鈕
        ImGui::SetCursorPos(ImVec2(winW - btnW * 2 - gap - 12, btnY));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.24f, 0.36f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.34f, 0.50f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.32f, 0.42f, 0.62f, 1.0f));
        if (ImGui::Button("-##minimize", ImVec2(btnW, btnH))) {
            ShowWindow(g_hWnd, SW_MINIMIZE);
        }
        ImGui::PopStyleColor(3);

        // 關閉按鈕
        ImGui::SetCursorPos(ImVec2(winW - btnW - 12, btnY));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.95f, 0.25f, 0.25f, 1.0f));
        if (ImGui::Button("X##close", ImVec2(btnW, btnH))) {
            extern volatile bool g_Running;
            g_guiVisible = false;
            g_Running = false;
            UIAddLog("[系統] UI 關閉按鈕觸發，準備結束程式");
            ShowWindow(g_hWnd, SW_HIDE);
            PostQuitMessage(0);
        }
        ImGui::PopStyleColor(3);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // 單一頁面顯示所有內容（無 Tab）
    ImGui::PushStyleColor(ImGuiCol_ChildBg, T.bg0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
    ImGui::BeginChild("##content_area", ImVec2(0, -50), false);
    {
        PageLicense();
        PageStatus();
        PageCombat();
        PageConfig();
        PageSupply();
        PageProtect();
        PageControl();
        PageCalibration();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // Footer
    ImGui::PushStyleColor(ImGuiCol_ChildBg, T.bg1);
    ImGui::BeginChild("##footer", ImVec2(0, 0), false);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetWindowPos();
        ImVec2 p1 = ImVec2(p0.x + ImGui::GetWindowSize().x, p0.y + ImGui::GetWindowSize().y);
        dl->AddLine(ImVec2(p0.x, p0.y + 1), ImVec2(p1.x, p0.y + 1), IM_COL32(55, 110, 200, 100), 1.0f);
        ImGui::SetCursorPos(ImVec2(12, 6));
        ImGui::TextColored(T.textDim, "F10=隱藏   F11=切換   F12=停止");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar(3);
}
