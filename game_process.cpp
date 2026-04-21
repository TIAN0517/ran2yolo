#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include "game_process.h"
#include "offsets.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdio>
#include <cstdarg>
#include <winreg.h>

// ============================================================
// Forward Declaration
// ============================================================
static void LogGame(const char* fmt, ...);

// ============================================================
// SeDebugPrivilege 啟用（解決 Win7 OpenProcess 附加失敗）
// ============================================================
static bool EnableSeDebugPrivilege() {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        LogGame("[偵測] OpenProcessToken 失敗 (code=%u)", GetLastError());
        return false;
    }

    LUID luid;
    if (!LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &luid)) {
        LogGame("[偵測] LookupPrivilegeValue 失敗 (code=%u)", GetLastError());
        CloseHandle(hToken);
        return false;
    }

    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL)) {
        LogGame("[偵測] AdjustTokenPrivileges 失敗 (code=%u)", GetLastError());
        CloseHandle(hToken);
        return false;
    }

    CloseHandle(hToken);
    LogGame("[偵測] ✅ SeDebugPrivilege 已啟用");
    return true;
}

// ============================================================
// DirectDraw 兼容性修復（Win7 閃黑屏問題）
// ============================================================
static bool EnableDirectDrawEmulation() {
    // Win10/11：只檢測，不修改 registry
    OSVERSIONINFOW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
    typedef LONG(WINAPI* RtlGetVersionPtr)(OSVERSIONINFOW*);
    HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
    if (hNt) {
        RtlGetVersionPtr pfn = (RtlGetVersionPtr)GetProcAddress(hNt, "RtlGetVersion");
        if (pfn && pfn(&osvi) == 0) {
            if (osvi.dwMajorVersion >= 10) {
                LogGame("[DirectDraw] Win10/11 不修改 DirectDraw EmulationOnly 設定");
                return false;
            }
        }
    }

    if (!hNt) return false;

    // 優先使用 HKEY_CURRENT_USER（不需要 admin 權限）
    // 如果失敗則嘗試 HKEY_LOCAL_MACHINE（需要 admin 權限）
    const wchar_t* subKey = L"Software\\Microsoft\\DirectDraw\\EmulationOnly";
    DWORD value = 1;
    bool success = false;

    // 嘗試 HKEY_CURRENT_USER（使用者自己的設定，不影響其他用戶）
    HKEY hKey = NULL;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_SET_VALUE, &hKey);
    if (result == ERROR_FILE_NOT_FOUND) {
        // 鍵不存在，先創建它
        result = RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, NULL,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL);
    }
    if (result == ERROR_SUCCESS && hKey) {
        result = RegSetValueExW(hKey, NULL, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
        if (result == ERROR_SUCCESS) {
            LogGame("[DirectDraw] ✅ HKEY_CURRENT_USER\\%S 已設定 (EmulationOnly=1)", subKey);
            success = true;
        }
        RegCloseKey(hKey);
    }

    // 如果 HKEY_CURRENT_USER 失敗，嘗試 HKEY_LOCAL_MACHINE（需要 admin）
    if (!success) {
        hKey = NULL;
        result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_SET_VALUE, &hKey);
        if (result == ERROR_FILE_NOT_FOUND) {
            result = RegCreateKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, NULL,
                REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL);
        }
        if (result == ERROR_SUCCESS && hKey) {
            result = RegSetValueExW(hKey, NULL, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
            if (result == ERROR_SUCCESS) {
                LogGame("[DirectDraw] ✅ HKEY_LOCAL_MACHINE\\%S 已設定 (EmulationOnly=1)", subKey);
                success = true;
            }
            RegCloseKey(hKey);
        } else {
            DWORD err = GetLastError();
            LogGame("[DirectDraw] ⚠️ 無法寫入 registry (code=%u)，需要管理員權限", err);
        }
    }

    return success;
}

// ============================================================
// 診斷日誌（寫入 log 檔案，供 GUI 讀取顯示）
// ============================================================
static FILE* s_gameLog = NULL;
static CRITICAL_SECTION s_gameLogCs;
static bool s_gameLogReady = false;
static volatile LONG s_gameLogCsInit = 0;

static void OpenGameLog() {
    if (s_gameLog) return;

    char path[MAX_PATH] = {};
    GetModuleFileNameA(NULL, path, MAX_PATH);

    char* slash = strrchr(path, '\\');
    if (slash) {
        strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "RanBot_Trainer.log");
    }
    else {
        strcpy_s(path, sizeof(path), "RanBot_Trainer.log");
    }

    fopen_s(&s_gameLog, path, "a");
}

// Win7兼容的延遲初始化
static void EnsureGameLogReady() {
    if (InterlockedCompareExchange(&s_gameLogCsInit, 1, 0) == 0) {
        InitializeCriticalSection(&s_gameLogCs);
        s_gameLogReady = true;
    }
}

static void LogGame(const char* fmt, ...) {
    char msg[640];
    va_list args;
    va_start(args, fmt);
    int result = vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, args);
    va_end(args);

    if (result < 0) {
        msg[639] = '\0';
    }

    EnsureGameLogReady();

    EnterCriticalSection(&s_gameLogCs);
    OpenGameLog();
    if (s_gameLog) {
        fprintf(s_gameLog, "%s\n", msg);
        fflush(s_gameLog);
    }
    LeaveCriticalSection(&s_gameLogCs);

    HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    WriteFile(hCon, msg, (DWORD)strlen(msg), &written, NULL);
}

// ============================================================
// 清理資源
// ============================================================
static void CloseGameLog() {
    if (s_gameLog) {
        fclose(s_gameLog);
        s_gameLog = NULL;
    }
}

// ============================================================
// 輔助結構與函數
// ============================================================
struct WindowSearch {
    DWORD pid;
    HWND hwnd;
};

struct TitleSearch {
    HWND hwnd;
};

static void LogWindowDetails(HWND hwnd, const char* tag) {
    if (!hwnd) return;

    wchar_t title[256] = {};
    wchar_t cls[128] = {};
    RECT wr = {};
    RECT cr = {};
    GetWindowTextW(hwnd, title, 256);
    GetClassNameW(hwnd, cls, 128);
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);

    char titleA[256] = {};
    char clsA[128] = {};
    WideCharToMultiByte(CP_UTF8, 0, title, -1, titleA, (int)sizeof(titleA), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, cls, -1, clsA, (int)sizeof(clsA), NULL, NULL);

    LogGame("[%s] hwnd=%p title=%s class=%s win=%ldx%ld client=%ldx%ld",
        tag,
        hwnd,
        titleA[0] ? titleA : "(empty)",
        clsA[0] ? clsA : "(empty)",
        wr.right - wr.left, wr.bottom - wr.top,
        cr.right - cr.left, cr.bottom - cr.top);
}

static void NormalizeWindowTitle(const wchar_t* src, wchar_t* dst, size_t dstCount) {
    if (!dst || dstCount == 0) return;
    dst[0] = L'\0';
    if (!src) return;

    size_t out = 0;
    for (size_t i = 0; src[i] && out + 1 < dstCount; ++i) {
        wchar_t ch = src[i];
        if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
            continue;
        }
        dst[out++] = ch;
    }
    dst[out] = L'\0';
}

static bool IsRanWindowTitle(const wchar_t* title) {
    if (!title || !title[0]) return false;

    wchar_t normalized[256] = {};
    NormalizeWindowTitle(title, normalized, sizeof(normalized) / sizeof(normalized[0]));

    return wcsstr(normalized, L"亂2") != NULL ||
        wcsstr(normalized, L"乱2") != NULL ||
        wcsstr(normalized, L"Ran2") != NULL ||
        wcsstr(normalized, L"RAN2") != NULL;
}

static BOOL CALLBACK FindRanWindowByTitle(HWND hwnd, LPARAM lParam) {
    // 移除 IsWindowVisible 檢查（有些遊戲窗口初始隱藏）
    // 移除 GW_OWNER 檢查（允許子窗口）

    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, 256);

    // 調試：打印找到的所有窗口
    if (title[0]) {
        // 只在窗口包含特定關鍵字時記錄
        if (wcsstr(title, L"Game") || wcsstr(title, L"Ran") ||
            wcsstr(title, L"乱") || wcsstr(title, L"亂") ||
            wcsstr(title, L"2 o")) {
            LogGame("[偵測] 枚舉窗口: \"%S\" (hwnd=%p)", title, hwnd);
        }
    }

    if (!IsRanWindowTitle(title)) return TRUE;

    TitleSearch* search = (TitleSearch*)lParam;
    search->hwnd = hwnd;
    LogGame("[偵測] ✅ 找到遊戲窗口: \"%S\" (hwnd=%p)", title, hwnd);
    return FALSE;
}

static BOOL CALLBACK FindMainWindowForPid(HWND hwnd, LPARAM lParam) {
    WindowSearch* search = (WindowSearch*)lParam;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (pid != search->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER)) return TRUE;

    search->hwnd = hwnd;
    return FALSE;
}

static DWORD FindProcessIdByName(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(PROCESSENTRY32W);

    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (!_wcsicmp(pe.szExeFile, exeName)) {
            DWORD pid = pe.th32ProcessID;
            CloseHandle(snap);
            return pid;
        }
    }

    CloseHandle(snap);
    return 0;
}

static bool IsKnownGameExeNameW(const wchar_t* exeName) {
    if (!exeName || !exeName[0]) return false;

    static const wchar_t* kKnownNames[] = {
        L"Game.exe", L"Gf.exe", L"Ran2.exe",
        L"亂2online.exe", L"乱2online.exe", L"Ran2Online.exe",
        L"亂2 Online.exe", L"乱2 Online.exe",
        L"RAN2.exe", L"Ran-2.exe", L"Ran-2 Online.exe",
        L"gf.exe", L"ran2.exe",
    };

    for (size_t i = 0; i < sizeof(kKnownNames) / sizeof(kKnownNames[0]); ++i) {
        if (_wcsicmp(exeName, kKnownNames[i]) == 0) {
            return true;
        }
    }

    return false;
}

static bool IsKnownGameExeNameA(const char* exeName) {
    if (!exeName || !exeName[0]) return false;

    static const char* kKnownNames[] = {
        "Game.exe", "Gf.exe", "Ran2.exe",
        "亂2online.exe", "乱2online.exe", "Ran2Online.exe",
        "亂2 Online.exe", "乱2 Online.exe",
        "RAN2.exe", "Ran-2.exe", "Ran-2 Online.exe",
        "gf.exe", "ran2.exe",
    };

    for (size_t i = 0; i < sizeof(kKnownNames) / sizeof(kKnownNames[0]); ++i) {
        if (_stricmp(exeName, kKnownNames[i]) == 0) {
            return true;
        }
    }

    return false;
}

static bool IsAlternateGameExeNameW(const wchar_t* exeName) {
    if (!exeName || !exeName[0]) return false;
    static const wchar_t* kAltNames[] = {
        L"亂2online.exe", L"乱2online.exe",
        L"亂2 Online.exe", L"乱2 Online.exe",
        L"RAN2.exe", L"Ran-2.exe",
    };
    for (size_t i = 0; i < sizeof(kAltNames) / sizeof(kAltNames[0]); ++i) {
        if (_wcsicmp(exeName, kAltNames[i]) == 0) return true;
    }
    return false;
}

static bool IsAlternateGameExeNameA(const char* exeName) {
    if (!exeName || !exeName[0]) return false;
    static const char* kAltNames[] = {
        "亂2online.exe", "乱2online.exe",
        "亂2 Online.exe", "乱2 Online.exe",
        "RAN2.exe", "Ran-2.exe",
    };
    for (size_t i = 0; i < sizeof(kAltNames) / sizeof(kAltNames[0]); ++i) {
        if (_stricmp(exeName, kAltNames[i]) == 0) return true;
    }
    return false;
}

static DWORD FindGameProcessIdByKnownNames(wchar_t* matchedExeName, size_t matchedExeNameCount) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(PROCESSENTRY32W);

    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (!IsKnownGameExeNameW(pe.szExeFile) && !IsAlternateGameExeNameW(pe.szExeFile)) continue;

        if (matchedExeName && matchedExeNameCount > 0) {
            wcsncpy_s(matchedExeName, matchedExeNameCount, pe.szExeFile, _TRUNCATE);
        }
        DWORD pid = pe.th32ProcessID;
        CloseHandle(snap);
        return pid;
    }

    CloseHandle(snap);
    return 0;
}

// ============================================================
// 全域遊戲句柄
// ============================================================
static CRITICAL_SECTION s_ghCs;
static bool s_ghInited = false;
static volatile LONG s_ghCsInit = 0;
static GameHandle s_gameHandle;

// Win7兼容的延遲初始化
static void EnsureGhCsReady() {
    if (InterlockedCompareExchange(&s_ghCsInit, 1, 0) == 0) {
        InitializeCriticalSection(&s_ghCs);
        s_ghInited = true;
    }
}

GameHandle GetGameHandle() {
    EnsureGhCsReady();
    EnterCriticalSection(&s_ghCs);
    GameHandle ret = s_gameHandle;
    LeaveCriticalSection(&s_ghCs);
    return ret;
}

void SetGameHandle(GameHandle* gh) {
    EnsureGhCsReady();
    if (!gh) {
        EnterCriticalSection(&s_ghCs);
        if (s_gameHandle.hProcess) {
            CloseHandle(s_gameHandle.hProcess);
        }
        ZeroMemory(&s_gameHandle, sizeof(s_gameHandle));
        LeaveCriticalSection(&s_ghCs);
        return;
    }

    EnterCriticalSection(&s_ghCs);
    HANDLE oldHandle = s_gameHandle.hProcess;
    s_gameHandle = *gh;              // 直接拷貝全部欄位（包含 hProcess）
    // gh->hProcess 在 FindGameProcess 已經是 OpenProcess 的有效 handle
    // 不需要 DuplicateHandle，直接使用即可
    LeaveCriticalSection(&s_ghCs);

    // 關閉舊 handle（如果有的話）
    if (oldHandle) {
        CloseHandle(oldHandle);
    }

    LogGame("[Handle] SetGameHandle: pid=%u hProcess=%p baseAddr=0x%08X attached=%d",
        gh->pid, gh->hProcess, gh->baseAddr, gh->attached);
}

// ============================================================
// Win7~Win11 通用模組基址取得
// ============================================================
static bool IsWin7System() {
    OSVERSIONINFOW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);

    typedef LONG(WINAPI* RtlGetVersionPtr)(OSVERSIONINFOW*);
    HMODULE hNt = GetModuleHandleW(L"ntdll.dll");

    if (hNt) {
        RtlGetVersionPtr pfn = (RtlGetVersionPtr)GetProcAddress(hNt, "RtlGetVersion");
        if (pfn && pfn(&osvi) == 0) {
            return (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion <= 1);
        }
    }

    return false;
}

// Win7 額外視窗標題檢測
static bool TryFindGameWindowWin7(GameHandle* gh) {
    // Win7 可能使用不同的視窗標題
    // ✅ 添加更多可能的標題變體
    const wchar_t* win7Titles[] = {
        L"亂2 online",
        L"亂 2 online",
        L"乱2 online",
        L"乱 2 online",
        L"Ran2 Online",
        L"Ran 2 Online",
        L"Ran2",
        L"Ran 2",
        L"Ran2 Online ",
        L"Ran2 ",
        L"RAN2",
        L"Ran-2 Online",
        L"Ran-2",
        // ✅ 添加更多變體（可能有全形空格或其他字符）
        L"亂2Online",
        L"乱2Online",
        L"Ran2Online",
        L"Ran2 online",
        L"RAN2 online",
        L"Ran2Online",
    };

    for (int i = 0; i < sizeof(win7Titles) / sizeof(win7Titles[0]); i++) {
        gh->hWnd = FindWindowW(NULL, win7Titles[i]);
        if (gh->hWnd) {
            LogGame("[Win7] 找到視窗標題: %S", win7Titles[i]);
            return true;
        }
    }

    // ✅ 新增：枚舉所有頂層視窗，找出類名為 "RenderWindow" 或包含 "Ran" 的視窗
    struct WindowSearchCtx {
        HWND found;
        wchar_t foundTitle[128];
    };
    static wchar_t s_lastFoundTitle[128] = {0};

    WindowSearchCtx ctx = {NULL, {0}};
    auto EnumCallback = [](HWND hwnd, LPARAM lp) -> BOOL {
        WindowSearchCtx* c = (WindowSearchCtx*)lp;
        if (!IsWindowVisible(hwnd)) return TRUE;

        wchar_t title[256] = {0};
        int len = GetWindowTextW(hwnd, title, 256);
        if (len <= 0) return TRUE;

        // 檢查標題是否包含 "亂" 或 "Ran" 或 "ran"
        if (wcsstr(title, L"亂") || wcsstr(title, L"乱") ||
            wcsstr(title, L"Ran") || wcsstr(title, L"ran")) {
            if (c->found == NULL) {  // 只記錄第一個
                c->found = hwnd;
                wcsncpy_s(c->foundTitle, title, 127);
                LogGame("[Win7] 枚舉找到視窗: %S", title);
            }
            return FALSE;  // 找到了就停止
        }
        return TRUE;
    };
    EnumWindows(EnumCallback, (LPARAM)&ctx);

    if (ctx.found) {
        gh->hWnd = ctx.found;
        wcscpy_s(s_lastFoundTitle, ctx.foundTitle);
        LogGame("[Win7] 使用枚舉找到的視窗: %S", s_lastFoundTitle);
        return true;
    }

    return false;
}

// 比對模組名是否為遊戲主模組
static bool IsGameModule(const wchar_t* modName, const char* fullPath) {
    if (modName && (IsKnownGameExeNameW(modName) || IsAlternateGameExeNameW(modName))) {
        return true;
    }

    if (fullPath) {
        const char* name = strrchr(fullPath, '\\');
        if (name) {
            name++;
        }
        else {
            name = fullPath;
        }
        if (IsKnownGameExeNameA(name) || IsAlternateGameExeNameA(name)) return true;
    }

    return false;
}

// 驗證模組基址是否合理（32位元遊戲不應該在極高位址）
static bool IsReasonableBase(DWORD base) {
    // 0x00400000 is a valid non-ASLR x86 image base. Reject only invalid user-mode ranges.
    if (base < 0x00400000) return false;
    if (base >= 0x80000000) return false;
    return true;
}

// 前向宣告（RefreshGameBaseAddress 在 GetGameBaseAddress 之前定義）
DWORD GetGameBaseAddress(GameHandle* gh);

// 刷新遊戲模組基址（傳入現有 GameHandle，不重新 OpenProcess）
DWORD RefreshGameBaseAddress(GameHandle* gh) {
    if (!gh || !gh->hProcess) return 0;
    DWORD base = GetGameBaseAddress(gh);
    if (base) {
        gh->baseAddr = base;
        gh->attached = true;
        LogGame("[Handle] ✅ RefreshGameBaseAddress 成功: baseAddr=0x%08X", base);
    }
    return base;
}

DWORD GetGameBaseAddress(GameHandle* gh) {
    if (!gh || !gh->hProcess) return 0;

    bool isWin7 = IsWin7System();
    LogGame("[偵測] [%s] 開始取得遊戲主模組基址...", isWin7 ? "Win7" : "Win10/11");

    // ── 優先：CreateToolhelp32Snapshot ──
    DWORD snapFlags = TH32CS_SNAPMODULE;
    if (!isWin7) {
        snapFlags |= TH32CS_SNAPMODULE32;  // Win10+ 支援此旗標
    }

    HANDLE snap = CreateToolhelp32Snapshot(snapFlags, gh->pid);
    if (snap == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        LogGame("[偵測] CreateToolhelp32Snapshot 失敗 (code=%u)，改用 EnumProcessModules", (unsigned int)err);
    } else {
        MODULEENTRY32 me = {};
        me.dwSize = sizeof(MODULEENTRY32);

        BOOL ok = Module32First(snap, &me);
        while (ok) {
            if (IsGameModule(me.szModule, NULL)) {
                DWORD base = (DWORD)me.modBaseAddr;
                LogGame("[偵測] CreateToolhelp32Snapshot 找到 %S (base=0x%08X)",
                    me.szModule, (unsigned int)base);
                CloseHandle(snap);

                if (!IsReasonableBase(base)) {
                    LogGame("[偵測] ❌ Base=0x%08X 不是合理的 32-bit user-mode 載入位址",
                        (unsigned int)base);
                    LogGame("[偵測]    提示：請用管理員身份執行 JyTrainer.exe");
                    return 0;
                }

                LogGame("[偵測] ✅ 取得遊戲主模組基址 = 0x%08X", (unsigned int)base);
                return base;
            }
            ok = Module32Next(snap, &me);
        }
        CloseHandle(snap);
        LogGame("[偵測] CreateToolhelp32Snapshot 未找到遊戲主模組，改用 EnumProcessModules");
    }
    HMODULE hMods[512];
    DWORD cbNeeded = 0;

    if (EnumProcessModules(gh->hProcess, hMods, sizeof(hMods), &cbNeeded)) {
        int count = cbNeeded / sizeof(HMODULE);
        LogGame("[偵測] EnumProcessModules 找到 %d 個模組:", count);

        for (int i = 0; i < count; i++) {
            HMODULE hMod = hMods[i];
            DWORD base = (DWORD)hMod;
            char name[MAX_PATH];

            if (GetModuleFileNameExA(gh->hProcess, hMod, name, sizeof(name))) {
                char* shortName = strrchr(name, '\\');
                if (shortName) {
                    shortName++;
                }
                else {
                    shortName = name;
                }

                LogGame("  [%d] %s -> 0x%08X", i, shortName, (unsigned int)base);

                if (IsGameModule(NULL, name) && IsReasonableBase(base)) {
                    LogGame("[偵測] ✅ EnumProcessModules 取得遊戲主模組基址 = 0x%08X",
                        (unsigned int)base);
                    return base;
                }

                if (IsGameModule(NULL, name) && !IsReasonableBase(base)) {
                    LogGame("[偵測] ❌ 遊戲主模組 Base=0x%08X 不合理，忽略", (unsigned int)base);
                }
            }
        }
    }
    else {
        DWORD err = GetLastError();
        LogGame("[偵測] EnumProcessModules 失敗 (code=%u)", (unsigned int)err);
    }

    // ── Fallback：直接嘗試從遊戲記憶體空間搜尋 PE Header ──
    LogGame("[偵測] 直接記憶體搜尋遊戲主模組（最後手段）...");
    MEMORY_BASIC_INFORMATION mbi = {};
    DWORD addr = 0x00400000;  // 典型 32-bit 遊戲載入位址
    while (VirtualQueryEx(gh->hProcess, (LPCVOID)addr, &mbi, sizeof(mbi)) != 0) {
        if ((mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) &&
            mbi.RegionSize >= 0x1000) {
            BYTE header[4] = {};
            SIZE_T bytesRead = 0;
            ReadProcessMemory(gh->hProcess, (LPCVOID)addr, header, sizeof(header), &bytesRead);
            if (header[0] == 0x4D && header[1] == 0x5A) {  // "MZ"
                LogGame("[偵測] 直接記憶體搜尋找到 MZ header @ 0x%08X", addr);
                if (IsReasonableBase(addr)) {
                    LogGame("[偵測] ✅ 直接記憶體搜尋成功: 遊戲主模組基址 = 0x%08X", addr);
                    return addr;
                }
            }
        }
        addr = (DWORD)mbi.BaseAddress + mbi.RegionSize;
        if (addr >= 0x80000000) break;
    }

    LogGame("[偵測] ❌ 無法取得合理的遊戲主模組基址！");
    LogGame("[偵測]    請確認：1) 遊戲已啟動  2) 以管理員身份執行 JyTrainer.exe");
    return 0;
}

// ============================================================
// 找遊戲進程
// ============================================================
bool FindGameProcess(GameHandle* gh) {
    if (!gh) return false;
    ZeroMemory(gh, sizeof(*gh));
    const bool isWin7 = IsWin7System();

    // 詳細調試日誌（使用 Log 格式，可以在 GUI 上顯示）
    char dbgBuf[256];
    sprintf_s(dbgBuf, "開始搜尋遊戲 (isWin7=%d)", isWin7 ? 1 : 0);
    LogGame("[偵測] %s", dbgBuf);
    OutputDebugStringA(dbgBuf);

    // 啟用 SeDebugPrivilege（解決 Win7 OpenProcess 附加失敗）
    static bool s_debugPrivDone = false;
    if (!s_debugPrivDone) {
        EnableSeDebugPrivilege();
        s_debugPrivDone = true;
    }

    // Win7 DirectDraw 修復（自動設定，解決閃黑屏）
    static bool s_ddrawFixDone = false;
    if (!s_ddrawFixDone) {
        if (EnableDirectDrawEmulation()) {
            printf("[DirectDraw] ✅ DirectDraw Emulation 已啟用 (Win7 閃黑屏修復)\n");
        }
        s_ddrawFixDone = true;
    }

    if (isWin7) {
        TryFindGameWindowWin7(gh);
    }

    if (!gh->hWnd) gh->hWnd = FindWindowW(NULL, L"亂2 online");
    if (!gh->hWnd) gh->hWnd = FindWindowW(NULL, L"亂 2 online");
    if (!gh->hWnd) gh->hWnd = FindWindowW(NULL, L"乱2 online");
    if (!gh->hWnd) gh->hWnd = FindWindowW(NULL, L"乱 2 online");
    if (!gh->hWnd) gh->hWnd = FindWindowW(NULL, L"Ran2 Online");
    if (!gh->hWnd) gh->hWnd = FindWindowW(NULL, L"Ran 2 Online");
    if (!gh->hWnd) gh->hWnd = FindWindowW(NULL, L"Ran2");
    if (!gh->hWnd) gh->hWnd = FindWindowW(NULL, L"Ran 2");
    if (!gh->hWnd) {
        LogGame("[偵測] ❌ 找不到遊戲視窗（乱2 online/Ran2），將透過行程名搜尋");
        TitleSearch search = { NULL };
        EnumWindows(FindRanWindowByTitle, (LPARAM)&search);
        gh->hWnd = search.hwnd;
    }

    DWORD pid = 0;
    if (gh->hWnd) {
        GetWindowThreadProcessId(gh->hWnd, &pid);
        if (pid) {
            LogGame("[偵測] ✅ 找到遊戲 PID=%u (hWnd=%p)", pid, gh->hWnd);
        } else {
            LogGame("[偵測] ❌ GetWindowThreadProcessId 失敗 (hWnd=%p)", gh->hWnd);
        }
        LogWindowDetails(gh->hWnd, isWin7 ? "Win7Window" : "GameWindow");
    }

    if (!pid) {
        LogGame("[偵測] 視窗搜尋失敗，透過程式名搜尋...");
        wchar_t matchedExeName[MAX_PATH] = {};
        pid = FindGameProcessIdByKnownNames(matchedExeName,
            sizeof(matchedExeName) / sizeof(matchedExeName[0]));
        if (pid) {
            char matchedExeNameA[MAX_PATH] = {};
            WideCharToMultiByte(CP_UTF8, 0, matchedExeName, -1, matchedExeNameA,
                (int)sizeof(matchedExeNameA), NULL, NULL);
            LogGame("[偵測] ✅ 透過程式名找到 %s PID=%u",
                matchedExeNameA[0] ? matchedExeNameA : "(unknown)", pid);
            WindowSearch search = { pid, NULL };
            EnumWindows(FindMainWindowForPid, (LPARAM)&search);
            gh->hWnd = search.hwnd;
            LogWindowDetails(gh->hWnd, "PidWindow");
        } else {
            LogGame("[偵測] ❌ 找不到 Game.exe/Gf.exe/Ran2.exe 程序");
        }
    }

    if (!pid) {
        printf("[偵測] ❌ 找不到遊戲行程（Game.exe/Gf.exe/Ran2.exe）！\n");
        LogGame("[偵測] ❌ 找不到遊戲行程（Game.exe/Gf.exe/Ran2.exe）！");
        return false;
    }
    gh->pid = pid;

    // Win7~Win11 相容：嘗試多級權限降級
    DWORD accessFlags[] = {
        // Level 1: 完全權限（可能失敗於 UAC Session 隔離）
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ |
        PROCESS_QUERY_INFORMATION | SYNCHRONIZE,
        // Level 2: VM 操作權限
        PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
        // Level 3: 只讀權限（最基本）
        PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
        // Level 4: 有限查詢權限（Win7 Session 隔離專用）
        PROCESS_QUERY_LIMITED_INFORMATION,
        // Level 5: 全部權限（最後手段）
        PROCESS_ALL_ACCESS,
    };

    DWORD openedAccessFlags = 0;
    for (int i = 0; i < 5 && !gh->hProcess; i++) {
        gh->hProcess = OpenProcess(accessFlags[i], FALSE, pid);
        if (gh->hProcess) {
            openedAccessFlags = accessFlags[i];
        }
        if (!gh->hProcess && i == 0) {
            DWORD err = GetLastError();
            LogGame("[偵測] OpenProcess Level%d 失敗 (code=%u)", i + 1, err);
        }
    }

    if (!gh->hProcess) {
        DWORD err = GetLastError();
        printf("[偵測] ❌ OpenProcess 失敗！請用管理員身分執行 (code=%u)\n", err);
        LogGame("[偵測] ❌ OpenProcess 失敗！請用管理員身分執行 (code=%u)", err);
        return false;
    }

    LogGame("[偵測] OpenProcess 成功 (flags=0x%X)", openedAccessFlags);

    gh->baseAddr = GetGameBaseAddress(gh);
    gh->attached = (gh->baseAddr != 0);

    if (gh->attached) {
        printf("[偵測] ✅ 成功附加遊戲！PID=%u Base=0x%08X HWND=%p\n",
            (unsigned int)pid, (unsigned int)gh->baseAddr, gh->hWnd);
        LogGame("[偵測] ✅ 成功附加遊戲！PID=%u Base=0x%08X HWND=%p",
            (unsigned int)pid, (unsigned int)gh->baseAddr, gh->hWnd);
        return true;
    }

    // baseAddr 為 0：仍然保留 hProcess，讓呼叫者可以重試刷新 baseAddr
    // 不要 close handle，否則每次 FindGameProcess 都浪費一次 OpenProcess 機會
    if (gh->hProcess) {
        LogGame("[偵測] ⚠️ hProcess=%p 已取得但 baseAddr=0，保留 handle 供後續重試",
            gh->hProcess);
        gh->pid = pid;  // 確保 PID 有設定
        return true;    // 讓 bot 能繼續，不因 baseAddr 一開始是 0 就完全放棄
    }

    LogGame("[偵測] ❌ 無法開啟遊戲行程 (hProcess=NULL)");
    return false;
}

// ============================================================
// 其餘函數
// ============================================================
void CloseGameHandle(GameHandle* gh) {
    if (!gh) return;

    if (gh->hProcess) {
        CloseHandle(gh->hProcess);
    }

    ZeroMemory(gh, sizeof(*gh));
}

bool IsGameRunning(GameHandle* gh) {
    if (!gh || !gh->hProcess) return false;

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(gh->hProcess, &exitCode)) return false;

    return (exitCode == STILL_ACTIVE);
}

bool GetGameClientSize(GameHandle* gh, int* w, int* h) {
    if (!gh || !gh->hWnd) return false;

    RECT r;
    if (!GetClientRect(gh->hWnd, &r)) return false;

    if (w) *w = r.right - r.left;
    if (h) *h = r.bottom - r.top;

    return true;
}

bool EnsureGameClientSize(GameHandle* gh, int targetW, int targetH) {
    if (!gh || !gh->hWnd || !IsWindow(gh->hWnd) || targetW <= 0 || targetH <= 0) return false;

    WINDOWPLACEMENT wp = {};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(gh->hWnd, &wp)) {
        if (wp.showCmd == SW_SHOWMAXIMIZED || wp.showCmd == SW_MINIMIZE || wp.showCmd == SW_SHOWMINIMIZED) {
            return false;
        }
    }

    RECT clientRc = {};
    RECT windowRc = {};
    if (!GetClientRect(gh->hWnd, &clientRc) || !GetWindowRect(gh->hWnd, &windowRc)) return false;

    const int curClientW = clientRc.right - clientRc.left;
    const int curClientH = clientRc.bottom - clientRc.top;
    if (curClientW == targetW && curClientH == targetH) return true;

    LONG style = (LONG)GetWindowLongPtr(gh->hWnd, GWL_STYLE);
    LONG exStyle = (LONG)GetWindowLongPtr(gh->hWnd, GWL_EXSTYLE);
    BOOL hasMenu = GetMenu(gh->hWnd) != NULL;

    RECT desired = { 0, 0, targetW, targetH };
    if (!AdjustWindowRectEx(&desired, style, hasMenu, exStyle)) return false;

    const int outerW = desired.right - desired.left;
    const int outerH = desired.bottom - desired.top;
    if (!SetWindowPos(gh->hWnd, NULL, windowRc.left, windowRc.top, outerW, outerH,
        SWP_NOZORDER | SWP_NOACTIVATE)) {
        return false;
    }

    Sleep(80);

    RECT verifyRc = {};
    if (!GetClientRect(gh->hWnd, &verifyRc)) return false;
    const int finalW = verifyRc.right - verifyRc.left;
    const int finalH = verifyRc.bottom - verifyRc.top;

    LogGame("[Detect] Normalize game client: %dx%d -> %dx%d (target=%dx%d)",
        curClientW, curClientH, finalW, finalH, targetW, targetH);

    return finalW == targetW && finalH == targetH;
}

// ============================================================
// 清理函數（程式結束時呼叫）
// ============================================================
void CleanupGameProcess() {
    CloseGameLog();

    if (s_gameLogReady) {
        DeleteCriticalSection(&s_gameLogCs);
        s_gameLogReady = false;
    }

    if (s_ghInited) {
        DeleteCriticalSection(&s_ghCs);
        s_ghInited = false;
    }
}
