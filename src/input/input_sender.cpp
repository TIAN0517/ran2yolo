#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include "input_sender.h"
#include "win32_helpers.h"
#include "coord_calib.h"
#include "coords.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <atomic>
#include <tlhelp32.h>

// ============================================================
// 簡單 Log 函數（用於平台檢測等早期階段）
// ============================================================
static void Log(const char* tag, const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    char line[540];
    sprintf_s(line, "[%s] %s\n", tag, buf);
    WriteFile(hCon, line, (DWORD)strlen(line), &written, NULL);
}

// ============================================================
// 平台檢測（Win7~Win11 + 32位元）
// ============================================================
static bool s_isWin7 = false;
static bool s_platformDetected = false;

// 檢查視窗標題是否是遊戲（支援中文標題）
static bool IsKnownGameWindowTitle(HWND hWnd) {
    if (!hWnd) return false;
    wchar_t title[256] = {0};
    int len = GetWindowTextW(hWnd, title, 256);
    if (len <= 0) return false;

    // 檢查常見的遊戲視窗標題
    static const wchar_t* knownTitles[] = {
        L"亂2 online",
        L"亂2 Online",
        L"Ran2",
        L"RAN2"
    };

    for (size_t i = 0; i < sizeof(knownTitles)/sizeof(knownTitles[0]); i++) {
        if (wcsstr(title, knownTitles[i])) {
            return true;
        }
    }
    return false;
}

static bool IsKnownGameExeNameW(const wchar_t* exeName) {
    if (!exeName || !exeName[0]) return false;
    return lstrcmpiW(exeName, L"Game.exe") == 0 ||
           lstrcmpiW(exeName, L"Gf.exe") == 0 ||
           lstrcmpiW(exeName, L"Ran2.exe") == 0;
}

static bool IsKnownGameWindow(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return false;

    // 首先檢查視窗標題（支援"亂2 online"）
    if (IsKnownGameWindowTitle(hWnd)) {
        return true;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (!pid) return false;

    static HWND s_cachedHwnd = NULL;
    static DWORD s_cachedPid = 0;
    static DWORD s_cachedTick = 0;
    static bool s_cachedOk = false;

    DWORD now = GetTickCount();
    if (s_cachedHwnd == hWnd && s_cachedPid == pid && now - s_cachedTick < 5000) {
        return s_cachedOk;
    }

    bool okGame = false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {};
        pe.dwSize = sizeof(pe);
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
            if (pe.th32ProcessID == pid) {
                okGame = IsKnownGameExeNameW(pe.szExeFile);
                break;
            }
        }
        CloseHandle(snap);
    }

    s_cachedHwnd = hWnd;
    s_cachedPid = pid;
    s_cachedTick = now;
    s_cachedOk = okGame;

    if (!okGame) {
        static DWORD s_lastWarn = 0;
        if (now - s_lastWarn > 5000) {
            wchar_t title[256] = {0};
            GetWindowTextW(hWnd, title, 256);
            char titleA[256] = {0};
            WideCharToMultiByte(CP_ACP, 0, title, -1, titleA, 256, NULL, NULL);
            Log("輸入", "⚠️ 拒絕輸入：目標視窗不是遊戲 (標題=%s hWnd=%p pid=%u)",
                titleA, hWnd, (unsigned int)pid);
            s_lastWarn = now;
        }
    }
    return okGame;
}

// 導出的遊戲視窗檢查函數（供外部模組使用）
bool IsGameWindow(HWND hWnd) {
    return IsKnownGameWindow(hWnd);
}

void InitPlatformDetect() {
    OSVERSIONINFOW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
    bool gotVersion = false;

    // 先嘗試 RtlGetVersion（推薦方式，不會觸發 deprecated 警告）
    HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
    if (hNtDll) {
        typedef LONG(WINAPI* RtlGetVersionPtr)(OSVERSIONINFOW*);
        RtlGetVersionPtr rtlGetVersion = LoadProcAddressT<RtlGetVersionPtr>(hNtDll, "RtlGetVersion");
        if (rtlGetVersion && rtlGetVersion(&osvi) == 0) {
            gotVersion = true;
        }
    }

    // 如果 RtlGetVersion 失敗，嘗試 VerifyVersionInfo 方式檢查
    if (!gotVersion) {
        // 檢查是否為 Windows 7 (6.1)
        OSVERSIONINFOEXW osviEx = {};
        osviEx.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);
        osviEx.dwMajorVersion = 6;
        osviEx.dwMinorVersion = 1;

        DWORDLONG mask = 0;
        mask = VerSetConditionMask(mask, VER_MAJORVERSION, VER_EQUAL);
        mask = VerSetConditionMask(mask, VER_MINORVERSION, VER_LESS_EQUAL);

        if (VerifyVersionInfoW(&osviEx, VER_MAJORVERSION | VER_MINORVERSION, mask)) {
            osvi.dwMajorVersion = 6;
            osvi.dwMinorVersion = 1;
            gotVersion = true;
        }
    }

    // Win7 = 6.1, Win10 = 10.0, Win11 = 10.0
    s_isWin7 = (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion <= 1);
    s_platformDetected = true;

    Log("平台", "偵測到 %s (32位元)",
        s_isWin7 ? "Windows 7" : "Windows 10/11");
}

bool IsWin7Platform() {
    if (!s_platformDetected) InitPlatformDetect();
    return s_isWin7;
}

// 判斷是否為擴展鍵
static bool IsExtendedKey(BYTE vk) {
    switch (vk) {
    case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_INSERT: case VK_DELETE:
    case VK_RCONTROL: case VK_RMENU:
    case VK_LWIN: case VK_RWIN:
    case VK_NUMLOCK:
        return true;
    default:
        return false;
    }
}

// ============================================================
// 平滑鼠標移動（防偵測）
// ============================================================
static std::atomic<bool> s_smoothMouseEnabled{ true };
static std::atomic<int> s_smoothMouseStepDelay{ 15 };      // 每步延遲 ms
static std::atomic<int> s_smoothMouseMaxStep{ 25 };        // 每步最大像素

void SetSmoothMouseEnabled(bool enabled) { s_smoothMouseEnabled = enabled; }

static bool RelativeToClientClamped(HWND hWnd, int rx, int ry, POINT* outPt) {
    if (!hWnd || !outPt || !IsWindow(hWnd)) return false;

    int clientW, clientH;
    if (!CoordConv::GetClientRect(hWnd, &clientW, &clientH)) return false;
    if (clientW <= 0 || clientH <= 0) return false;

    int cx = rx * clientW / 1000;
    int cy = ry * clientH / 1000;

    if (cx < 0) cx = 0;
    if (cx >= clientW) cx = clientW - 1;
    if (cy < 0) cy = 0;
    if (cy >= clientH) cy = clientH - 1;

    outPt->x = cx;
    outPt->y = cy;
    return true;
}

// 平滑移動到目標座標（遊戲內相對座標 0-1000, 0-1000）
// 起點固定為人物中心 (500, 500)，目標為標準化相對座標
static void SmoothMoveTo(HWND hWnd, int targetX, int targetY) {
    if (!hWnd || !IsWindow(hWnd)) return;

    // 禁用 FocusGameWindow 以避免黑屏
    // if (s_disableForegroundWindow.load()) {
    //     // 跳過 Focus，直接發送訊息
    // } else {
    //     FocusGameWindow(hWnd);
    // }

    // 座標系：目標是遊戲內相對座標 (0-1000, 0-1000)
    // 起點固定為人物中心 (500, 500)
    int startX = 500;
    int startY = 500;

    // 確保目標在有效範圍內
    if (targetX < 0) targetX = 0;
    if (targetX > 1000) targetX = 1000;
    if (targetY < 0) targetY = 0;
    if (targetY > 1000) targetY = 1000;

    // 計算總距離
    int dx = targetX - startX;
    int dy = targetY - startY;
    int dist = (int)std::sqrt((double)dx * dx + (double)dy * dy);
    if (dist < 5) return;  // 太近了不需要移動

    // 軌跡分段
    int maxStep = s_smoothMouseMaxStep.load();
    int stepDelay = s_smoothMouseStepDelay.load();
    int steps = (std::max)(1, dist / maxStep);

    for (int i = 1; i <= steps; i++) {
        float t = (float)i / steps;
        // 緩入緩出曲線 (ease-in-out)
        t = t < 0.5f ? 2 * t * t : -1 + (4 - 2 * t) * t;

        int newX = startX + (int)(dx * t);
        int newY = startY + (int)(dy * t);

        // 加微量隨機種子偏移（模擬人類輕微顫抖）
        if (i < steps) {
            newX += (rand() % 7) - 3;
            newY += (rand() % 7) - 3;
        }

        POINT clientPt;
        if (!RelativeToClientClamped(hWnd, newX, newY, &clientPt)) return;

        POINT screenPt = clientPt;
        if (ClientToScreen(hWnd, &screenPt)) {
            SetCursorPos(screenPt.x, screenPt.y);
        }

        SendMessageA(hWnd, WM_MOUSEMOVE, 0,
            MAKELPARAM((unsigned short)clientPt.x, (unsigned short)clientPt.y));

        // 隨機延遲（基礎延遲 + 0~50% 隨機）
        int jitter = (rand() % (stepDelay * 3 / 2));
        Sleep(stepDelay + jitter);
    }
}

// ============================================================
// 座標轉換（使用統一 CoordConv 命名空間）
// ============================================================

// 遊戲相對座標 → 螢幕絕對座標
bool GameCoordsToScreen(HWND hWnd, int gameX, int gameZ, POINT* outScreen) {
    if (!hWnd || !outScreen) return false;
    int sx, sy;
    if (!CoordConv::RelToScreen(hWnd, gameX, gameZ, &sx, &sy)) return false;
    outScreen->x = sx;
    outScreen->y = sy;
    return true;
}

// 螢幕絕對座標 → 遊戲相對座標
bool ScreenToGameCoords(HWND hWnd, int screenX, int screenY, int* outGameX, int* outGameZ) {
    return CoordConv::ScreenToRel(hWnd, screenX, screenY, outGameX, outGameZ);
}

// 取得遊戲視窗客戶區大小
void GetGameClientSize(HWND hWnd, int* outW, int* outH) {
    CoordConv::GetClientRect(hWnd, outW, outH);
}

// ============================================================
// 延遲設定（可微調）
// ============================================================
static std::atomic<int> s_keyHoldDelay{ 20 };
static std::atomic<int> s_keyReleaseDelay{ 15 };
static std::atomic<int> s_actionDelay{ 40 };

// 黑屏修復開關：禁用 SetForegroundWindow 避免 DirectX 黑屏
static std::atomic<bool> s_disableForegroundWindow{true};  // 預設啟用

void SetForegroundWindowDisabled(bool disabled) {
    s_disableForegroundWindow = disabled;
}

bool FocusGameWindow(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return false;
    if (!IsKnownGameWindow(hWnd)) return false;

    // 禁用 SetForegroundWindow 以避免黑屏
    if (s_disableForegroundWindow.load()) {
        // 只恢復最小化的視窗，不做 SetForegroundWindow
        if (IsIconic(hWnd)) {
            ShowWindow(hWnd, SW_RESTORE);
        }
        return true;
    }

    if (IsIconic(hWnd)) {
        ShowWindow(hWnd, SW_RESTORE);
    }

    // Win7 兼容：確保執行緒附加
    DWORD targetTid = GetWindowThreadProcessId(hWnd, NULL);
    DWORD myTid = GetCurrentThreadId();
    bool attached = false;

    if (targetTid && targetTid != myTid) {
        attached = AttachThreadInput(myTid, targetTid, TRUE) != FALSE;
    }

    BringWindowToTop(hWnd);
    SetForegroundWindow(hWnd);
    SetActiveWindow(hWnd);
    SetFocus(hWnd);

    if (attached) {
        AttachThreadInput(myTid, targetTid, FALSE);
    }

    // Win7 兼容：增加等待時間
    Sleep(IsWin7Platform() ? 50 : 10);

    return (GetForegroundWindow() == hWnd);
}

// 客戶區像素 → 螢幕座標（帶邊界限制）
static bool ClientToScreenClamped(HWND hWnd, int cx, int cy, POINT* outPt) {
    if (!hWnd || !outPt || !IsWindow(hWnd)) return false;

    int clientW, clientH;
    if (!CoordConv::GetClientRect(hWnd, &clientW, &clientH)) return false;
    if (clientW <= 0 || clientH <= 0) return false;

    int sx = (int)((int64_t)cx * clientW / 1000);
    int sy = (int)((int64_t)cy * clientH / 1000);

    if (sx < 0) sx = 0;
    if (sx >= clientW) sx = clientW - 1;
    if (sy < 0) sy = 0;
    if (sy >= clientH) sy = clientH - 1;

    POINT pt = { sx, sy };
    if (!ClientToScreen(hWnd, &pt)) return false;

    *outPt = pt;
    return true;
}

class ScopedCursorGameLock {
public:
    explicit ScopedCursorGameLock(HWND hWnd) : oldPt_{}, oldClip_{}, hasOldPt_(false), hasOldClip_(false) {
        hasOldPt_ = GetCursorPos(&oldPt_) != FALSE;
        hasOldClip_ = GetClipCursor(&oldClip_) != FALSE;

        RECT rc = {};
        if (!hWnd || !IsWindow(hWnd) || !GetClientRect(hWnd, &rc)) return;
        POINT tl = { rc.left, rc.top };
        POINT br = { rc.right, rc.bottom };
        if (!ClientToScreen(hWnd, &tl) || !ClientToScreen(hWnd, &br)) return;

        RECT clip = { tl.x, tl.y, br.x, br.y };
        ClipCursor(&clip);
    }

    ~ScopedCursorGameLock() {
        if (hasOldClip_) {
            ClipCursor(&oldClip_);
        } else {
            ClipCursor(NULL);
        }
        if (hasOldPt_) {
            SetCursorPos(oldPt_.x, oldPt_.y);
        }
    }

private:
    POINT oldPt_;
    RECT oldClip_;
    bool hasOldPt_;
    bool hasOldClip_;
};

// SendMessage 版（相比 mouse_event，更安全、更兼容）
static void SendMouseClick(HWND hWnd, int cx, int cy, DWORD downFlag, DWORD upFlag) {
    (void)upFlag;
    if (!hWnd || !IsWindow(hWnd)) {
        static DWORD s_lastFailLog = 0;
        if (GetTickCount() - s_lastFailLog > 3000) {
            Log("輸入", "❌ SendMouseClick: hWnd 無效");
            s_lastFailLog = GetTickCount();
        }
        return;
    }
    if (!IsKnownGameWindow(hWnd)) {
        static DWORD s_lastFailLog = 0;
        if (GetTickCount() - s_lastFailLog > 3000) {
            Log("輸入", "❌ SendMouseClick: 視窗不是遊戲進程");
            s_lastFailLog = GetTickCount();
        }
        return;
    }

    // 禁用 SetForegroundWindow 以避免黑屏
    if (s_disableForegroundWindow.load()) {
        // 只用 SendMessage，無需 Focus
        POINT pt;
        if (!RelativeToClientClamped(hWnd, cx, cy, &pt)) return;

        LPARAM lParam = MAKELPARAM((unsigned short)pt.x, (unsigned short)pt.y);
        WPARAM downMsg = (downFlag == MOUSEEVENTF_RIGHTDOWN) ? WM_RBUTTONDOWN : WM_LBUTTONDOWN;
        WPARAM upMsg = (downFlag == MOUSEEVENTF_RIGHTDOWN) ? WM_RBUTTONUP : WM_LBUTTONUP;

        SendMessageA(hWnd, WM_MOUSEMOVE, 0, lParam);
        Sleep(10);
        SendMessageA(hWnd, (UINT)downMsg, 0, lParam);
        Sleep(15);
        SendMessageA(hWnd, (UINT)upMsg, 0, lParam);
        return;
    }

    // 先平滑移動到目標位置
    if (s_smoothMouseEnabled.load()) {
        SmoothMoveTo(hWnd, cx, cy);
        Sleep(10);
    } else {
        // Fallback: 直接移動
        FocusGameWindow(hWnd);
        POINT clientPt;
        if (RelativeToClientClamped(hWnd, cx, cy, &clientPt)) {
            POINT screenPt = clientPt;
            if (ClientToScreen(hWnd, &screenPt)) {
                SetCursorPos(screenPt.x, screenPt.y);
            }
            SendMessageA(hWnd, WM_MOUSEMOVE, 0,
                MAKELPARAM((unsigned short)clientPt.x, (unsigned short)clientPt.y));
        }
    }

    // 點擊
    POINT pt;
    if (!RelativeToClientClamped(hWnd, cx, cy, &pt)) {
        Log("輸入", "❌ SendMouseClick: RelativeToClientClamped 失敗");
        return;
    }

    WPARAM downWparam = (downFlag == MOUSEEVENTF_RIGHTDOWN) ? WM_RBUTTONDOWN : WM_LBUTTONDOWN;
    WPARAM upWparam = (downFlag == MOUSEEVENTF_RIGHTDOWN) ? WM_RBUTTONUP : WM_LBUTTONUP;
    LPARAM lParam = MAKELPARAM((unsigned short)pt.x, (unsigned short)pt.y);

    SendMessageA(hWnd, downWparam, 0, lParam);
    Sleep(10);
    SendMessageA(hWnd, upWparam, 0, lParam);
    Sleep(10);
    Log("輸入", "📍 [派] 右鍵點擊完成");
}

// 前景版：使用 SendMessageA（同步，確保 DirectX 遊戲收到）
void SendMouseClickDirect(HWND hWnd, int cx, int cy, DWORD downFlag, DWORD upFlag) {
    (void)upFlag;
    if (!hWnd || !IsWindow(hWnd)) {
        Log("輸入", "❌ SendMouseClickDirect: hWnd 無效 hWnd=%p", hWnd);
        return;
    }

    // Win10/11: 使用 SendMessageA 同步模式，DirectX 遊戲需要同步
    POINT pt;
    if (!RelativeToClientClamped(hWnd, cx, cy, &pt)) {
        Log("輸入", "❌ SendMouseClickDirect: RelativeToClientClamped 失敗");
        return;
    }

    WPARAM downMsg = (downFlag == MOUSEEVENTF_RIGHTDOWN) ? WM_RBUTTONDOWN : WM_LBUTTONDOWN;
    WPARAM upMsg = (downFlag == MOUSEEVENTF_RIGHTDOWN) ? WM_RBUTTONUP : WM_LBUTTONUP;
    WPARAM moveMask = (downFlag == MOUSEEVENTF_RIGHTDOWN) ? MK_RBUTTON : MK_LBUTTON;
    LPARAM lParam = MAKELPARAM((unsigned short)pt.x, (unsigned short)pt.y);

    Log("輸入", "📍 [WindowMsg] SendMessageA 點擊 client=(%d,%d)", pt.x, pt.y);

    // 禁用 FocusGameWindow 以避免黑屏
    if (s_disableForegroundWindow.load()) {
        SendMessageA(hWnd, WM_MOUSEMOVE, 0, lParam);
        Sleep(5);
        SendMessageA(hWnd, (UINT)downMsg, 0, lParam);
        Sleep(10);
        SendMessageA(hWnd, (UINT)upMsg, 0, lParam);
        return;
    }

    FocusGameWindow(hWnd);
    Sleep(IsWin7Platform() ? 50 : 30);

    // 使用 SendMessageA（非同步保證送達）
    SendMessageA(hWnd, WM_MOUSEMOVE, 0, lParam);
    Sleep(10);
    SendMessageA(hWnd, (UINT)downMsg, moveMask, lParam);
    Sleep(15);
    SendMessageA(hWnd, (UINT)upMsg, 0, lParam);
    Sleep(10);

    Log("輸入", "📍 [WindowMsg] 點擊完成");
}

// ============================================================
// 內部：通用按鍵發送
// ============================================================
static void SendKeyboardInput(BYTE vk, DWORD extraFlags) {
    if (!vk) return;
    UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    DWORD flags = extraFlags;
    if (IsExtendedKey(vk)) {
        flags |= KEYEVENTF_EXTENDEDKEY;
    }

    INPUT inputs[2];
    ZeroMemory(inputs, sizeof(inputs));

    // 按下
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = (flags & KEYEVENTF_SCANCODE) ? 0 : vk;
    inputs[0].ki.wScan = (flags & KEYEVENTF_SCANCODE) ? (WORD)scan : 0;
    inputs[0].ki.dwFlags = flags;
    inputs[0].ki.time = 0;
    inputs[0].ki.dwExtraInfo = 0;

    // 放開
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = (flags & KEYEVENTF_SCANCODE) ? 0 : vk;
    inputs[1].ki.wScan = (flags & KEYEVENTF_SCANCODE) ? (WORD)scan : 0;
    inputs[1].ki.dwFlags = flags | KEYEVENTF_KEYUP;
    inputs[1].ki.time = 0;
    inputs[1].ki.dwExtraInfo = 0;

    SendInput(2, inputs, sizeof(INPUT));
}

static void SendKeyboardTap(HWND hWnd, BYTE vk) {
    if (!hWnd || !vk) return;
    if (!IsWindow(hWnd)) return;

    // 禁用 FocusGameWindow 以避免黑屏
    if (!s_disableForegroundWindow.load()) {
        FocusGameWindow(hWnd);
        Sleep(IsWin7Platform() ? 50 : 30);
    }

    UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    LPARAM downLP = (scan << 16) | (1 << 30) | (1 << 31);  // keydown with repeat
    LPARAM upLP = (scan << 16) | (1 << 30) | (1 << 31) | (1 << 29);  // keyup

    if (!IsWin7Platform()) {
        PostMessageA(hWnd, WM_KEYDOWN, vk, downLP);
        Sleep(10);
        PostMessageA(hWnd, WM_KEYUP, vk, upLP);
    } else {
        SendMessageA(hWnd, WM_KEYDOWN, vk, downLP);
        Sleep(10);
        SendMessageA(hWnd, WM_KEYUP, vk, upLP);
    }
}

void SendKeyFast(BYTE vk) {
    SendKeyboardInput(vk, KEYEVENTF_SCANCODE);
}

// ============================================================
// 快速右鍵點擊（使用真實游標 + SendInput，避免 Win7 絕對座標飄移）
// TODO: Win7 測試 - 特別注意 RClickFast/ClickAtDirect 在 Win7 上的表現
// ============================================================
void RClickFast(HWND hWnd, int cx, int cy) {
    if (!hWnd || !IsWindow(hWnd)) return;
    if (!IsKnownGameWindow(hWnd)) return;

    if (!IsWin7Platform()) {
        SendMouseClickDirect(hWnd, cx, cy, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
        return;
    }

    // 戰鬥施法需要先穩定聚焦，否則技能鍵常常打到別的視窗
    if (!FocusGameWindow(hWnd)) return;
    Sleep(IsWin7Platform() ? 50 : 20);

    // 計算客戶區座標（遊戲內 1024x768 範圍）
    POINT pt;
    if (!RelativeToClientClamped(hWnd, cx, cy, &pt)) return;

    // 移動真實滑鼠到遊戲視窗客戶區的絕對螢幕座標
    POINT screenPt = pt;
    ClientToScreen(hWnd, &screenPt);

    if (IsWin7Platform()) {
        static DWORD s_lastWin7RClickLog = 0;
        DWORD now = GetTickCount();
        if (now - s_lastWin7RClickLog > 2000) {
            RECT rc = {};
            GetClientRect(hWnd, &rc);
            Log("輸入", "Win7 RClickFast rel=(%d,%d) client=(%d,%d) screen=(%d,%d) clientSize=%ldx%ld",
                cx, cy, pt.x, pt.y, screenPt.x, screenPt.y,
                rc.right - rc.left, rc.bottom - rc.top);
            s_lastWin7RClickLog = now;
        }
    }

    ScopedCursorGameLock cursorLock(hWnd);
    SetCursorPos(screenPt.x, screenPt.y);

    LPARAM lParam = MAKELPARAM((unsigned short)pt.x, (unsigned short)pt.y);
    SendMessageA(hWnd, WM_MOUSEMOVE, 0, lParam);
    Sleep(IsWin7Platform() ? 18 : 10);

    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
    SendInput(1, &input, sizeof(INPUT));
    Sleep(IsWin7Platform() ? 18 : 10);

    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    SendInput(1, &input, sizeof(INPUT));

    if (IsWin7Platform()) Sleep(10);
}

// ============================================================
// 公開函數
// ============================================================
void RClickAt(HWND hWnd, int cx, int cy) {
    if (!hWnd || !IsWindow(hWnd)) return;
    if (!IsKnownGameWindow(hWnd)) return;

    // 使用相對座標 (0-1000) 轉換為客戶區座標
    POINT pt;
    if (!RelativeToClientClamped(hWnd, cx, cy, &pt)) {
        Log("輸入", "❌ RClickAt: RelativeToClientClamped 失敗");
        return;
    }

    // DirectX 遊戲使用 SendMessageA（同步，確保送達）
    LPARAM lParam = MAKELPARAM((unsigned short)pt.x, (unsigned short)pt.y);

    Log("輸入", "📍 [RClickAt] SendMessageA 右鍵 client=(%d,%d) rel=(%d,%d)",
        pt.x, pt.y, cx, cy);

    SendMessageA(hWnd, WM_MOUSEMOVE, 0, lParam);
    Sleep(10);
    SendMessageA(hWnd, WM_RBUTTONDOWN, 0, lParam);
    Sleep(15);
    SendMessageA(hWnd, WM_RBUTTONUP, 0, lParam);
    Sleep(10);

    Log("輸入", "📍 [RClickAt] 右鍵完成");
}

void RClickAtDirect(HWND hWnd, int cx, int cy) {
    SendMouseClickDirect(hWnd, cx, cy, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
}

void ClickAtDirect(HWND hWnd, int cx, int cy) {
    SendMouseClickDirect(hWnd, cx, cy, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
}

void ClickAtDirectRight(HWND hWnd, int cx, int cy) {
    SendMouseClickDirect(hWnd, cx, cy, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
}

// 帶校正查詢的點擊（座標自動查詢 CoordCalibrator 覆寫）
void ClickAtCalib(HWND hWnd, int cx, int cy, CalibIndex idx) {
    CoordCalibrator& calib = CoordCalibrator::Instance();
    // 有設定過校正值就用校正值，否則用呼叫端提供的預設值
    int rx = calib.IsCalibrated(idx) ? calib.GetX(idx) : cx;
    int rz = calib.IsCalibrated(idx) ? calib.GetZ(idx) : cy;
    SendMouseClickDirect(hWnd, rx, rz, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
}

void ClickAttackPoint(HWND hWnd, int pointIndex) {
    if (pointIndex < 0 || pointIndex >= 8) {
        Log("輸入", "❌ ClickAttackPoint: pointIndex=%d 超出範圍", pointIndex);
        return;
    }

    if (!hWnd) {
        Log("輸入", "❌ ClickAttackPoint: hWnd 為 NULL!");
        return;
    }

    if (!IsWindow(hWnd)) {
        Log("輸入", "❌ ClickAttackPoint: hWnd=%p 不是有效視窗", hWnd);
        return;
    }

    CoordCalibrator& calib = CoordCalibrator::Instance();

    // 先查校正值（只有設定過才套用，否則用預設）
    CalibIndex calibIdx = (CalibIndex)((int)CalibIndex::SCAN_PT01 + pointIndex);
    int rx, rz;
    if (calib.IsCalibrated(calibIdx)) {
        rx = calib.GetX(calibIdx);
        rz = calib.GetZ(calibIdx);
    } else {
        const Coords::ScanPoint* scanPoints = Coords::GetAttackScanPoints();
        rx = scanPoints[pointIndex].x;
        rz = scanPoints[pointIndex].z;
    }

    Log("戰鬥", "攻擊點[%d] -> (%d,%d)", pointIndex, rx, rz);

    // 使用 SendMouseClickDirect（不使用 SetCursorPos）
    SendMouseClickDirect(hWnd, rx, rz, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
}

void GetAttackPointCoords(int pointIndex, int* outX, int* outY) {
    if (pointIndex < 0 || pointIndex >= 8) {
        *outX = 0; *outY = 0;
        return;
    }

    CoordCalibrator& calib = CoordCalibrator::Instance();
    CalibIndex calibIdx = (CalibIndex)((int)CalibIndex::SCAN_PT01 + pointIndex);

    if (calib.IsCalibrated(calibIdx)) {
        *outX = calib.GetX(calibIdx);
        *outY = calib.GetZ(calibIdx);
    } else {
        const Coords::ScanPoint* scanPoints = Coords::GetAttackScanPoints();
        *outX = scanPoints[pointIndex].x;
        *outY = scanPoints[pointIndex].z;
    }
}

void ClickAt(HWND hWnd, int cx, int cy) {
    FocusGameWindow(hWnd);
    // Win7 兼容：增加等待時間
    Sleep(IsWin7Platform() ? 50 : 30);
    SendMouseClick(hWnd, cx, cy, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
}

// 拖曳：左鍵按住移動後右鍵釋放（寵物餵食用）
void DragFeedToPet(HWND hWnd, int fromX, int fromZ, int toX, int toZ) {
    if (!hWnd || !IsWindow(hWnd)) return;
    if (!IsKnownGameWindow(hWnd)) return;

    POINT ptFrom, ptTo;
    if (!ClientToScreenClamped(hWnd, fromX, fromZ, &ptFrom)) return;
    if (!ClientToScreenClamped(hWnd, toX, toZ, &ptTo)) return;

    if (!FocusGameWindow(hWnd)) return;
    Sleep(50);

    ScopedCursorGameLock cursorLock(hWnd);

    // 移動到起點
    SetCursorPos(ptFrom.x, ptFrom.y);
    Sleep(30);

    // 左鍵按下
    INPUT inputs[1];
    ZeroMemory(inputs, sizeof(inputs));
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dx = 0;
    inputs[0].mi.dy = 0;
    inputs[0].mi.mouseData = 0;
    inputs[0].mi.time = 0;
    inputs[0].mi.dwExtraInfo = 0;
    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, inputs, sizeof(INPUT));
    Sleep(20);

    // 移動到終點
    SetCursorPos(ptTo.x, ptTo.y);
    Sleep(30);

    // 右鍵釋放（吃掉飼料）
    ZeroMemory(inputs, sizeof(inputs));
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dx = 0;
    inputs[0].mi.dy = 0;
    inputs[0].mi.mouseData = 0;
    inputs[0].mi.time = 0;
    inputs[0].mi.dwExtraInfo = 0;
    inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
    SendInput(1, inputs, sizeof(INPUT));
    Sleep(20);

    ZeroMemory(inputs, sizeof(inputs));
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dx = 0;
    inputs[0].mi.dy = 0;
    inputs[0].mi.mouseData = 0;
    inputs[0].mi.time = 0;
    inputs[0].mi.dwExtraInfo = 0;
    inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    SendInput(1, inputs, sizeof(INPUT));
}

void SendKeyDirect(HWND hWnd, BYTE vk) {
    if (!hWnd || !vk) {
        Log("輸入", "❌ SendKeyDirect: hWnd=%p vk=%d", (void*)hWnd, vk);
        return;
    }

    // 禁用 FocusGameWindow 以避免黑屏
    if (!s_disableForegroundWindow.load()) {
        FocusGameWindow(hWnd);
        Sleep(IsWin7Platform() ? 50 : 30);
    }

    UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    // 構造正確的 lParam：scan code + repeat + keydown/extended flag
    LPARAM downLP = (scan << 16) | (1 << 30) | (1 << 31);  // keydown with repeat
    LPARAM upLP = (scan << 16) | (1 << 30) | (1 << 31) | (1 << 29);  // keyup

    Log("輸入", "📍 [SendKeyDirect] PostMessageA WM_KEYDOWN vk=%d scan=%d", vk, scan);

    // 使用 PostMessageA（非同步，DirectX 遊戲較好支援）
    PostMessageA(hWnd, WM_KEYDOWN, vk, downLP);
    Sleep(15);
    PostMessageA(hWnd, WM_KEYUP, vk, upLP);

    Log("輸入", "📍 [SendKeyDirect] 按鍵完成 vk=%d", vk);
}

// 快速攻擊鍵：無 Focus，直接發送 PostMessage（避免黑屏）
void SendKeyAttack(HWND hWnd, BYTE vk) {
    if (!hWnd || !vk || !IsWindow(hWnd)) return;

    UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    LPARAM downLP = (scan << 16) | (1 << 30) | (1 << 31);
    LPARAM upLP = (scan << 16) | (1 << 30) | (1 << 31) | (1 << 29);

    PostMessageA(hWnd, WM_KEYDOWN, vk, downLP);
    Sleep(10);
    PostMessageA(hWnd, WM_KEYUP, vk, upLP);
}

void SendKeyInputFocused(BYTE vk, HWND hWnd) {
    SendKeyDirect(hWnd, vk);
}

void DClickAt(HWND hWnd, int cx, int cy) {
    ClickAt(hWnd, cx, cy);
    Sleep(s_keyHoldDelay.load());
    ClickAt(hWnd, cx, cy);
}

void Delay(int ms) {
    Sleep(ms > 0 ? ms : s_actionDelay.load());
}

void ReleaseAllMovementKeysPost(HWND hWnd) {
    if (!hWnd) return;

    BYTE keys[] = { 'W', 'S', 'A', 'D', VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        UINT scan = MapVirtualKeyA(keys[i], MAPVK_VK_TO_VSC);
        // Win7 兼容：確保 lParam 正確構造
        LPARAM lParam = (LPARAM)((scan << 16) | (1 << 30) | (3 << 30));
        PostMessageA(hWnd, WM_KEYUP, keys[i], lParam);
    }
}

void TypeNumber(HWND hWnd, int number) {
    char buf[16];
    sprintf_s(buf, sizeof(buf), "%d", number);

    // 32位元安全：正確轉換數字字符為虛擬按鍵
    for (size_t i = 0; buf[i]; i++) {
        char digit = buf[i];
        BYTE vk;

        if (digit >= '0' && digit <= '9') {
            // 數字鍵：0-9 對應 VK_0~VK_9
            vk = (BYTE)('0' + (digit - '0'));
        }
        else if (digit == '-') {
            vk = VK_OEM_MINUS;
        }
        else {
            continue;  // 跳過未知字符
        }

        SendKeyDirect(hWnd, vk);
        Sleep(30);
    }
}

void SendCtrlA(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return;

    if (!FocusGameWindow(hWnd)) return;
    Sleep(IsWin7Platform() ? 50 : 30);

    UINT scanCtrl = MapVirtualKeyA(VK_CONTROL, MAPVK_VK_TO_VSC);
    UINT scanA = MapVirtualKeyA('A', MAPVK_VK_TO_VSC);

    INPUT inputs[4];
    ZeroMemory(inputs, sizeof(inputs));

    // Ctrl 按下
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = (WORD)scanCtrl;
    inputs[0].ki.dwFlags = KEYEVENTF_SCANCODE;
    inputs[0].ki.time = 0;
    inputs[0].ki.dwExtraInfo = 0;

    // A 按下
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wScan = (WORD)scanA;
    inputs[1].ki.dwFlags = KEYEVENTF_SCANCODE;
    inputs[1].ki.time = 0;
    inputs[1].ki.dwExtraInfo = 0;

    // A 放開
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wScan = (WORD)scanA;
    inputs[2].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    inputs[2].ki.time = 0;
    inputs[2].ki.dwExtraInfo = 0;

    // Ctrl 放開
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wScan = (WORD)scanCtrl;
    inputs[3].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    inputs[3].ki.time = 0;
    inputs[3].ki.dwExtraInfo = 0;

    SendInput(4, inputs, sizeof(INPUT));
    Sleep(10);
}

void SendKeyDownUp(HWND hWnd, BYTE vk) {
    if (!hWnd || !vk) return;
    if (!IsWindow(hWnd)) return;

    UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    LPARAM downLP = (scan << 16) | (1 << 29) | (1 << 30) | (3 << 30);  // keydown
    LPARAM upLP = (scan << 16) | (1 << 29) | (1 << 30) | (3 << 30) | (1 << 31);  // keyup

    if (!IsWin7Platform()) {
        PostMessageA(hWnd, WM_KEYDOWN, vk, downLP);
        Sleep(10);
        PostMessageA(hWnd, WM_KEYUP, vk, upLP);
    } else {
        SendMessageA(hWnd, WM_KEYDOWN, vk, downLP);
        Sleep(10);
        SendMessageA(hWnd, WM_KEYUP, vk, upLP);
    }
}
