/**
 * @file gui_ranbot.cpp
 * @brief JyTrainer GUI - 现代化三區塊佈局
 *
 * 佈局結構:
 * ┌─────────────────────────────────────────────────────────┐
 * │  [狀態列] 授權狀態 | Bot狀態 | 系統狀態 | [控制按鈕]     │
 * ├────────┬──────────────────────────────────────────────┤
 * │ [導航] │  [主內容區 - 卡片式佈局]                      │
 * │        │                                              │
 * │ 首頁   │  ┌──────────────┐  ┌──────────────┐          │
 * │ 戰鬥   │  │  卡片 1      │  │  卡片 2      │          │
 * │ 補給   │  └──────────────┘  └──────────────┘          │
 * │ 保護   │  ┌──────────────┐  ┌──────────────┐          │
 * │ 校正   │  │  卡片 3      │  │  卡片 4      │          │
 * │        │  └──────────────┘  └──────────────┘          │
 * └────────┴──────────────────────────────────────────────┘
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include "gui_ranbot.h"
#include "../core/bot_logic.h"
#include "../config/offset_config.h"
#include "../config/config_updater.h"
#include "../license/offline_license.h"
#include "../game/game_process.h"
#include "../input/attack_packet.h"
#include "../input/input_sender.h"
#include "../config/coords.h"
#include "../platform/coord_calib.h"
#include "../../imgui/imgui.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

// ============================================================================
// EXTERNAL GLOBALS
// ============================================================================
extern bool g_guiVisible;
extern HWND g_hWnd;
void UIAddLog(const char* fmt, ...);
void ToggleBotActive();
void ForceStopBot();

// ============================================================================
// THEME - 暗色系專業主題
// ============================================================================
struct JyTheme {
    // 背景層次
    ImVec4 bg_main      = ImVec4(0.08f, 0.08f, 0.12f, 1.0f);   // 主背景
    ImVec4 bg_sidebar   = ImVec4(0.06f, 0.06f, 0.10f, 1.0f);   // 側邊欄
    ImVec4 bg_card      = ImVec4(0.10f, 0.10f, 0.15f, 1.0f);   // 卡片背景
    ImVec4 bg_input     = ImVec4(0.12f, 0.12f, 0.18f, 1.0f);   // 輸入框背景

    // 邊框
    ImVec4 border       = ImVec4(0.20f, 0.22f, 0.30f, 1.0f);   // 普通邊框
    ImVec4 border_focus = ImVec4(0.30f, 0.50f, 0.90f, 1.0f);   // 聚焦邊框

    // 主要色彩
    ImVec4 accent       = ImVec4(0.30f, 0.60f, 1.00f, 1.0f);   // 主色（藍）
    ImVec4 accent_hover = ImVec4(0.40f, 0.70f, 1.00f, 1.0f);
    ImVec4 accent_dim   = ImVec4(0.20f, 0.40f, 0.70f, 1.0f);

    // 功能色彩
    ImVec4 success      = ImVec4(0.20f, 0.80f, 0.40f, 1.0f);   // 成功/在線
    ImVec4 warning     = ImVec4(1.00f, 0.75f, 0.20f, 1.0f);   // 警告
    ImVec4 danger      = ImVec4(0.95f, 0.30f, 0.30f, 1.0f);   // 危險/錯誤
    ImVec4 info        = ImVec4(0.30f, 0.75f, 0.90f, 1.0f);   // 信息

    // 文字
    ImVec4 text_primary = ImVec4(0.92f, 0.92f, 0.98f, 1.0f);   // 主文字
    ImVec4 text_secondary = ImVec4(0.60f, 0.62f, 0.70f, 1.0f);// 次文字
    ImVec4 text_muted   = ImVec4(0.40f, 0.42f, 0.50f, 1.0f);  // 暗淡文字

    // 按鈕
    ImVec4 btn_normal   = ImVec4(0.15f, 0.18f, 0.25f, 1.0f);
    ImVec4 btn_hover   = ImVec4(0.20f, 0.25f, 0.35f, 1.0f);
    ImVec4 btn_active   = ImVec4(0.25f, 0.32f, 0.45f, 1.0f);

    // 導航
    ImVec4 nav_active  = ImVec4(0.20f, 0.40f, 0.80f, 1.0f);
    ImVec4 nav_hover    = ImVec4(0.15f, 0.18f, 0.28f, 1.0f);
    ImVec4 nav_normal   = ImVec4(0.08f, 0.08f, 0.12f, 1.0f);
};
static JyTheme T;

// ============================================================================
// LOGGING SYSTEM
// ============================================================================
static char s_logBuf[64][512];
static int s_logHead = 0;
static int s_logCount = 0;
static CRITICAL_SECTION s_logCs;
static volatile bool s_logReady = false;
static volatile LONG s_logCsInit = 0;

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

// ============================================================================
// LICENSE MANAGEMENT
// ============================================================================
static char s_licenseKey[4096] = "";
static char s_licenseDays[32] = "---";
static char s_licenseMsg[256] = "未驗證";
static char s_licensePerms[512] = "";
static bool s_licenseChecked = false;
static bool s_autoLoadTried = false;

static void NormalizeLicenseKey(char* key) {
    if (!key) return;
    char tmp[4096] = {0};
    size_t out = 0;
    for (size_t i = 0; key[i] && out < sizeof(tmp) - 1; i++) {
        unsigned char ch = (unsigned char)key[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
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
        if (info.days_left >= 0) sprintf_s(s_licenseDays, "%d 天", info.days_left);
        else strcpy_s(s_licenseDays, "未知");
        strcpy_s(s_licenseMsg, "驗證成功");
        SetLicenseValid(true);
        OfflineLicenseSaveCached(token);

        if (!info.permissions.empty()) {
            char permBuf[512] = "";
            for (size_t i = 0; i < info.permissions.size(); i++) {
                if (i > 0) strcat_s(permBuf, ", ");
                strcat_s(permBuf, info.permissions[i].c_str());
            }
            strcpy_s(s_licensePerms, permBuf);
        } else {
            strcpy_s(s_licensePerms, "無特殊權限");
        }
    } else {
        strcpy_s(s_licenseDays, "---");
        strcpy_s(s_licensePerms, "");
        strcpy_s(s_licenseMsg, info.message.empty() ? "驗證失敗" : info.message.c_str());
        SetLicenseValid(false);
    }
    s_licenseChecked = true;
    delete[] token;
    return 0;
}

static void StartLicenseVerifyAsync() {
    NormalizeLicenseKey(s_licenseKey);
    if (strlen(s_licenseKey) == 0) {
        strcpy_s(s_licenseMsg, "請輸入卡密");
        SetLicenseValid(false);
        s_licenseChecked = true;
        return;
    }
    if (!OfflineLicenseLooksLikeToken(s_licenseKey)) {
        strcpy_s(s_licenseMsg, "卡密格式無效");
        SetLicenseValid(false);
        s_licenseChecked = true;
        return;
    }

    strcpy_s(s_licenseMsg, "驗證中...");
    s_licenseChecked = false;

    char* tokenCopy = new char[strlen(s_licenseKey) + 1];
    strcpy_s(tokenCopy, strlen(s_licenseKey) + 1, s_licenseKey);

    HANDLE hThread = CreateThread(NULL, 0, LicenseVerifyThreadProc, tokenCopy, 0, NULL);
    if (hThread) CloseHandle(hThread);
}

static void TryAutoLoadLicense() {
    if (s_autoLoadTried) return;
    s_autoLoadTried = true;
    if (OfflineLicenseLoadCached(s_licenseKey, sizeof(s_licenseKey))) {
        StartLicenseVerifyAsync();
    }
}

// ============================================================================
// HOTKEY SYSTEM
// ============================================================================
static HHOOK s_hKeyboardHook = NULL;
static HANDLE s_hHotkeyThread = NULL;
static HANDLE s_hHotkeyReadyEvent = NULL;
static DWORD s_hotkeyThreadId = 0;
static volatile bool s_hotkeysRunning = false;
static volatile bool s_f10Down = false;
static volatile bool s_f11Down = false;
static volatile bool s_f12Down = false;
static volatile bool s_f7Down = false;
static HHOOK s_hMouseHook = NULL;
static HWND s_gameHwndForCalib = NULL;

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKbd = (KBDLLHOOKSTRUCT*)lParam;
        bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
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
            UIAddLog("[熱鍵] F11 按下");
            ToggleBotActive();
        } else if (pKbd->vkCode == VK_F11 && isUp) {
            s_f11Down = false;
        } else if (pKbd->vkCode == VK_F12 && isDown && !s_f12Down) {
            s_f12Down = true;
            UIAddLog("[熱鍵] F12 強制停止");
            ForceStopBot();
        } else if (pKbd->vkCode == VK_F12 && isUp) {
            s_f12Down = false;
        } else if (pKbd->vkCode == VK_F7 && isDown && !s_f7Down) {
            s_f7Down = true;
            CoordCalibrator& calib = CoordCalibrator::Instance();
            CalibIndex sel = calib.GetSelected();
            if (sel != CalibIndex::NONE) {
                calib.SetActive(true);
                UIAddLog("[校正] F7 鎖定 %s，點擊遊戲內目標設定座標", calib.GetLabel(sel));
            }
        } else if (pKbd->vkCode == VK_F7 && isUp) {
            s_f7Down = false;
        } else if (pKbd->vkCode == VK_ESCAPE) {
            CoordCalibrator::Instance().SetActive(false);
        }
    }
    return CallNextHookEx(s_hKeyboardHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        CoordCalibrator& calib = CoordCalibrator::Instance();
        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN) {
            if (calib.IsActive()) {
                MSLLHOOKSTRUCT* pMouse = (MSLLHOOKSTRUCT*)lParam;
                calib.SetGameHwnd(s_gameHwndForCalib);
                calib.OnScreenClick(pMouse->pt.x, pMouse->pt.y);
                calib.Save();
                return 1;
            }
        }
    }
    return CallNextHookEx(s_hMouseHook, nCode, wParam, lParam);
}

static DWORD WINAPI HotkeyThreadProc(LPVOID) {
    MSG msg;
    PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);
    s_hotkeyThreadId = GetCurrentThreadId();

    s_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    s_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, NULL, 0);
    if (s_hHotkeyReadyEvent) SetEvent(s_hHotkeyReadyEvent);
    if (!s_hKeyboardHook) { s_hotkeyThreadId = 0; return 1; }

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (!s_hotkeysRunning) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (s_hMouseHook) { UnhookWindowsHookEx(s_hMouseHook); s_hMouseHook = NULL; }
    if (s_hKeyboardHook) { UnhookWindowsHookEx(s_hKeyboardHook); s_hKeyboardHook = NULL; }
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
        if (s_hotkeyThreadId != 0) PostThreadMessage(s_hotkeyThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(s_hHotkeyThread, 3000);
        CloseHandle(s_hHotkeyThread);
        s_hHotkeyThread = NULL;
    }
}

// ============================================================================
// UI WIDGET HELPERS
// ============================================================================
static void ApplyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.IndentSpacing = 20;
    style.ScrollbarSize = 10;

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = T.bg_main;
    c[ImGuiCol_ChildBg] = T.bg_card;
    c[ImGuiCol_Border] = T.border;
    c[ImGuiCol_Text] = T.text_primary;
    c[ImGuiCol_TextDisabled] = T.text_muted;
    c[ImGuiCol_FrameBg] = T.bg_input;
    c[ImGuiCol_FrameBgHovered] = T.bg_card;
    c[ImGuiCol_FrameBgActive] = T.btn_hover;
    c[ImGuiCol_Button] = T.btn_normal;
    c[ImGuiCol_ButtonHovered] = T.btn_hover;
    c[ImGuiCol_ButtonActive] = T.btn_active;
    c[ImGuiCol_Header] = T.nav_active;
    c[ImGuiCol_HeaderHovered] = T.nav_hover;
    c[ImGuiCol_HeaderActive] = T.nav_active;
    c[ImGuiCol_Separator] = T.border;
    c[ImGuiCol_Tab] = T.nav_normal;
    c[ImGuiCol_TabHovered] = T.nav_hover;
    c[ImGuiCol_TabActive] = T.nav_active;
    c[ImGuiCol_SliderGrab] = T.accent;
    c[ImGuiCol_SliderGrabActive] = T.accent_hover;
    c[ImGuiCol_CheckMark] = T.accent;
    c[ImGuiCol_ScrollbarBg] = T.bg_sidebar;
    c[ImGuiCol_ScrollbarGrab] = T.btn_normal;
    c[ImGuiCol_ScrollbarGrabHovered] = T.btn_hover;
    c[ImGuiCol_ScrollbarGrabActive] = T.btn_active;
}

static void BeginCard(const char* title) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, T.bg_card);
    ImGui::PushStyleColor(ImGuiCol_Border, T.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::BeginChild(title, ImVec2(0, 0), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

static void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

static void CardTitle(const char* title) {
    ImGui::Spacing();
    ImGui::TextColored(T.accent, "%s", title);
    ImGui::Separator();
    ImGui::Spacing();
}

static void SliderI(const char* label, int* val, int min, int max) {
    ImGui::TextColored(T.text_secondary, "%s", label);
    ImGui::SameLine();
    ImGui::PushItemWidth(80);
    ImGui::SliderInt(("##" + std::string(label)).c_str(), val, min, max, "%d");
    ImGui::PopItemWidth();
}

static void Checkbox(const char* label, bool* val) {
    ImGui::PushStyleColor(ImGuiCol_CheckMark, T.accent);
    ImGui::Checkbox(label, val);
    ImGui::PopStyleColor();
}

// ============================================================================
// PAGE: DASHBOARD (首頁)
// ============================================================================
static void PageDashboard() {
    extern BotState GetBotState();
    extern PlayerState GetCachedPlayerState();
    extern bool HasCachedPlayerStateData();

    BotState st = GetBotState();
    PlayerState ps = GetCachedPlayerState();
    bool hasPlayerData = HasCachedPlayerStateData();
    bool authValid = IsLicenseValid();
    extern BotConfig g_cfg;
    bool isActive = g_cfg.active.load();

    // Bot狀態大字顯示
    const char* stStr = "未知";
    ImVec4 stColor = T.text_muted;
    switch (st) {
        case BotState::IDLE: stStr = "閒置"; stColor = T.text_muted; break;
        case BotState::HUNTING: stStr = "戰鬥中"; stColor = T.success; break;
        case BotState::DEAD: stStr = "死亡"; stColor = T.danger; break;
        case BotState::RETURNING: stStr = "返回城鎮"; stColor = T.warning; break;
        case BotState::TOWN_SUPPLY: stStr = "城鎮補給"; stColor = T.info; break;
        case BotState::BACK_TO_FIELD: stStr = "返回野外"; stColor = T.info; break;
        case BotState::TRAVELING: stStr = "移動中"; stColor = T.accent; break;
        case BotState::PAUSED: stStr = "已暫停"; stColor = T.warning; break;
    }

    ImGui::TextColored(T.text_secondary, "Bot 狀態");
    ImGui::SameLine();
    ImGui::TextColored(stColor, "%s", stStr);

    // 戰鬥意向狀態（乾淨版本）
    ImGui::BeginGroup();
    IntentMode mode = GetIntentMode();
    int intentState = GetCombatIntentState();

    ImGui::TextColored(T.text_secondary, "意向:");
    ImGui::SameLine(0, 15);

    static int currentIntention = 0;
    const char* intentions[] = {
        "攻擊（普通）",
        "風箏模式",
        "AOE清怪",
        "只打精英怪",
        "防禦反擊"
    };

    ImGui::PushItemWidth(180);
    ImGui::Combo("##combat_intention", &currentIntention, intentions, IM_ARRAYSIZE(intentions));
    ImGui::PopItemWidth();

    ImGui::EndGroup();

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120);
    if (ImGui::Button(isActive ? "##暫停" : "##開始", ImVec2(120, 32))) {
        ToggleBotActive();
    }
    ImGui::SameLine(-126);
    ImGui::TextColored(isActive ? T.success : T.danger, isActive ? "  已啟用  " : "  已停用  ");

    ImGui::Spacing();

    // 屬性卡片
    BeginCard("##屬性");
    CardTitle("玩家屬性");
    if (hasPlayerData) {
        char buf[64];
        if (ps.maxHp > 1) {
            sprintf_s(buf, "%d / %d", ps.hp, ps.maxHp);
            ImGui::TextColored(T.text_secondary, "HP");
            ImGui::SameLine(60);
            ImGui::TextColored((ps.hp < ps.maxHp * 0.3f) ? T.danger : T.success, "%s", buf);
        }
        if (ps.maxMp > 1) {
            sprintf_s(buf, "%d / %d", ps.mp, ps.maxMp);
            ImGui::TextColored(T.text_secondary, "MP");
            ImGui::SameLine(60);
            ImGui::TextColored(T.info, "%s", buf);
        }
        if (ps.maxSp > 1) {
            sprintf_s(buf, "%d / %d", ps.sp, ps.maxSp);
            ImGui::TextColored(T.text_secondary, "SP");
            ImGui::SameLine(60);
            ImGui::TextColored(T.accent, "%s", buf);
        }
        if (ps.gold > 0) {
            ImGui::TextColored(T.text_secondary, "金幣");
            ImGui::SameLine(60);
            ImGui::Text("%lu", (unsigned long)ps.gold);
        }
        if (ps.level > 0) {
            ImGui::TextColored(T.text_secondary, "等級");
            ImGui::SameLine(60);
            ImGui::Text("%d", ps.level);
        }
    } else {
        ImGui::TextColored(T.text_muted, "等待遊戲數據...");
    }
    EndCard();

    ImGui::SameLine();

    // 系統狀態卡片
    BeginCard("##系統");
    CardTitle("系統狀態");
    ImGui::TextColored(T.text_secondary, "授權");
    ImGui::SameLine(80);
    ImGui::TextColored(authValid ? T.success : T.danger, authValid ? "已驗證" : "未驗證");

    ImGui::TextColored(T.text_secondary, "實體池");
    ImGui::SameLine(80);
    ImGui::TextColored(IsEntityPoolWorking() ? T.success : T.danger,
        IsEntityPoolWorking() ? "正常" : "失效");

    ImGui::TextColored(T.text_secondary, "封包");
    ImGui::SameLine(80);
    ImGui::TextColored(IsAttackSenderConnected() ? T.success : T.danger,
        IsAttackSenderConnected() ? "已連線" : "未連線");

    ImGui::TextColored(T.text_secondary, "戰鬥模式");
    ImGui::SameLine(80);
    ImGui::TextColored(IsRelativeOnlyCombatMode() ? T.info : T.success,
        IsRelativeOnlyCombatMode() ? "純相對" : "一般");
    EndCard();

    // 座標卡片
    BeginCard("##座標");
    CardTitle("當前位置");
    if (hasPlayerData && (ps.x != 0.0f || ps.z != 0.0f)) {
        ImGui::TextColored(T.text_secondary, "X:");
        ImGui::SameLine(30);
        ImGui::Text("%.1f", ps.x);
        ImGui::SameLine(120);
        ImGui::TextColored(T.text_secondary, "Z:");
        ImGui::SameLine(150);
        ImGui::Text("%.1f", ps.z);
        if (ps.mapId > 0) {
            ImGui::TextColored(T.text_secondary, "地圖:");
            ImGui::SameLine(60);
            ImGui::Text("MapID %d", ps.mapId);
        }
        if (ps.hasTarget > 0) {
            ImGui::TextColored(T.text_secondary, "目標:");
            ImGui::SameLine(60);
            ImGui::Text("%u", ps.targetId);
        }
    } else {
        ImGui::TextColored(T.text_muted, "等待座標數據...");
    }
    EndCard();
}

// ============================================================================
// PAGE: COMBAT (戰鬥設定)
// ============================================================================
static void PageCombat() {
    extern BotConfig g_cfg;

    bool useVisualMode = g_cfg.use_visual_mode.load();
    int attackRange = g_cfg.attack_range.load();
    int attackInterval = g_cfg.attack_interval_ms.load();
    bool autoPickup = g_cfg.auto_pickup.load();
    int pickupRange = g_cfg.pickup_range.load();
    int skillCount = g_cfg.attackSkillCount.load();
    int skillInterval = g_cfg.attackSkillInterval.load();
    bool autoSupport = g_cfg.auto_support.load();

    BeginCard("##戰鬥模式");
    CardTitle("戰鬥模式設定");
    bool useDmMode = g_cfg.use_dm_mode.load();
    Checkbox("視覺模式 (像素掃描)", &useVisualMode);
    Checkbox("DM 插件模式", &useDmMode);
    g_cfg.use_visual_mode.store(useVisualMode);
    g_cfg.use_dm_mode.store(useDmMode);
    if (useVisualMode) {
        ImGui::TextColored(T.success, "  使用像素掃描血條辨識");
    } else if (useDmMode) {
        ImGui::TextColored(T.info, "  使用大漠插件");
    } else {
        ImGui::TextColored(T.text_muted, "  使用記憶體讀取");
    }
    EndCard();

    ImGui::SameLine();

    BeginCard("##攻擊參數");
    CardTitle("攻擊參數");
    SliderI("攻擊範圍 (格)", &attackRange, 1, 30);
    SliderI("攻擊間隔 (ms)", &attackInterval, 10, 200);
    SliderI("F1攻擊技能數量 (1-5)", &skillCount, 1, 5);
    SliderI("攻擊技能冷卻 (ms)", &skillInterval, 50, 5000);
    int rightClickDelay = g_cfg.rightClickDelayMs.load();
    SliderI("右鍵延遲 (ms)", &rightClickDelay, 50, 500);
    g_cfg.attack_range.store(attackRange);
    g_cfg.attack_interval_ms.store(attackInterval);
    g_cfg.attackSkillCount.store(skillCount);
    g_cfg.mainSkillCount.store(skillCount);
    g_cfg.attackSkillInterval.store(skillInterval);
    g_cfg.rightClickDelayMs.store(rightClickDelay);
    EndCard();

    BeginCard("##撿物設定");
    CardTitle("撿物設定");
    Checkbox("自動撿物", &autoPickup);
    g_cfg.auto_pickup.store(autoPickup);
    if (autoPickup) {
        SliderI("撿物範圍 (格)", &pickupRange, 1, 10);
        g_cfg.pickup_range.store(pickupRange);
    }
    EndCard();

    ImGui::SameLine();

    BeginCard("##輔助技能");
    CardTitle("輔助技能");
    Checkbox("自動施放輔助", &autoSupport);
    g_cfg.auto_support.store(autoSupport);
    g_cfg.buffEnabled.store(autoSupport);
    if (autoSupport) {
        int buffCount = g_cfg.buffSkillCount.load();
        int buffCooldown = g_cfg.buffCastInterval.load();
        SliderI("F1輔助技能數量 (6-0)", &buffCount, 1, 5);
        SliderI("輔助冷卻時間 (秒)", &buffCooldown, 1, 300);
        g_cfg.buffSkillCount.store(buffCount);
        g_cfg.buffCastInterval.store(buffCooldown);
        g_cfg.buffWaveInterval.store(buffCooldown * 1000);
        g_cfg.buffSkillInterval.store(buffCooldown * 1000);
    }
    EndCard();
}

// ============================================================================
// PAGE: SUPPLY (補給設定)
// ============================================================================
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
    bool blindSell = g_cfg.blind_sell_enable.load();

    BeginCard("##復活設定");
    CardTitle("自動復活");
    Checkbox("自動復活", &autoRevive);
    g_cfg.auto_revive.store(autoRevive);
    if (autoRevive) {
        const char* reviveItems[] = { "歸魂珠優先", "復活法術", "基本復活" };
        ImGui::TextColored(T.text_secondary, "復活方式");
        ImGui::SameLine();
        ImGui::PushItemWidth(150);
        ImGui::Combo("##revive_mode", &reviveMode, reviveItems, 3);
        ImGui::PopItemWidth();
        g_cfg.revive_mode.store(reviveMode);
        SliderI("復活延遲 (ms)", &reviveDelay, 500, 5000);
        g_cfg.revive_delay_ms.store(reviveDelay);
    }
    EndCard();

    ImGui::SameLine();

    BeginCard("##購買設定");
    CardTitle("購買數量");
    SliderI("HP藥水", &buyHp, 0, 999);
    SliderI("MP藥水", &buyMp, 0, 999);
    SliderI("SP藥水", &buySp, 0, 999);
    SliderI("箭矢", &buyArrow, 0, 999);
    SliderI("符咒", &buyCharm, 0, 999);
    g_cfg.buy_hp_qty.store(buyHp);
    g_cfg.buy_mp_qty.store(buyMp);
    g_cfg.buy_sp_qty.store(buySp);
    g_cfg.buy_arrow_qty.store(buyArrow);
    g_cfg.buy_charm_qty.store(buyCharm);
    EndCard();

    ImGui::SameLine();

    BeginCard("##藥水檢測");
    CardTitle("藥水不足檢測");
    Checkbox("啟用檢測", &potionCheck);
    g_cfg.potion_check_enable.store(potionCheck);
    if (potionCheck) {
        int potionStart = g_cfg.potion_slot_start.load();
        int potionEnd = g_cfg.potion_slot_end.load();
        int minSlots = g_cfg.min_potion_slots.load();
        SliderI("起始格", &potionStart, 0, 77);
        SliderI("結束格", &potionEnd, 0, 77);
        SliderI("最低格數", &minSlots, 1, 20);
        g_cfg.potion_slot_start.store(potionStart);
        g_cfg.potion_slot_end.store(potionEnd);
        g_cfg.min_potion_slots.store(minSlots);
    }
    EndCard();

    BeginCard("##盲目賣物");
    CardTitle("盲目賣物設定");
    ImGui::TextColored(T.text_muted, "不走記憶體，直接輪點格子");
    Checkbox("啟用盲目賣物", &blindSell);
    g_cfg.blind_sell_enable.store(blindSell);
    if (blindSell) {
        int sellStart = g_cfg.blind_sell_start.load();
        int sellCount = g_cfg.blind_sell_count.load();
        int sellDelay = g_cfg.blind_sell_delay.load();
        SliderI("起始格", &sellStart, 0, 77);
        SliderI("賣格數", &sellCount, 1, 78);
        SliderI("間隔(ms)", &sellDelay, 50, 500);
        g_cfg.blind_sell_start.store(sellStart);
        g_cfg.blind_sell_count.store(sellCount);
        g_cfg.blind_sell_delay.store(sellDelay);
        ImGui::TextColored(T.warning, "  保護列 0-2");
    }
    EndCard();

    BeginCard("##自動補給");
    CardTitle("城鎮補給");
    const char* towns[] = { "聖門", "商洞", "玄巖", "鳳凰" };
    ImGui::TextColored(T.text_secondary, "返回城鎮");
    ImGui::SameLine();
    ImGui::PushItemWidth(100);
    ImGui::Combo("##town", &townIdx, towns, 4);
    ImGui::PopItemWidth();
    g_cfg.town_index.store(townIdx);

    SliderI("傳送延遲 (ms)", &teleportDelay, 1000, 10000);
    g_cfg.teleport_delay_ms.store(teleportDelay);
    ImGui::Spacing();
    Checkbox("啟用自動補給", &autoSupply);
    g_cfg.auto_supply.store(autoSupply);
    if (autoSupply) {
        Checkbox("自動賣垃圾", &autoSell);
        Checkbox("自動買藥水", &autoBuy);
        Checkbox("藥水不足檢測", &potionCheck);
        g_cfg.auto_sell.store(autoSell);
        g_cfg.auto_buy.store(autoBuy);
        g_cfg.potion_check_enable.store(potionCheck);
    }
    EndCard();

    ImGui::SameLine();

    BeginCard("##盲目賣物");
    CardTitle("盲目賣物");
    ImGui::TextColored(T.text_muted, "不走記憶體讀取，直接輪點格子");
    Checkbox("啟用盲目賣物", &blindSell);
    g_cfg.blind_sell_enable.store(blindSell);
    if (blindSell) {
        int sellStart = g_cfg.blind_sell_start.load();
        int sellCount = g_cfg.blind_sell_count.load();
        int sellDelay = g_cfg.blind_sell_delay.load();
        SliderI("起始格", &sellStart, 0, 77);
        SliderI("賣格數", &sellCount, 1, 78);
        SliderI("間隔 (ms)", &sellDelay, 50, 500);
        g_cfg.blind_sell_start.store(sellStart);
        g_cfg.blind_sell_count.store(sellCount);
        g_cfg.blind_sell_delay.store(sellDelay);
        ImGui::TextColored(T.warning, "  保護列 0-2 (前 18 格)");
    }
    EndCard();
}

// ============================================================================
// PAGE: PROTECT (保護設定)
// ============================================================================
static void PageProtect() {
    extern BotConfig g_cfg;
    extern int GetProtectedItemCount();
    extern int GetProtectedItemId(int index);
    extern int GetProtectedRowCount();
    extern bool IsRowProtected(int row);
    extern void SetRowProtected(int row, bool protect);

    BeginCard("##列保護");
    CardTitle("列保護");
    ImGui::TextColored(T.text_secondary, "勾選該列不自動出售");
    ImGui::Spacing();

    int protectedRows = GetProtectedRowCount();
    ImGui::Text("已保護: %d / 13 列", protectedRows);
    ImGui::Spacing();

    if (ImGui::BeginTable("##row_table", 7, ImGuiTableFlags_SizingFixedFit)) {
        for (int row = 0; row < 13; row++) {
            ImGui::TableNextColumn();
            bool isProtected = IsRowProtected(row);
            ImGui::PushID(row);
            if (ImGui::Checkbox("##row", &isProtected)) {
                SetRowProtected(row, isProtected);
            }
            ImGui::PopID();
            ImGui::SameLine();
            ImGui::Text("%d", row + 1);
        }
        ImGui::EndTable();
    }
    EndCard();

    ImGui::SameLine();

    BeginCard("##物品保護");
    CardTitle("物品保護");
    int protectedCount = GetProtectedItemCount();
    ImGui::Text("已保護: %d 個物品", protectedCount);
    ImGui::Spacing();

    if (protectedCount > 0) {
        int showCount = protectedCount > 15 ? 15 : protectedCount;
        for (int i = 0; i < showCount; i++) {
            ImGui::BulletText("ID: %d", GetProtectedItemId(i));
        }
        if (protectedCount > 15) {
            ImGui::TextColored(T.text_muted, "... 共 %d 個物品", protectedCount);
        }
    } else {
        ImGui::TextColored(T.text_muted, "尚無保護物品");
        ImGui::TextColored(T.text_muted, "(需掃描背包取得ID)");
    }
    EndCard();

    BeginCard("##保護說明");
    CardTitle("使用說明");
    ImGui::TextColored(T.text_secondary, "1. 列保護: 勾選該列不自動出售");
    ImGui::TextColored(T.text_secondary, "2. 物品保護: 需掃描背包取得ID");
    ImGui::TextColored(T.text_secondary, "3. 建議: 藥水列設為保護");
    ImGui::TextColored(T.text_secondary, "4. 盲目賣物: 不讀記憶體直接輪點");
    EndCard();
}

// ============================================================================
// PAGE: CALIBRATION (校正工具)
// ============================================================================
static void PageCalibration() {
    CoordCalibrator& calib = CoordCalibrator::Instance();
    static int s_selectedIdx = 0;
    static char s_inputX[32] = "";
    static char s_inputZ[32] = "";

    BeginCard("##校正說明");
    CardTitle("座標校正工具");
    ImGui::TextWrapped(
        "用於微調 RAN2 客戶端的相對座標。\n"
        "座標以 0-1000 相對值儲存。\n\n"
        "可校正: 復活按鈕、城鎮NPC位置、寵物餵食、攻擊定點、商店買賣");
    EndCard();

    ImGui::SameLine();

    BeginCard("##校正選擇");
    CardTitle("選擇校正項目");
    CalibIndex sel = calib.GetSelected();
    s_selectedIdx = (int)sel;

    ImGui::PushItemWidth(-1);
    if (ImGui::BeginListBox("##CalibList", ImVec2(-1, 180))) {
        for (int i = 0; i < (int)CalibIndex::COUNT; i++) {
            CalibIndex idx = (CalibIndex)i;
            bool isSelected = (s_selectedIdx == i);
            char line[64];
            snprintf(line, sizeof(line), "%s%s",
                calib.GetLabel(idx),
                calib.IsCalibrated(idx) ? " *" : "");
            if (ImGui::Selectable(line, isSelected)) {
                s_selectedIdx = i;
                calib.SetSelected(idx);
            }
        }
        ImGui::EndListBox();
    }
    ImGui::PopItemWidth();

    CalibIndex curIdx = (CalibIndex)s_selectedIdx;
    int curX = calib.GetX(curIdx);
    int curZ = calib.GetZ(curIdx);

    ImGui::Spacing();
    ImGui::TextColored(T.text_secondary, "X: %d  Z: %d", curX, curZ);
    EndCard();

    BeginCard("##座標輸入");
    CardTitle("手動輸入座標");
    ImGui::TextColored(T.text_secondary, "X:");
    ImGui::SameLine(30);
    ImGui::PushItemWidth(80);
    ImGui::InputText("##CalX", s_inputX, sizeof(s_inputX), ImGuiInputTextFlags_CharsDecimal);
    ImGui::PopItemWidth();
    ImGui::SameLine(130);
    ImGui::TextColored(T.text_secondary, "Z:");
    ImGui::SameLine(160);
    ImGui::PushItemWidth(80);
    ImGui::InputText("##CalZ", s_inputZ, sizeof(s_inputZ), ImGuiInputTextFlags_CharsDecimal);
    ImGui::PopItemWidth();

    ImGui::Spacing();
    if (ImGui::Button("套用", ImVec2(70, 28))) {
        int nx = atoi(s_inputX);
        int nz = atoi(s_inputZ);
        bool isAttackPoint = (curIdx >= CalibIndex::SCAN_PT01 && curIdx <= CalibIndex::SCAN_PT08);
        int maxX = isAttackPoint ? 1000 : 1023;
        int maxZ = isAttackPoint ? 1000 : 767;
        if (nx >= 0 && nx <= maxX && nz >= 0 && nz <= maxZ) {
            calib.Set(curIdx, nx, nz);
            calib.Save();
            UIAddLog("[校正] %s -> (%d, %d)", calib.GetLabel(curIdx), nx, nz);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("重置", ImVec2(70, 28))) {
        calib.Reset(curIdx);
        calib.Save();
    }
    ImGui::SameLine();
    if (ImGui::Button("全部重置", ImVec2(-1, 28))) {
        calib.ResetAll();
        calib.Save();
    }
    EndCard();

    ImGui::SameLine();

    BeginCard("##校正提示");
    CardTitle("校正流程");
    ImGui::TextColored(T.text_secondary, "1. 選擇要校正的項目");
    ImGui::TextColored(T.text_secondary, "2. 按 F7 鎖定該項目");
    ImGui::TextColored(T.text_secondary, "3. 在遊戲內移到目標位置");
    ImGui::TextColored(T.text_secondary, "4. 點擊遊戲視窗即可設定");
    ImGui::TextColored(T.text_secondary, "5. ESC 取消鎖定");
    ImGui::Spacing();
    if (calib.IsActive()) {
        ImGui::TextColored(T.warning, "[ 鎖定中 ] 請點擊遊戲視窗");
    } else {
        ImGui::TextColored(T.success, "[ 就緒 ]");
    }
    EndCard();
}

// ============================================================================
// PAGE: SETTINGS (系統設定)
// ============================================================================
static void PageSettings() {
    extern BotConfig g_cfg;

    int hpPct = g_cfg.hp_potion_pct.load();
    int mpPct = g_cfg.mp_potion_pct.load();
    int spPct = g_cfg.sp_potion_pct.load();
    int hpReturn = g_cfg.hp_return_pct.load();
    bool invReturn = g_cfg.inventory_return.load();
    int invFullPct = g_cfg.inventory_full_pct.load();
    bool enemyApproachDetect = g_cfg.enemy_approach_detect.load();

    BeginCard("##喝水設定");
    CardTitle("自動喝水");
    SliderI("HP < %%", &hpPct, 0, 100);
    SliderI("MP < %%", &mpPct, 0, 100);
    SliderI("SP < %%", &spPct, 0, 100);
    g_cfg.hp_potion_pct.store(hpPct);
    g_cfg.mp_potion_pct.store(mpPct);
    g_cfg.sp_potion_pct.store(spPct);
    EndCard();

    ImGui::SameLine();

    BeginCard("##回城設定");
    CardTitle("自動回城");
    SliderI("HP < %% 回城", &hpReturn, 0, 100);
    Checkbox("背包滿時回城", &invReturn);
    g_cfg.hp_return_pct.store(hpReturn);
    g_cfg.inventory_return.store(invReturn);
    if (invReturn) {
        SliderI("背包 > %% 滿", &invFullPct, 50, 100);
        g_cfg.inventory_full_pct.store(invFullPct);
    }
    EndCard();

    BeginCard("##敵人偵測");
    CardTitle("敵人接近偵測");
    Checkbox("啟用偵測", &enemyApproachDetect);
    g_cfg.enemy_approach_detect.store(enemyApproachDetect);
    if (enemyApproachDetect) {
        float approachSpeed = g_cfg.enemy_approach_speed.load();
        float approachDist = g_cfg.enemy_approach_dist.load();
        ImGui::TextColored(T.text_secondary, "速度閾值: %.0f", approachSpeed);
        ImGui::SameLine();
        ImGui::PushItemWidth(80);
        ImGui::SliderFloat("##spd", &approachSpeed, 20.0f, 100.0f, "%.0f");
        ImGui::PopItemWidth();
        ImGui::TextColored(T.text_secondary, "偵測距離: %.0f 格", approachDist);
        ImGui::SameLine();
        ImGui::PushItemWidth(80);
        ImGui::SliderFloat("##dist", &approachDist, 10.0f, 50.0f, "%.0f");
        ImGui::PopItemWidth();
        g_cfg.enemy_approach_speed.store(approachSpeed);
        g_cfg.enemy_approach_dist.store(approachDist);
    }
    EndCard();

    BeginCard("##技能設定");
    CardTitle("戰鬥技能設定");
    int mainCount = g_cfg.attackSkillCount.load();
    SliderI("F1 1-5攻擊技能數量", &mainCount, 1, 5);
    g_cfg.attackSkillCount.store(mainCount);
    g_cfg.mainSkillCount.store(mainCount);

    int buffCount = g_cfg.buffSkillCount.load();
    SliderI("F1 6-0輔助技能數量", &buffCount, 1, 5);
    g_cfg.buffSkillCount.store(buffCount);

    int attackSkillMs = g_cfg.attackSkillInterval.load();
    SliderI("攻擊技能冷卻(ms)", &attackSkillMs, 50, 5000);
    g_cfg.attackSkillInterval.store(attackSkillMs);

    int auxCooldownSec = g_cfg.buffCastInterval.load();
    SliderI("輔助冷卻(秒)", &auxCooldownSec, 1, 300);
    g_cfg.buffCastInterval.store(auxCooldownSec);
    g_cfg.buffWaveInterval.store(auxCooldownSec * 1000);
    g_cfg.buffSkillInterval.store(auxCooldownSec * 1000);

    ImGui::TextColored(T.text_secondary, "F1: 1-5 攻擊輪替，6-0 輔助冷卻施放");
    EndCard();

    ImGui::SameLine();

    BeginCard("##反PK設置");
    CardTitle("反玩家攻擊");
    bool antiPk = g_cfg.anti_pk_enable.load();
    Checkbox("啟用反PK（秒飛）", &antiPk);
    g_cfg.anti_pk_enable.store(antiPk);
    if (antiPk) {
        int cooldownSec = g_cfg.anti_pk_cooldown_sec.load();
        ImGui::TextColored(T.text_secondary, "逃生後冷卻:");
        ImGui::SameLine(100);
        ImGui::PushItemWidth(80);
        SliderI("##pk_cd", &cooldownSec, 60, 600);
        ImGui::PopItemWidth();
        g_cfg.anti_pk_cooldown_sec.store(cooldownSec);
        ImGui::TextColored(T.text_secondary, "逃生後等待冷卻結束自動返回練功");
    }
    EndCard();

    ImGui::SameLine();

    BeginCard("##遊戲時間");
    CardTitle("遊戲時間自動");
    bool autoGameTime = g_cfg.auto_game_time.load();
    Checkbox("啟用遊戲時間", &autoGameTime);
    g_cfg.auto_game_time.store(autoGameTime);
    if (autoGameTime) {
        int returnHour = g_cfg.game_time_hour.load();
        int returnMin = g_cfg.game_time_min.load();
        ImGui::TextColored(T.text_secondary, "回城:");
        ImGui::SameLine(60);
        ImGui::PushItemWidth(50);
        ImGui::SliderInt("##rtn_hr", &returnHour, 0, 23, "%dh");
        ImGui::PopItemWidth();
        ImGui::SameLine(120);
        ImGui::PushItemWidth(50);
        ImGui::SliderInt("##rtn_min", &returnMin, 0, 59, "%dm");
        ImGui::PopItemWidth();
        g_cfg.game_time_hour.store(returnHour);
        g_cfg.game_time_min.store(returnMin);

        bool autoReturnToField = g_cfg.auto_return_to_field.load();
        Checkbox("自動返回野外", &autoReturnToField);
        g_cfg.auto_return_to_field.store(autoReturnToField);
        if (autoReturnToField) {
            int fieldHour = g_cfg.game_time_return_hour.load();
            int fieldMin = g_cfg.game_time_return_min.load();
            ImGui::TextColored(T.text_secondary, "返回:");
            ImGui::SameLine(60);
            ImGui::PushItemWidth(50);
            ImGui::SliderInt("##fld_hr", &fieldHour, 0, 23, "%dh");
            ImGui::PopItemWidth();
            ImGui::SameLine(120);
            ImGui::PushItemWidth(50);
            ImGui::SliderInt("##fld_min", &fieldMin, 0, 59, "%dm");
            ImGui::PopItemWidth();
            g_cfg.game_time_return_hour.store(fieldHour);
            g_cfg.game_time_return_min.store(fieldMin);
        }
    }
    EndCard();
}

// ============================================================================
// PAGE: LICENSE (授權管理)
// ============================================================================
static void PageLicense() {
    TryAutoLoadLicense();
    bool licenseValid = IsLicenseValid();

    BeginCard("##授權狀態");
    CardTitle("離線授權");
    ImGui::TextColored(T.text_secondary, "狀態:");
    ImGui::SameLine(60);
    ImGui::TextColored(licenseValid ? T.success : T.danger,
        licenseValid ? "[ 已啟用 ]" : "[ 未啟用 ]");
    if (licenseValid) {
        ImGui::TextColored(T.text_secondary, "剩餘:");
        ImGui::SameLine(60);
        ImGui::Text("%s", s_licenseDays);
        if (strlen(s_licensePerms) > 0) {
            ImGui::TextColored(T.text_secondary, "權限:");
            ImGui::SameLine(60);
            ImGui::Text("%s", s_licensePerms);
        }
    }
    EndCard();

    ImGui::SameLine();

    BeginCard("##HWID");
    CardTitle("機器識別碼");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, T.bg_input);
    ImGui::BeginChild("##hwid_box", ImVec2(-1, 40), true);
    ImGui::TextColored(T.accent, "%s", GetMachineHWID());
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (ImGui::Button("複製 HWID", ImVec2(120, 28))) {
        ImGui::SetClipboardText(GetMachineHWID());
    }
    ImGui::SameLine();
    ImGui::TextColored(T.text_muted, "提供給管理員生成卡密");
    EndCard();

    BeginCard("##卡密輸入");
    CardTitle("輸入卡密");
    ImGui::PushItemWidth(-1);
    bool enterPressed = ImGui::InputText("##license_key", s_licenseKey, sizeof(s_licenseKey),
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    ImGui::Spacing();
    float btnW = (ImGui::GetContentRegionAvail().x - 8) * 0.5f;
    if (ImGui::Button("驗  證", ImVec2(btnW, 36))) {
        StartLicenseVerifyAsync();
    }
    ImGui::SameLine();
    if (ImGui::Button("刷  新", ImVec2(btnW, 36))) {
        StartLicenseVerifyAsync();
    }
    EndCard();

    if (s_licenseChecked && strlen(s_licenseMsg) > 0) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, licenseValid ?
            ImVec4(0.10f, 0.20f, 0.10f, 1.0f) : ImVec4(0.20f, 0.10f, 0.10f, 1.0f));
        ImGui::BeginChild("##license_msg", ImVec2(-1, 40), true);
        ImGui::TextColored(licenseValid ? T.success : T.danger, "%s", s_licenseMsg);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
}

// ============================================================================
// MAIN GUI RENDER
// ============================================================================
static int s_navIndex = 0;

void RenderMainGUI() {
    if (!g_guiVisible) return;

    ApplyTheme();
    ImGui::GetIO().FontGlobalScale = 1.0f;

    ImGuiViewport* vp = ImGui::GetMainViewport();

    // 視窗大小 850x550，置中顯示
    ImGui::SetNextWindowPos(ImVec2((vp->Size.x - 850.0f) * 0.5f, (vp->Size.y - 550.0f) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(850.0f, 550.0f), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, T.bg_main);
    ImGui::PushStyleColor(ImGuiCol_Border, T.border);

    if (!ImGui::Begin("JyTrainer###main", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        ImGui::PopStyleColor(2);
        ImGui::End();
        return;
    }

    // ========== 狀態列 ==========
    ImGui::PushStyleColor(ImGuiCol_ChildBg, T.bg_sidebar);
    ImGui::PushStyleColor(ImGuiCol_Border, T.border);
    ImGui::BeginChild("##StatusBar", ImVec2(-1, 40), true);
    bool authValid = IsLicenseValid();
    extern BotConfig g_cfg;
    bool isActive = g_cfg.active.load();

    ImGui::TextColored(T.accent, "JyTrainer");
    ImGui::SameLine(100);
    ImGui::TextColored(T.text_secondary, "|");

    ImGui::SameLine(120);
    ImGui::TextColored(T.text_secondary, "授權:");
    ImGui::SameLine(160);
    ImGui::TextColored(authValid ? T.success : T.danger, authValid ? "已驗證" : "未驗證");

    ImGui::SameLine(240);
    ImGui::TextColored(T.text_secondary, "Bot:");
    ImGui::SameLine(280);
    ImGui::TextColored(isActive ? T.success : T.danger, isActive ? "執行中" : "已停用");

    ImGui::SameLine(380);
    ImGui::TextColored(T.text_secondary, "F10 隱藏 | F11 暫停 | F12 停止");

    // 控制按鈕
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200);
    ImGui::PushStyleColor(ImGuiCol_Button, isActive ? T.warning : T.success);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isActive ? ImVec4(1.0f, 0.85f, 0.3f, 1.0f) : ImVec4(0.3f, 0.9f, 0.5f, 1.0f));
    if (ImGui::Button(isActive ? "暫停##bar" : "開始##bar", ImVec2(80, 28))) {
        ToggleBotActive();
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, T.danger);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
    if (ImGui::Button("停止##bar", ImVec2(80, 28))) {
        ForceStopBot();
    }
    ImGui::PopStyleColor(2);

    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    // ========== 主內容區 ==========
    const char* navItems[] = { "首頁", "戰鬥", "補給", "保護", "校正", "設定", "授權" };
    float navW = 90.0f;
    float contentW = ImGui::GetContentRegionAvail().x;

    // 導航
    ImGui::PushStyleColor(ImGuiCol_ChildBg, T.bg_sidebar);
    ImGui::BeginChild("##NavBar", ImVec2(navW, -1), true);
    for (int i = 0; i < 7; i++) {
        bool selected = (s_navIndex == i);
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? T.nav_active : T.nav_normal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, T.nav_hover);
        if (ImGui::Button(navItems[i], ImVec2(navW - 8, 36))) {
            s_navIndex = i;
        }
        ImGui::PopStyleColor(2);
        if (i < 6) ImGui::Spacing();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    // 內容頁
    ImGui::PushStyleColor(ImGuiCol_ChildBg, T.bg_main);
    ImGui::BeginChild("##Content", ImVec2(-1, -1), false);
    ImGui::Spacing();

    switch (s_navIndex) {
        case 0: PageDashboard(); break;
        case 1: PageCombat(); break;
        case 2: PageSupply(); break;
        case 3: PageProtect(); break;
        case 4: PageCalibration(); break;
        case 5: PageSettings(); break;
        case 6: PageLicense(); break;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleColor(2);
}
