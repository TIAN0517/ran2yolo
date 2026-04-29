#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include <windows.h>
#include <atomic>

#include "bot_logic.h"
#include "fsm/state_handler.h"
#include "fsm/bot_tick_simplified.h"
#include "../game/game_process.h"
#include "../input/input_sender.h"
#include "../game/memory_reader.h"
#include "../config/offset_config.h"
#include "../config/coords.h"
#include "../game/nethook_shmem.h"
#include "../input/attack_packet.h"
#include "../license/offline_license.h"
#include "../platform/coord_calib.h"
#include "../input/target_lock.h"
#include "../vision/visionentity.h"
#include "../vision/screenshot_assist_compat.h"
#include "../vision/dm_visual_supply.h"
#include "../vision/dm_visual.h"
#include "pattern_scanner.h"
#include "recovery_vision.h"
#include "../embed/dm_plugin.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#ifndef _MSC_VER
#ifndef __readfsdword
#define __readfsdword(x) 0
#endif
#ifndef __readgsqword
#define __readgsqword(x) 0
#endif
#endif

// x64 uses GS segment, x86 uses FS segment
#ifdef _WIN64
#define READ_PEB_ADDR() ((void*)(__readgsqword(0x30)))
#else
#define READ_PEB_ADDR() ((void*)(__readfsdword(0x30)))
#endif

// ============================================================
// RAII 防重入保護（防止異常導致卡死）
// ============================================================
class ReentryGuard {
    bool& m_flag;
public:
    ReentryGuard(bool& flag) : m_flag(flag) { 
        m_flag = true; 
    }
    ~ReentryGuard() { 
        m_flag = false; 
    }
private:
    ReentryGuard(const ReentryGuard&) = delete;
    ReentryGuard& operator=(const ReentryGuard&) = delete;
};

// ============================================================
// 隨機延遲包裝（防外掛時序偵測）
// ============================================================
static void SleepJitter(int ms) {
    if (ms <= 0) { ms = 1; }  // ✅ 杜絕 Sleep(0) 等於沒延遲
    int jitter = ms + (int)((rand() % 41 - 20) * ms / 100.0f);
    if (jitter < 1) jitter = 1;
    Sleep(jitter);
}

// 前向宣告（Log/Logf 在後面定義）
void Log(const char* tag, const char* msg);
static Coords::Point ResolveReviveClickPoint(CalibIndex idx, int fx, int fz);
static bool ClickRelativeWithSendInput(HWND hWnd, int rx, int rz, const char* reason);

// External function declarations (from fsm_integration.cpp)
extern void InitFSM();
extern void ShutdownFSM();

// ============================================================
// Global state
// ============================================================
volatile bool g_Running = true;
BotConfig g_cfg;
static std::atomic<int> g_State{(int)BotState::IDLE};
static PlayerState s_uiPlayerCache;
static CRITICAL_SECTION s_uiCacheCs;
static CRITICAL_SECTION s_stateTransitionCs;  // 狀態轉換需要鎖保護

// ✅ 執行緒安全：UI 渲染鎖（保護 PlayerState 快取）
// 在 BotThread 寫入、UIThread 讀取時需要持有此鎖
static CRITICAL_SECTION s_renderCs;
static volatile LONG s_renderCsInit = 0;

// Win7兼容的渲染鎖初始化
static void EnsureRenderCsReady() {
    if (InterlockedCompareExchange(&s_renderCsInit, 1, 0) == 0) {
        InitializeCriticalSection(&s_renderCs);
    }
}

// ✅ 離線卡密驗證狀態
std::atomic<bool> g_licenseValid{false};
bool IsLicenseValid() { return g_licenseValid.load(); }
void SetLicenseValid(bool valid) { g_licenseValid.store(valid); }

static volatile LONG s_uiCacheCsInit = 0;
static volatile LONG s_invCacheCsInited = 0;

// 戰鬥意向枚舉（必須在 atomic 之前定義）
enum class CombatIntent {
    SEEKING = 0,   // 尋找目標中
    ENGAGING = 1,  // 已在攻擊範圍，施放技能中
    LOOTING  = 2,  // 目標死亡，等待撿物品
};

// 多執行緒安全：atomic 保護的關鍵狀態
static std::atomic<DWORD> s_currentTargetId{0};    // 當前鎖定目標 ID
static std::atomic<DWORD> s_killCount{0};          // 擊殺計數器
static std::atomic<int>   s_combatIntent{(int)CombatIntent::SEEKING};  // 戰鬥意向狀態

static DWORD s_deathStartTime = 0;  // 死亡時間
static DWORD s_deadRecoverySeenTime = 0;  // 偵測到 HP 恢復的穩定起點
// 復活狀態變量
static std::atomic<bool> s_reviveClicked{false};
static int s_reviveRetryCount = 0;
static bool s_stopInProgress = false;  // STOP 防重入
static bool s_enteredHunting = false;
static bool s_wasInHunting = false;
static bool s_enteredDeadState = false;
static bool s_loggedDead = false;
static BotState s_pausedPreviousState = BotState::IDLE;  // PAUSED 之前的狀態（用於正確恢復）
static DWORD s_lastReadFailLog = 0;

static const int REVIVE_MAX_ATTEMPTS = 20;
static const int REVIVE_SOUL_PEARL_ATTEMPTS = 4;
static DWORD s_consecutiveReadFail = 0;
static DWORD s_lastInvalidStateLog = 0;
static DWORD s_lastInventoryDiagLog = 0;
static DWORD s_lastValidPlayerStateTime = 0;
static DWORD s_lastPatternScanTime = 0;
static bool s_patternScanFailed = false;
static bool s_hasLastValidPlayerState = false;
static PlayerState s_lastValidPlayerState = {};
static std::atomic<bool> s_relativeOnlyCombatMode{false};
// 意向切換防抖：避免高頻率熱鍵切換
static DWORD s_lastIntentChange = 0;
// ============================================================
// CAPTCHA 安全碼防檢測
// ============================================================
static bool CheckCaptchaWindow()
{
    static bool s_alreadyNotified = false;

    // Fast window check
    HWND hCaptcha1 = FindWindowW(NULL, L"轉轉樂安全碼");
    HWND hCaptcha2 = FindWindowW(NULL, L"抽獎轉轉樂");

    bool detected = (hCaptcha1 && IsWindowVisible(hCaptcha1)) ||
                    (hCaptcha2 && IsWindowVisible(hCaptcha2));

    // Also check for "RANRI" in window titles
    if (!detected) {
        wchar_t title[256] = {0};
        HWND hWnd = GetForegroundWindow();
        if (hWnd && GetWindowTextW(hWnd, title, 256) > 0) {
            if (wcsstr(title, L"RANRI")) {
                detected = true;
            }
        }
    }

    if (detected) {
        BotState oldState = (BotState)g_State.load();
        if (oldState != BotState::PAUSED) {
            SetBotState(BotState::PAUSED);
            Log("防偵測", "★★★ 偵測到 CAPTCHA 安全碼視窗！已強制暫停機器人 ★★★");
            Log("防偵測", "  └- 請手動輸入安全碼");
            Log("防偵測", "  └- 輸入完後按 F11 繼續 bot");
        }
        if (!s_alreadyNotified) {
            s_alreadyNotified = true;
            MessageBoxW(NULL,
                L"偵測到 CAPTCHA 安全碼視窗！\n\n"
                L"機器人已自動暫停。\n"
                L"請手動輸入安全碼後，按 F11 恢復運行。",
                L"JyTrainer - 安全碼警告",
                MB_OK | MB_ICONWARNING | MB_TOPMOST);
        }
        return true;
    }
    s_alreadyNotified = false;
    return false;
}

// Handle CAPTCHA state - pause bot until window closes
static void HandleCaptchaState()
{
    // Bot is already paused by CheckCaptchaWindow()
    // Just wait for window to close
    HWND hCaptcha1 = FindWindowW(NULL, L"轉轉樂安全碼");
    HWND hCaptcha2 = FindWindowW(NULL, L"抽獎轉轉樂");

    while ((hCaptcha1 && IsWindowVisible(hCaptcha1)) ||
           (hCaptcha2 && IsWindowVisible(hCaptcha2))) {
        Sleep(500);
        hCaptcha1 = FindWindowW(NULL, L"轉轉樂安全碼");
        hCaptcha2 = FindWindowW(NULL, L"抽獎轉轉樂");
    }

    Log("防偵測", "✅ CAPTCHA 安全碼視窗已關閉");
}
static int s_huntPointIndex = 0;
static DWORD s_invBase = 0;

enum class PlayerStateReadStatus {
    OK = 0,
    READ_FAILED = 1,
    INVALID_DATA = 2,
};

enum class InventoryScanStatus {
    OK = 0,
    EMPTY = 1,
    INVALID_HANDLE = 2,
    INVALID_BASE = 3,
};
// ============================================================
// 預設保護物品（補充保護，適用於第4排之後的貴重物品）
// 需要透過 CE 掃描記憶體取得實際物品 ID
// ============================================================
const BotConfig::ProtectedItem BotConfig::defaultProtectedItems[] = {
    {"起點傳送卡", 0},       // 傳送卡（建議放第4排後）
    {"前點傳送卡", 0},       // 傳送卡（建議放第4排後）
    {"聖財團D停車場卡", 0},  // 特殊卡片
    {"好友卡", 0},           // 社交道具
    {"公車卡", 0},           // 交通工具卡
    {"異界磨石", 0},         // 強化材料
    {"磨石", 0},             // 強化材料
    {"護貝劑", 0},           // 強化材料
    {"高級護貝劑", 0},       // 強化材料
    {"生釉", 0},             // 強化材料
    {"強化釉", 0},           // 強化材料
    {"HP恢復藥劑", 0},       // 珍貴消耗品
};
// Win7兼容的延遲初始化
static void EnsureUICacheReady() {
    static volatile LONG s_stateCsInited = 0;
    if (InterlockedCompareExchange(&s_uiCacheCsInit, 1, 0) == 0) {
        InitializeCriticalSection(&s_uiCacheCs);
    }
    if (InterlockedCompareExchange(&s_stateCsInited, 1, 0) == 0) {
        InitializeCriticalSection(&s_stateTransitionCs);
    }
}
// IsGoodPtr is now defined in memory_reader.h
static float Distance2D(float ax, float az, float bx, float bz) {
    float dx = ax - bx;
    float dz = az - bz;
    return sqrtf(dx * dx + dz * dz);
}
static bool HasUsableWorldPos(float x, float z) {
    if (!std::isfinite(x) || !std::isfinite(z)) return false;
    if ((fabsf(x) <= 0.01f) && (fabsf(z) <= 0.01f)) return false;
    if ((fabsf(x) <= 2.0f) && (fabsf(z) <= 2.0f)) return false;
    if (fabsf(x) >= 100000.0f || fabsf(z) >= 100000.0f) return false;
    return true;
}
static bool LooksLikeAsciiDword(DWORD value) {
    unsigned char bytes[4] = {
        (unsigned char)(value & 0xFF),
        (unsigned char)((value >> 8) & 0xFF),
        (unsigned char)((value >> 16) & 0xFF),
        (unsigned char)((value >> 24) & 0xFF),
    };
    int printable = 0;
    for (int i = 0; i < 4; i++) {
        if (bytes[i] >= 0x20 && bytes[i] <= 0x7E) printable++;
    }
    return printable >= 3;
}
static bool IsLikelyValidInventorySlot(DWORD itemId, DWORD itemCount) {
    if (!itemId || itemId == 0xFFFFFFFF || itemId == 0xCCCCCCCC) return false;
    if (itemId >= 0xF0000000) return false;
    if ((itemId & 0xFF000000) != 0) return false;
    if (LooksLikeAsciiDword(itemId)) return false;
    if (itemCount == 0 || itemCount > 9999) return false;
    return true;
}
static int ClampRelativeCoord(int value, int limit) {
    if (value < 0) return 0;
    if (value >= limit) return limit - 1;
    return value;
}
static void ClampRelativePoint(int* x, int* y) {
    if (!x || !y) return;
    *x = ClampRelativeCoord(*x, 1024);
    *y = ClampRelativeCoord(*y, 768);
}
static BYTE SkillKeyFromIndex(int index) {
    index %= 10;
    if (index < 0) index = 0;
    return (index == 9) ? (BYTE)'0' : (BYTE)('1' + index);
}
static bool IsReliableTownMapId(int mapId) {
    // 0/-1 在目前偏移讀取裡經常代表 unknown/fallback，不可用來判斷城鎮。
    return mapId > 0 && mapId <= 65535;
}
bool IsTownMap(int mapId) {
    if (!IsReliableTownMapId(mapId)) return false;
    bool found = false;
    EnterCriticalSection(&g_cfg.cs_protected);
    for (int id : g_cfg.townMapIds) {
        if (IsReliableTownMapId(id) && id == mapId) { found = true; break; }
    }
    LeaveCriticalSection(&g_cfg.cs_protected);
    return found;
}
static bool HasTownMapConfig() {
    bool hasConfig = false;
    EnterCriticalSection(&g_cfg.cs_protected);
    for (int id : g_cfg.townMapIds) {
        if (IsReliableTownMapId(id)) { hasConfig = true; break; }
    }
    LeaveCriticalSection(&g_cfg.cs_protected);
    return hasConfig;
}
static bool IsInventoryScanUsable(InventoryScanStatus status) {
    return status == InventoryScanStatus::OK || status == InventoryScanStatus::EMPTY;
}
static const char* GetPlayerStateReadStatusName(PlayerStateReadStatus status) {
    switch (status) {
        case PlayerStateReadStatus::OK: return "OK";
        case PlayerStateReadStatus::READ_FAILED: return "READ_FAILED";
        case PlayerStateReadStatus::INVALID_DATA: return "INVALID_DATA";
        default: return "UNKNOWN";
    }
}
static const char* GetInventoryScanStatusName(InventoryScanStatus status) {
    switch (status) {
        case InventoryScanStatus::OK: return "OK";
        case InventoryScanStatus::EMPTY: return "EMPTY";
        case InventoryScanStatus::INVALID_HANDLE: return "INVALID_HANDLE";
        case InventoryScanStatus::INVALID_BASE: return "INVALID_BASE";
        default: return "UNKNOWN";
    }
}
static bool IsPlausibleMapId(int mapId) {
    // 允許 0（表示讀取失敗時使用相對模式）
    if (mapId == 0) return true;
    if (mapId < 0) return true;  // 允許 -1
    return (mapId >= 1 && mapId <= 65535);
}

// ============================================================
// Pattern Scan Status
// ============================================================
bool TriggerPatternScanIfNeeded(GameHandle* gh) {
    if (!gh || !gh->hProcess || !gh->baseAddr) return false;

    DWORD now = GetTickCount();

    // 每 30 秒最多嘗試一次 Pattern Scan
    if (now - s_lastPatternScanTime < 30000) return s_patternScanFailed;

    // 首次嘗試掃描
    if (!s_patternScanFailed) {
        Logf("PatternScan", "嘗試 AOB Pattern Scan 動態解析偏移...");

        // 安全：取得實際模組大小，避免掃描越界
        DWORD moduleSize = GetGameModuleSize(gh);
        if (moduleSize == 0) {
            Logf("PatternScan", "❌ 無法取得模組大小，跳過 Pattern Scan");
            s_patternScanFailed = true;
            s_lastPatternScanTime = now;
            return false;
        }

        bool scanResult = PatternScanner::ScanAllOffsets(
            gh->hProcess,
            (DWORD)gh->baseAddr,
            moduleSize
        );

        if (scanResult) {
            Logf("PatternScan", "✅ Pattern Scan 成功");
            s_lastPatternScanTime = now;
            return true;
        } else {
            Logf("PatternScan", "❌ Pattern Scan 失敗，靜態偏移已失效");
            Logf("PatternScan", "  建議：使用 Cheat Engine 重新掃描偏移");
            s_patternScanFailed = true;
            s_lastPatternScanTime = now;
            return false;
        }
    }

    return false;
}

// ============================================================
// Logging
// ============================================================
static FILE* s_logFile = NULL;
static void OpenLogFile() {
    if (s_logFile) return;
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) {
        strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "RanBot_Trainer.log");
    } else {
        strcpy_s(path, "RanBot_Trainer.log");
    }
    fopen_s(&s_logFile, path, "a");
}
void Log(const char* tag, const char* msg) {
    char line[768];
    SYSTEMTIME st;
    GetLocalTime(&st);
    sprintf_s(line, "[%s][%02d:%02d:%02d] %s",
        tag ? tag : "LOG", st.wHour, st.wMinute, st.wSecond, msg ? msg : "");
    HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    WriteFile(hCon, line, (DWORD)strlen(line), &written, NULL);
    WriteFile(hCon, "\n", 1, &written, NULL);
    OpenLogFile();
    if (s_logFile) {
        fprintf(s_logFile, "%s\n", line);
        fflush(s_logFile);
    }
}
void Logf(const char* tag, const char* fmt, ...) {
    char msg[640];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    Log(tag, msg);
}
// ============================================================
// Anti-Debug Protection
// ============================================================
static void InitAntiDebugProtection() {
    __try {
        typedef struct _PEB_FULL {
            BOOLEAN InheritedAddressSpace;
            BOOLEAN ReadImageFileExecOptions;
            BOOLEAN BeingDebugged;
            BOOLEAN SpareBool;
            HANDLE Mutant;
            PVOID ImageBaseAddress;
            PVOID Ldr;
            PVOID ProcessParameters;
            PVOID SubSystemData;
            PVOID ProcessHeap;
            PVOID FastPebLock;
            ULONG NtGlobalFlag;
            // ... 其餘欄位省略
        } PEB_FULL;
        PEB_FULL* peb = (PEB_FULL*)READ_PEB_ADDR();
        if (peb) {
            peb->BeingDebugged = FALSE;
            peb->NtGlobalFlag = 0;
            Log("防偵測", "✓ PEB 反偵測設置完成 (BeingDebugged=0, NtGlobalFlag=0)");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("防偵測", "⚠️ PEB 修改失敗（異常）");
    }
}
// ============================================================
// Anti-Debug Hooks（帶 SEH 保護）
// ============================================================
static void InitAntiDebugHooks() {
    __try {
        Log("防偵測", "正在安裝 Inline Patch Anti-Debug 保護...");
        HMODULE hK32 = GetModuleHandleA("kernel32.dll");
        if (!hK32) {
            Log("防偵測", "⚠️ 無法取得 kernel32.dll 句柄");
            return;
        }

        // IsDebuggerPresent Patch
        DWORD_PTR pIsDbg = (DWORD_PTR)GetProcAddress(hK32, "IsDebuggerPresent");
        if (pIsDbg) {
            DWORD oldProt = 0;
            if (VirtualProtect((LPVOID)pIsDbg, 3, PAGE_EXECUTE_READWRITE, &oldProt)) {
                BYTE patch[] = { 0x31, 0xC0, 0xC3 };  // xor eax,eax; ret
                memcpy((void*)pIsDbg, patch, sizeof(patch));
                VirtualProtect((LPVOID)pIsDbg, 3, oldProt, &oldProt);
                Log("防偵測", "✓ IsDebuggerPresent Patch");
            }
        }

        // DebugBreak Patch
        DWORD_PTR pDbgBrk = (DWORD_PTR)GetProcAddress(hK32, "DebugBreak");
        if (pDbgBrk) {
            DWORD oldProt = 0;
            if (VirtualProtect((LPVOID)pDbgBrk, 1, PAGE_EXECUTE_READWRITE, &oldProt)) {
                BYTE patch[] = { 0xC3 };  // ret
                memcpy((void*)pDbgBrk, patch, sizeof(patch));
                VirtualProtect((LPVOID)pDbgBrk, 1, oldProt, &oldProt);
                Log("防偵測", "✓ DebugBreak Patch");
            }
        }

        // CheckRemoteDebuggerPresent Patch
        DWORD_PTR pChkRemote = (DWORD_PTR)GetProcAddress(hK32, "CheckRemoteDebuggerPresent");
        if (pChkRemote) {
            DWORD oldProt = 0;
            if (VirtualProtect((LPVOID)pChkRemote, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
                BYTE patch[] = {
                    0x8B, 0x44, 0x24, 0x08,  // mov eax, [esp+8]
                    0xC7, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov [eax], 0
                    0x31, 0xC0,              // xor eax, eax
                    0x40,                    // inc eax
                    0xC2, 0x08, 0x00         // ret 8
                };
                memcpy((void*)pChkRemote, patch, sizeof(patch));
                VirtualProtect((LPVOID)pChkRemote, 16, oldProt, &oldProt);
                Log("防偵測", "✓ CheckRemoteDebuggerPresent Patch");
            }
        }

        Log("防偵測", "✓ Anti-Debug 保護安裝完成");
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("防偵測", "⚠️ Anti-Debug Hook 安裝失敗（安全繼續）");
    }
}
// ============================================================
// Cached player state（執行緒安全版本）
// ============================================================
static bool s_renderLockHeld = false;  // 防重入標誌

static void UpdateUICache(const PlayerState& st) {
    EnsureUICacheReady();
    // ✅ 寫入時鎖定渲染，防止半讀取
    bool needUnlock = false;
    if (!s_renderLockHeld) {
        LockRenderData();
        needUnlock = true;
    }
    EnterCriticalSection(&s_uiCacheCs);
    s_uiPlayerCache = st;
    LeaveCriticalSection(&s_uiCacheCs);
    if (needUnlock) UnlockRenderData();
}

// 無鎖版本（供快速喝水等高頻讀取使用）
PlayerState GetCachedPlayerStateRaw() {
    // 不等待，直接讀取（可能讀到半寫入狀態但換來速度）
    return s_uiPlayerCache;
}

PlayerState GetCachedPlayerState() {
    EnsureUICacheReady();
    PlayerState st;
    // ✅ 讀取時鎖定渲染（可重入）
    bool needUnlock = false;
    if (!s_renderLockHeld) {
        LockRenderData();
        needUnlock = true;
    }
    EnterCriticalSection(&s_uiCacheCs);
    st = s_uiPlayerCache;
    LeaveCriticalSection(&s_uiCacheCs);
    if (needUnlock) UnlockRenderData();
    return st;
}

// 鎖定渲染數據（內部實現 + 公開給 gui_ranbot.cpp 使用）
void LockRenderData() {
    EnsureRenderCsReady();
    EnterCriticalSection(&s_renderCs);
    s_renderLockHeld = true;
}

// 解鎖渲染數據（內部實現 + 公開給 gui_ranbot.cpp 使用）
void UnlockRenderData() {
    s_renderLockHeld = false;
    if (InterlockedCompareExchange(&s_renderCsInit, 0, 0) != 0) {
        LeaveCriticalSection(&s_renderCs);
    }
}
bool HasCachedPlayerStateData() {
    PlayerState st = GetCachedPlayerState();
    return (st.maxHp > 1 || st.maxMp > 1 || st.maxSp > 1 ||
            st.hp > 0 || st.mp > 0 || st.sp > 0 ||
            st.level > 0 || st.gold > 0 ||
            st.combatPower > 0 || st.physAtkMin > 0 || st.sprAtkMin > 0 ||
            (st.targetId != 0 && st.targetId != 0xFFFFFFFF));
}
bool IsRelativeOnlyCombatMode() {
    return s_relativeOnlyCombatMode.load();
}
// ============================================================
// Memory helpers
// ============================================================
static DWORD GetLocalCharPtrExternal(GameHandle* gh) {
    if (!gh || !gh->hProcess || !gh->baseAddr) return 0;
    return gh->baseAddr + OffsetConfig::GLCharacterObj();
}

static bool RefreshUiPlayerSnapshot(GameHandle* gh, PlayerState* outSnapshot = NULL) {
    if (!gh || !gh->hProcess || !gh->baseAddr) return false;

    PlayerState st;
    st.maxHp = st.maxMp = st.maxSp = 0;
    st.state = (BotState)g_State.load();

    DWORD base = gh->baseAddr;
    DWORD charAddr = GetLocalCharPtrExternal(gh);

    st.hp = SafeReadHP(gh->hProcess, base + OffsetConfig::PlayerHP());
    st.maxHp = SafeReadHP(gh->hProcess, base + OffsetConfig::PlayerMaxHP());
    st.mp = SafeReadHP(gh->hProcess, base + OffsetConfig::PlayerMP());
    st.maxMp = SafeReadHP(gh->hProcess, base + OffsetConfig::PlayerMaxMP());
    st.sp = SafeReadHP(gh->hProcess, base + OffsetConfig::PlayerSP());
    st.maxSp = SafeReadHP(gh->hProcess, base + OffsetConfig::PlayerMaxSP());
    st.gold = SafeNtRPM<int>(gh->hProcess, base + OffsetConfig::PlayerGold(), MemErrors::HP_INVALID);
    st.level = SafeRPM<WORD>(gh->hProcess, base + OffsetConfig::PlayerLevel(), 0);
    st.mapId = SafeReadMapID(gh->hProcess, base + OffsetConfig::PlayerMapID());
    st.combatPower = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerCombatPower(), 0);
    st.str = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerSTR(), 0);
    st.vit = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerVIT(), 0);
    st.physAtkMin = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerPhysAtkMin(), 0);
    st.sprAtkMin = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerSprAtkMin(), 0);
    st.targetId = SafeRPM<DWORD>(gh->hProcess, base + OffsetConfig::TargetID(), 0);

    if (charAddr && IsGoodPtr(charAddr)) {
        float cx = SafeRPM<float>(gh->hProcess, charAddr + OffsetConfig::CrowPosX(), 0.0f);
        float cz = SafeRPM<float>(gh->hProcess, charAddr + OffsetConfig::CrowPosZ(), 0.0f);
        if (HasUsableWorldPos(cx, cz)) {
            st.x = cx;
            st.z = cz;
            st.y = SafeRPM<float>(gh->hProcess, charAddr + OffsetConfig::CrowPosY(), 0.0f);
        }
    }

    // ✅ 驗證玩家數據有效性：檢查錯誤碼
    bool hasValidData = (st.maxHp > MemErrors::HP_INVALID || st.maxMp > MemErrors::HP_INVALID || st.maxSp > MemErrors::HP_INVALID ||
                         st.level > 0 || st.gold > MemErrors::HP_INVALID || st.combatPower > 0 ||
                         st.physAtkMin > 0 || st.sprAtkMin > 0 ||
                         (st.targetId != 0 && st.targetId != 0xFFFFFFFF));
    if (!hasValidData) {
        // MapID 若是錯誤碼，更新 reason
        if (st.mapId == MemErrors::MAPID_INVALID) {
            Log("錯誤", "[ERROR] MapID read failed - protected memory region or stale pointer");
        }
        return false;
    }

    if (outSnapshot) *outSnapshot = st;
    UpdateUICache(st);
    return true;
}
// ============================================================
// 實體池讀取（CROwList 鏈表）
// ============================================================
static bool s_entityPoolKnownBad = false;
static DWORD s_entityPoolBadTime = 0;

static DWORD GetCROWListHead(GameHandle* gh) {
    if (!gh || !gh->hProcess || !gh->baseAddr) return 0;

    // EntityPool 冷卻期（2秒）
    // 當讀取失敗時進入冷卻，避免頻繁重試
    if (s_entityPoolKnownBad) {
        DWORD now = GetTickCount();
        if (now - s_entityPoolBadTime < 2000) {
            return 0;  // 冷卻中，等待 Pattern Scan 恢復
        }
        s_entityPoolKnownBad = false;
    }

    DWORD charAddr = GetLocalCharPtrExternal(gh);
    if (!charAddr) return 0;

    DWORD landMan = SafeRPM<DWORD>(gh->hProcess, charAddr + OffsetConfig::EntityLandManPtr(), 0);
    if (!IsGoodPtr(landMan)) {
        s_entityPoolKnownBad = true;
        s_entityPoolBadTime = GetTickCount();
        return 0;
    }

    DWORD head = SafeRPM<DWORD>(gh->hProcess, landMan + OffsetConfig::EntityCROWList(), 0);
    if (!IsGoodPtr(head)) return 0;

    return head;
}
struct CrowInfo {
    DWORD serverId;
    DWORD crowDataPtr;
    DWORD hp;
    DWORD maxHp;
    float x, y, z;
};

// ============================================================
// EnumerateCrows - 實體列舉（移除 legacy fallback）
// 返回值：
//   > 0: 找到的怪物數量
//   = 0: 讀取失敗或無怪物（使用 MemErrors 錯誤碼）
// ============================================================
static int EnumerateCrows(GameHandle* gh, CrowInfo* outCrows, int maxCrows) {
    if (!gh || !gh->baseAddr || !outCrows) return 0;

    // ── 嘗試 NetHook 共享記憶體（首選方案）──
    if (NetHookShmem_IsConnected()) {
        ShmemEntity entities[200];
        int count = NetHookShmem_EnumerateEntities(entities, 200);
        if (count > 0) {
            int outCount = 0;
            for (int i = 0; i < count && outCount < maxCrows; i++) {
                ShmemEntity& e = entities[i];
                if (e.id == 0 || e.id == 0xFFFFFFFF) continue;
                if (e.dead) continue;
                CrowInfo& ci = outCrows[outCount];
                ci.serverId = e.id;
                ci.crowDataPtr = 0;
                ci.hp = e.hp;
                ci.maxHp = e.maxHp;
                ci.x = e.x;
                ci.y = e.y;
                ci.z = e.z;
                outCount++;
            }
            if (outCount > 0) return outCount;
        }
    }

    // ── CROwList 直接讀取（需要 hProcess）──
    if (!gh->hProcess) {
        Logf("掃描", "EnumerateCrows: hProcess 無效，無法讀取");
        return 0;
    }

    DWORD headNode = GetCROWListHead(gh);
    if (!headNode) {
        // EntityPool 不可用，狀態機應進入 RECOVERY 等待 Pattern Scan 恢復
        return 0;
    }

    int count = 0;
    DWORD nodeAddr = headNode;
    DWORD visited[200];
    int visitCount = 0;

    while (nodeAddr && count < maxCrows && visitCount < OffsetConfig::EntityMaxCrows()) {
        bool alreadyVisited = false;
        for (int i = 0; i < visitCount; i++) {
            if (visited[i] == nodeAddr) { alreadyVisited = true; break; }
        }
        if (alreadyVisited) break;
        visited[visitCount++] = nodeAddr;

        DWORD crowPtr = SafeRPM<DWORD>(gh->hProcess, nodeAddr + OffsetConfig::CrowNodeCrowPtr(), 0);
        DWORD nextNode = SafeRPM<DWORD>(gh->hProcess, nodeAddr + OffsetConfig::CrowNodeNext(), 0);

        if (crowPtr && IsGoodPtr(crowPtr)) {
            DWORD crowData = SafeRPM<DWORD>(gh->hProcess, crowPtr + OffsetConfig::CrowDataPtr(), 0);
            if (crowData && IsGoodPtr(crowData)) {
                CrowInfo& ci = outCrows[count];
                ci.serverId = SafeRPM<DWORD>(gh->hProcess, crowPtr + OffsetConfig::CrowServerID(), 0);
                ci.crowDataPtr = crowData;
                ci.hp = SafeRPM<DWORD>(gh->hProcess, crowPtr + OffsetConfig::CrowHP(), 0);
                WORD maxHp = SafeRPM<WORD>(gh->hProcess, crowPtr + OffsetConfig::CrowMaxHP(), 0);
                ci.maxHp = maxHp > 0 ? (DWORD)maxHp : ci.hp;
                ci.x = SafeRPM<float>(gh->hProcess, crowPtr + OffsetConfig::CrowPosX(), 0.0f);
                ci.y = SafeRPM<float>(gh->hProcess, crowPtr + OffsetConfig::CrowPosY(), 0.0f);
                ci.z = SafeRPM<float>(gh->hProcess, crowPtr + OffsetConfig::CrowPosZ(), 0.0f);

                if (ci.serverId != 0 && ci.serverId != 0xFFFFFFFF && ci.hp > 0) {
                    count++;
                }
            }
        }
        nodeAddr = nextNode;
    }

    if (count > 0) {
        return count;
    }

    // 無怪物時返回 0，狀態機需要處理此情況
    return 0;
}

struct PCInfo {
    DWORD serverId;
    float x, y, z;
    DWORD hp, maxHp;
};

static int EnumeratePCs(GameHandle* gh, PCInfo* outPCs, int maxPCs) {
    if (!gh || !gh->hProcess || !gh->baseAddr || !outPCs) return 0;
    DWORD charAddr = GetLocalCharPtrExternal(gh);
    if (!charAddr) return 0;
    DWORD landMan = SafeRPM<DWORD>(gh->hProcess, charAddr + OffsetConfig::EntityLandManPtr(), 0);
    if (!landMan || !IsGoodPtr(landMan)) return 0;
    DWORD headNode = SafeRPM<DWORD>(gh->hProcess, landMan + OffsetConfig::EntityPCList(), 0);
    if (!headNode || !IsGoodPtr(headNode)) return 0;
    int count = 0;
    DWORD nodeAddr = headNode;
    DWORD visited[100];
    int visitCount = 0;
    while (nodeAddr && count < maxPCs && visitCount < 100) {
        bool alreadyVisited = false;
        for (int i = 0; i < visitCount; i++) {
            if (visited[i] == nodeAddr) { alreadyVisited = true; break; }
        }
        if (alreadyVisited) break;
        visited[visitCount++] = nodeAddr;
        DWORD pcPtr = SafeRPM<DWORD>(gh->hProcess, nodeAddr + OffsetConfig::CrowNodeCrowPtr(), 0);
        DWORD nextNode = SafeRPM<DWORD>(gh->hProcess, nodeAddr + OffsetConfig::CrowNodeNext(), 0);
        if (pcPtr && IsGoodPtr(pcPtr)) {
            PCInfo& pi = outPCs[count];
            pi.serverId = SafeRPM<DWORD>(gh->hProcess, pcPtr + OffsetConfig::PCServerID(), 0);
            pi.hp = SafeRPM<DWORD>(gh->hProcess, pcPtr + OffsetConfig::PCHP(), 0);
            pi.maxHp = SafeRPM<DWORD>(gh->hProcess, pcPtr + OffsetConfig::PCMaxHP(), 0);
            pi.x = SafeRPM<float>(gh->hProcess, pcPtr + OffsetConfig::PCPosX(), 0.0f);
            pi.y = SafeRPM<float>(gh->hProcess, pcPtr + OffsetConfig::PCPosY(), 0.0f);
            pi.z = SafeRPM<float>(gh->hProcess, pcPtr + OffsetConfig::PCPosZ(), 0.0f);
            if (pi.serverId != 0 && pi.serverId != 0xFFFFFFFF) {
                count++;
            }
        }
        nodeAddr = nextNode;
    }
    return count;
}
// ============================================================
// 背包快取
// ============================================================
static InvSlot      s_invCache[78];
static int          s_invCacheCount = 0;
static DWORD        s_invCacheTime = 0;
static CRITICAL_SECTION s_invCacheCs;
// ============================================================
// Public bot API
// ============================================================
// Helper for SEH-protected initialization (separated to avoid C2712)
static void InitBotLogic_SafeInits() {
    __try {
        InitAttackSender("210.64.10.55", 6870);
        UIAddLog("[Bot] 攻擊封包發送器已初始化");
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        UIAddLog("[Bot] ⚠️ InitAttackSender 失敗（安全繼續）");
    }

    __try {
        CoordCalibrator::Instance().Load();
        Log("校正", "座標校正已載入");
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("校正", "⚠️ CoordCalibrator::Load 失敗（安全繼續）");
    }

    __try {
        DMVisual::Init();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("DM", "⚠️ DMVisual::Init 失敗（安全繼續）");
    }

    __try {
        InitFSM();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("FSM", "⚠️ InitFSM 失敗（安全繼續）");
    }

    __try {
        VisualSupply::Init();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("視覺", "⚠️ VisualSupply::Init 失敗（安全繼續）");
    }
}

void InitBotLogic() {
    EnsureUICacheReady();
    InitializeCriticalSection(&s_invCacheCs);
    InterlockedExchange(&s_invCacheCsInited, 1);
    g_State.store((int)BotState::IDLE);
    g_cfg.active.store(false);
    s_entityPoolKnownBad = false;
    srand((unsigned int)time(nullptr));
    s_hasLastValidPlayerState = false;
    s_lastValidPlayerStateTime = 0;
    s_consecutiveReadFail = 0;
    s_enteredHunting = false;
    s_wasInHunting = false;
    s_enteredDeadState = false;
    s_loggedDead = false;
    // town_index 只代表補給城鎮選項；MapID 必須是可靠讀值才可用來判斷城鎮。
    {
        // 0 目前是未知值，不可作為城鎮 MapID。
        static const int townMapIdTable[] = { 0, 1, 2, 3 };
        int idx = g_cfg.town_index.load();
        g_cfg.townMapIds.clear();
        if (idx >= 0 && idx <= 3) {
            int mapId = townMapIdTable[idx];
            if (IsReliableTownMapId(mapId)) {
                g_cfg.townMapIds.push_back(mapId);
                Logf("狀態機", "✅ 城鎮地圖 ID 已設置: town_index=%d, mapId=%d",
                     idx, mapId);
            } else {
                Logf("狀態機",
                    "⚠️ 城鎮 MapID=%d 不可信，已停用 StartInTown/MapID 城鎮判斷；請不要用 0 當城鎮 ID",
                    mapId);
            }
        }
    }
    if (g_cfg.protected_item_ids.empty()) {
        for (int i = 0; i < BotConfig::defaultProtectedCount; i++) {
            int id = BotConfig::defaultProtectedItems[i].id;
            if (id != 0) {  // 只添加有效的ID
                g_cfg.protected_item_ids.push_back(id);
            }
        }
        if (!g_cfg.protected_item_ids.empty()) {
            Logf("保護", "已載入 %d 個預設保護物品", (int)g_cfg.protected_item_ids.size());
        } else {
            Log("保護", "請在設定中手動添加保護物品");
        }
    }

    // PEB 反偵測（各有 try/except）
    InitAntiDebugProtection();
    InitAntiDebugHooks();

    // 大漠模式預設啟用
    g_cfg.use_dm_mode.store(true);

    // SEH 保護的初始化
    InitBotLogic_SafeInits();

    // ── 啟動時自動載入並驗證卡密（如果有的話）──
    char cachedToken[4096] = {};
    if (OfflineLicenseLoadCached(cachedToken, sizeof(cachedToken))) {
        Logf("認證", "發現本地緩存卡密，正在驗證...");
        UIAddLog("[License] 載入本地緩存，自動驗證中");

        OfflineLicenseInfo info = {};
        bool ok = OfflineLicenseVerifyToken(cachedToken, NULL, &info);
        if (ok && info.valid) {
            g_licenseValid.store(true);
            Logf("認證", "✅ 自動驗證成功，剩餘 %d 天", info.days_left);
            UIAddLog("[License] 驗入 %d 天", info.days_left);
        } else {
            g_licenseValid.store(false);
            const char* msg = info.message.empty() ? "驗證失敗" : info.message.c_str();
            Logf("認證", "❌ 自動驗證失敗: %s", msg);
            UIAddLog("[License] 驗證失敗: %s", msg);
        }
    } else {
        Log("認證", "無本地緩存卡密，請在 UI 輸入卡密");
        UIAddLog("[License] 無本地緩存，請輸入卡密");
    }

    Log("系統", "Bot 邏輯初始化完成");
}

void ShutdownBotLogic() {
    ShutdownAttackSender();
    // 只刪除已初始化的 CriticalSection
    if (InterlockedCompareExchange(&s_uiCacheCsInit, 0, 0) != 0) {
        DeleteCriticalSection(&s_uiCacheCs);
    }
    if (InterlockedCompareExchange(&s_invCacheCsInited, 0, 0) != 0) {
        DeleteCriticalSection(&s_invCacheCs);
    }
}
bool IsEntityPoolWorking() {
    return !s_entityPoolKnownBad;
}
static InventoryScanStatus ScanInventoryDetailed(GameHandle* gh, std::vector<InvSlot>* out, int* outCount) {
    if (out) out->clear();
    if (outCount) *outCount = 0;
    if (!gh || !gh->hProcess || !gh->baseAddr) {
        return InventoryScanStatus::INVALID_HANDLE;
    }

    DWORD glChar = GetLocalCharPtrExternal(gh);
    if (IsGoodPtr(glChar)) {
        DWORD inv = SafeRPM<DWORD>(gh->hProcess, glChar + OffsetConfig::InvInvPtr(), 0);
        if (IsGoodPtr(inv)) s_invBase = inv;
    }

    if (!IsGoodPtr(s_invBase)) {
        DWORD now = GetTickCount();
        const bool relativeOnly = s_relativeOnlyCombatMode.load();
        DWORD logInterval = relativeOnly ? 15000 : 3000;
        if (now - s_lastInventoryDiagLog > logInterval) {
            Log("背包", relativeOnly
                ? "⚠️ 純相對模式：背包基底暫不可用，跳過背包掃描"
                : "❌ 背包基底無效，跳過背包掃描");
            s_lastInventoryDiagLog = now;
        }
        return InventoryScanStatus::INVALID_BASE;
    }

    DWORD testDword = SafeRPM<DWORD>(gh->hProcess, s_invBase, 0xCDCDCDCD);
    if (testDword == 0xCDCDCDCD || testDword == 0) {
        s_invBase = 0;
        if (IsGoodPtr(glChar)) {
            DWORD inv = SafeRPM<DWORD>(gh->hProcess, glChar + OffsetConfig::InvInvPtr(), 0);
            if (IsGoodPtr(inv)) s_invBase = inv;
        }
        if (!IsGoodPtr(s_invBase)) {
            DWORD now = GetTickCount();
            const bool relativeOnly = s_relativeOnlyCombatMode.load();
            DWORD logInterval = relativeOnly ? 15000 : 3000;
            if (now - s_lastInventoryDiagLog > logInterval) {
                Log("背包", relativeOnly
                    ? "⚠️ 純相對模式：背包基底刷新失敗，暫停背包掃描"
                    : "❌ 背包基底刷新失敗，無法辨識背包內容");
                s_lastInventoryDiagLog = now;
            }
            return InventoryScanStatus::INVALID_BASE;
        }
    }

    int count = 0;
    for (int i = 0; i < OffsetConfig::InvMaxSlots(); ++i) {
        DWORD slotAddr = s_invBase + (DWORD)i * OffsetConfig::InvItemStride();
        DWORD itemId = SafeRPM<DWORD>(gh->hProcess, slotAddr + OffsetConfig::InvItemId(), 0);
        DWORD itemCount = SafeRPM<DWORD>(gh->hProcess, slotAddr + OffsetConfig::InvItemCount(), 0);
        if (!IsLikelyValidInventorySlot(itemId, itemCount)) continue;
        InvSlot slot;
        slot.slotIdx = i;
        slot.itemId = itemId;
        slot.count = itemCount;
        slot.valid = true;
        if (out) out->push_back(slot);
        count++;
    }

    if (outCount) *outCount = count;
    return count > 0 ? InventoryScanStatus::OK : InventoryScanStatus::EMPTY;
}
static bool ValidatePlayerStateData(const PlayerState& st, DWORD charAddr, InventoryScanStatus invStatus,
    char* reason, size_t reasonSize) {
    if (!IsGoodPtr(charAddr)) {
        _snprintf_s(reason, reasonSize, _TRUNCATE, "GLCharacter 指標無效 (0x%08X)", charAddr);
        return false;
    }
    (void)invStatus;
    if (!IsPlausibleMapId(st.mapId)) {
        _snprintf_s(reason, reasonSize, _TRUNCATE, "地圖 ID 無效 (%d)", st.mapId);
        return false;
    }
    if (!HasUsableWorldPos(st.x, st.z)) {
        _snprintf_s(reason, reasonSize, _TRUNCATE, "座標無效 (x=%.1f z=%.1f)", st.x, st.z);
        return false;
    }
    if (st.maxHp <= 0 || st.maxMp <= 0 || st.maxSp <= 0) {
        _snprintf_s(reason, reasonSize, _TRUNCATE, "最大屬性無效 HP/MP/SP=%d/%d/%d",
            st.maxHp, st.maxMp, st.maxSp);
        return false;
    }
    // 允許死亡狀態（hp=0 但 maxHp > 0）
    // 死亡時 HP=0 是正常狀態，不應視為讀取失敗
    // 只有當 hp < 0（不合理）或 hp > maxHp * 20（越界）才是無效
    bool hpInvalid = (st.hp < 0) || (st.hp > 0 && st.hp > st.maxHp * 20);
    bool mpInvalid = (st.mp < 0) || (st.mp > 0 && st.mp > st.maxMp * 20);
    bool spInvalid = (st.sp < 0) || (st.sp > 0 && st.sp > st.maxSp * 20);
    if (hpInvalid || mpInvalid || spInvalid) {
        _snprintf_s(reason, reasonSize, _TRUNCATE, "屬性數值越界 HP=%d/%d MP=%d/%d SP=%d/%d",
            st.hp, st.maxHp, st.mp, st.maxMp, st.sp, st.maxSp);
        return false;
    }
    return true;
}
static PlayerStateReadStatus ReadPlayerStateDetailedInternal(GameHandle* gh, PlayerState* out,
    char* reason, size_t reasonSize, bool allowRefresh) {
    if (!out) return PlayerStateReadStatus::READ_FAILED;

    *out = PlayerState{};
    out->maxHp = out->maxMp = out->maxSp = 1;
    out->state = (BotState)g_State.load();
    if (reason && reasonSize > 0) reason[0] = '\0';

    if (!gh || !gh->hProcess) {
        static DWORD s_lastDiag = 0;
        if (GetTickCount() - s_lastDiag > 3000) {
            Logf("讀取", "❌ ReadPlayerState: gh=%p hProcess=%p baseAddr=0x%08X",
                gh, gh ? gh->hProcess : 0, gh ? gh->baseAddr : 0);
            s_lastDiag = GetTickCount();
        }
        if (reason && reasonSize > 0) {
            _snprintf_s(reason, reasonSize, _TRUNCATE, "GameHandle 未就緒");
        }
        return PlayerStateReadStatus::READ_FAILED;
    }

    // ── baseAddr==0 時的兜底 refresh ──
    if (!gh->baseAddr) {
        static DWORD s_lastBaseRefreshDiag = 0;
        DWORD refreshed = RefreshGameBaseAddress(gh);
        if (refreshed) {
            gh->baseAddr = refreshed;
            gh->attached = true;
            if (reason && reasonSize > 0) {
                _snprintf_s(reason, reasonSize, _TRUNCATE, "baseAddr 刷新成功: 0x%08X", refreshed);
            }
            if (GetTickCount() - s_lastBaseRefreshDiag > 3000) {
                Logf("讀取", "✅ baseAddr 刷新成功: 0x%08X", refreshed);
                s_lastBaseRefreshDiag = GetTickCount();
            }
        } else {
            if (reason && reasonSize > 0) {
                _snprintf_s(reason, reasonSize, _TRUNCATE, "baseAddr 刷新失敗");
            }
            return PlayerStateReadStatus::READ_FAILED;
        }
    }

    DWORD base = gh->baseAddr;
    DWORD charAddr = GetLocalCharPtrExternal(gh);
    if (charAddr && IsGoodPtr(charAddr)) {
        char nameBuf[22] = {0};
        SIZE_T bytesRead = 0;
        DWORD_PTR nameAddr = (DWORD_PTR)charAddr + 0x050;
        if (ReadProcessMemory(gh->hProcess, reinterpret_cast<LPCVOID>(nameAddr), nameBuf, 21, &bytesRead) && bytesRead > 0) {
            nameBuf[21] = '\0';
            memcpy(out->name, nameBuf, 22);
        } else {
            // 修復：strcpy_s(NULL) 會崩潰
            if (out && out->name) strcpy_s(out->name, sizeof(out->name), "???");
        }
    } else {
        // charAddr 無效時也要填預設值
        if (out && out->name) strcpy_s(out->name, sizeof(out->name), "???");
    }

    out->hp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerHP(), 0);
    out->maxHp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMaxHP(), 0);
    out->mp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMP(), 0);
    out->maxMp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMaxMP(), 0);
    out->sp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerSP(), 0);
    out->maxSp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMaxSP(), 0);
    out->gold = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerGold(), 0);
    out->level = SafeRPM<WORD>(gh->hProcess, base + OffsetConfig::PlayerLevel(), 0);
    out->exp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerEXP(), 0);
    out->expMax = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerEXPMax(), 0);
    out->combatPower = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerCombatPower(), 0);
    out->str = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerSTR(), 0);
    out->vit = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerVIT(), 0);
    out->spr = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerSPR(), 0);
    out->dex = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerDEX(), 0);
    out->end = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerEND(), 0);
    out->physAtkMin = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerPhysAtkMin(), 0);
    out->physAtkMax = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerPhysAtkMax(), 0);
    out->sprAtkMin = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerSprAtkMin(), 0);
    out->sprAtkMax = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerSprAtkMax(), 0);
    out->arrowCount = SafeRPM<int>(gh->hProcess, base + OffsetConfig::QuickSlotArrowCount(), 0);
    out->talismanCount = SafeRPM<int>(gh->hProcess, base + OffsetConfig::QuickSlotTalismanCount(), 0);
    out->mapId = SafeReadMapID(gh->hProcess, base + OffsetConfig::PlayerMapID());

    // 調試日誌：30秒一次，減少噪音
    static DWORD s_lastDebugTime = 0;
    DWORD now = GetTickCount();
    bool shouldLog = (now - s_lastDebugTime > 30000);

    bool posResolved = false;
    DWORD charAddrForDebug = GetLocalCharPtrExternal(gh);
    if (shouldLog) {
        Logf("讀取", "DEBUG: charAddr=0x%08X, IsGoodPtr=%d, base=0x%08X, GLCharObj=0x%08X",
            charAddrForDebug, IsGoodPtr(charAddrForDebug) ? 1 : 0, base, OffsetConfig::GLCharacterObj());
        s_lastDebugTime = now;
    }

    if (charAddr && IsGoodPtr(charAddr)) {
        float cx = SafeRPM<float>(gh->hProcess, charAddr + OffsetConfig::CrowPosX(), 0.0f);
        float cz = SafeRPM<float>(gh->hProcess, charAddr + OffsetConfig::CrowPosZ(), 0.0f);
        if (shouldLog) {
            Logf("讀取", "DEBUG: from charAddr+0x890: cx=%.1f cz=%.1f", cx, cz);
        }
        if (HasUsableWorldPos(cx, cz)) {
            out->x = cx;
            out->z = cz;
            out->y = SafeRPM<float>(gh->hProcess, charAddr + OffsetConfig::CrowPosY(), 0.0f);
            posResolved = true;
        }
    }
    if (!posResolved) {
        float wx = SafeRPM<float>(gh->hProcess, base + OffsetConfig::PlayerPosX(), 0.0f);
        float wz = SafeRPM<float>(gh->hProcess, base + OffsetConfig::PlayerPosZ(), 0.0f);
        if (shouldLog) {
            Logf("讀取", "DEBUG: from base+0x92F550: wx=%.1f wz=%.1f", wx, wz);
        }
        if (HasUsableWorldPos((float)wx, (float)wz)) {
            out->x = wx;
            out->z = wz;
            float wy = SafeRPM<float>(gh->hProcess, base + OffsetConfig::PlayerPosY(), 0.0f);
            out->y = wy;
            posResolved = true;
        }
    }
    if (!posResolved) {
        PCInfo pcs[20];
        int pcCount = EnumeratePCs(gh, pcs, 20);
        int myHP = out->hp;
        for (int i = 0; i < pcCount; i++) {
            if ((int)pcs[i].hp == myHP && HasUsableWorldPos(pcs[i].x, pcs[i].z)) {
                out->x = pcs[i].x;
                out->y = pcs[i].y;
                out->z = pcs[i].z;
                posResolved = true;
                break;
            }
        }
        if (!posResolved && pcCount > 0) {
            for (int i = 0; i < pcCount; i++) {
                if (HasUsableWorldPos(pcs[i].x, pcs[i].z)) {
                    out->x = pcs[i].x;
                    out->y = pcs[i].y;
                    out->z = pcs[i].z;
                    posResolved = true;
                    break;
                }
            }
        }
    }

    out->targetId = SafeRPM<DWORD>(gh->hProcess, base + OffsetConfig::TargetID(), 0xFFFFFFFF);
    out->hasTarget = SafeRPM<int>(gh->hProcess, base + OffsetConfig::TargetHasTarget(), 0);
    out->skillLockState = SafeRPM<int>(gh->hProcess, base + OffsetConfig::TargetLockedState(), 0);
    out->attackCount = SafeRPM<int>(gh->hProcess, base + 0x724, 0);
    out->attackRange = SafeRPM<int>(gh->hProcess, base + 0x728, 0);

    // ============================================================
    // 庫存讀取（使用明確錯誤碼）
    // ============================================================

    int inventoryCount = 0;
    InventoryScanStatus invStatus = ScanInventoryDetailed(gh, NULL, &inventoryCount);
    out->inventoryCount = (invStatus == InventoryScanStatus::INVALID_BASE ||
        invStatus == InventoryScanStatus::INVALID_HANDLE) ? -1 : inventoryCount;

    char localReason[160] = {0};
    if (!ValidatePlayerStateData(*out, charAddr, invStatus, localReason, sizeof(localReason))) {
        DWORD refreshed = 0;
        if (allowRefresh) {
            refreshed = GetGameBaseAddress(gh);
        }
        if (allowRefresh && refreshed && refreshed != gh->baseAddr) {
            gh->baseAddr = refreshed;
            if (gh->attached) {
                SetGameHandle(gh);
            }
            return ReadPlayerStateDetailedInternal(gh, out, reason, reasonSize, false);
        }

        if (reason && reasonSize > 0) {
            strncpy_s(reason, reasonSize, localReason, _TRUNCATE);
        }
        DWORD now = GetTickCount();
        const bool relativeOnly = s_relativeOnlyCombatMode.load();
        DWORD logInterval = relativeOnly ? 15000 : 3000;
        if (now - s_lastInvalidStateLog > logInterval) {
            Logf("讀取", "%s: %s [GLChar=0x%08X Map=%d x=%.1f z=%.1f HP=%d/%d Inv=%s Offset=%s MapRVA=0x%08X PosXRVA=0x%08X PosZRVA=0x%08X]",
                relativeOnly ? "⚠️ 純相對模式：記憶體座標暫不可用" : "⚠️ 玩家資料無效",
                localReason, charAddr, out->mapId, out->x, out->z, out->hp, out->maxHp,
                GetInventoryScanStatusName(invStatus), OffsetConfig::GetLoadSource(),
                OffsetConfig::PlayerMapID(), OffsetConfig::PlayerPosX(), OffsetConfig::PlayerPosZ());
            s_lastInvalidStateLog = now;
        }
        return PlayerStateReadStatus::INVALID_DATA;
    }

    if (invStatus == InventoryScanStatus::INVALID_BASE || invStatus == InventoryScanStatus::INVALID_HANDLE) {
        DWORD now = GetTickCount();
        DWORD logInterval = s_relativeOnlyCombatMode.load() ? 15000 : 5000;
        if (now - s_lastInventoryDiagLog > logInterval) {
            Logf("讀取", "⚠️ 背包狀態未知 (%s)，戰鬥可繼續，補給判定暫停",
                GetInventoryScanStatusName(invStatus));
            s_lastInventoryDiagLog = now;
        }
    }

    s_lastValidPlayerState = *out;
    s_lastValidPlayerStateTime = GetTickCount();
    s_hasLastValidPlayerState = true;
    UpdateUICache(*out);
    return PlayerStateReadStatus::OK;
}
static PlayerStateReadStatus ReadPlayerStateDetailed(GameHandle* gh, PlayerState* out,
    char* reason, size_t reasonSize) {
    static DWORD s_lastCallDiag = 0;
    DWORD now = GetTickCount();
    DWORD logInterval = s_relativeOnlyCombatMode.load() ? 10000 : 3000;
    if (now - s_lastCallDiag > logInterval) {
        Logf("讀取", "📍 ReadPlayerStateDetailed called: gh=%p hProcess=%p baseAddr=0x%08X",
            (void*)gh, gh ? (void*)gh->hProcess : NULL, gh ? gh->baseAddr : 0);
        s_lastCallDiag = now;
    }
    return ReadPlayerStateDetailedInternal(gh, out, reason, reasonSize, true);
}
bool ReadPlayerState(GameHandle* gh, PlayerState* out) {
    return ReadPlayerStateDetailed(gh, out, NULL, 0) == PlayerStateReadStatus::OK;
}
static int ReadEntitiesFromCROwList(GameHandle* gh, std::vector<Entity>* monsters, std::vector<Entity>* npcs, const PlayerState& ps) {
    if (!gh || !gh->hProcess || !gh->baseAddr) return 0;
    (void)npcs;
    CrowInfo crows[200];
    int crowCount = EnumerateCrows(gh, crows, OffsetConfig::EntityMaxCrows());
    int total = 0;
    for (int i = 0; i < crowCount; i++) {
        Entity e;
        e.id = crows[i].serverId;
        e.type = 2;
        e.x = crows[i].x;
        e.y = crows[i].y;
        e.z = crows[i].z;
        e.hp = (int)crows[i].hp;
        e.maxHp = (crows[i].maxHp > 0) ? (int)crows[i].maxHp : e.hp;
        if (e.maxHp <= 0) e.maxHp = 1;
        e.isDead = e.hp <= 0;
        e.dist = Distance2D(ps.x, ps.z, e.x, e.z);
        if (monsters) monsters->push_back(e);
        total++;
    }
    return total;
}
int ScanEntities(GameHandle* gh, std::vector<Entity>* monsters, std::vector<Entity>* npcs) {
    if (monsters) monsters->clear();
    if (npcs) npcs->clear();
    if (!IsLicenseValid()) return 0;
    PlayerState ps;
    if (!ReadPlayerState(gh, &ps)) return 0;
    return ReadEntitiesFromCROwList(gh, monsters, npcs, ps);
}
int ScanInventory(GameHandle* gh, std::vector<InvSlot>* out) {
    int count = 0;
    InventoryScanStatus status = ScanInventoryDetailed(gh, out, &count);
    return (status == InventoryScanStatus::OK || status == InventoryScanStatus::EMPTY) ? count : 0;
}
bool IsInventoryFull(GameHandle* gh) {
    int count = 0;
    InventoryScanStatus status = ScanInventoryDetailed(gh, NULL, &count);
    if (status != InventoryScanStatus::OK && status != InventoryScanStatus::EMPTY) return false;
    int pct = (count * 100) / OffsetConfig::InvMaxSlots();
    return pct >= g_cfg.inventory_full_pct.load();
}

bool IsPotionSlotsLow(GameHandle* gh) {
    if (!g_cfg.potion_check_enable.load()) return false;

    std::vector<InvSlot> slots;
    int count = 0;
    InventoryScanStatus status = ScanInventoryDetailed(gh, &slots, &count);
    if (status != InventoryScanStatus::OK && status != InventoryScanStatus::EMPTY) return false;

    int start = g_cfg.potion_slot_start.load();
    int end = g_cfg.potion_slot_end.load();
    int minSlots = g_cfg.min_potion_slots.load();
    if (minSlots < 1) minSlots = 1;

    int occupiedCount = 0;
    for (const auto& slot : slots) {
        if (slot.slotIdx >= start && slot.slotIdx <= end && slot.itemId != 0) {
            occupiedCount++;
        }
    }
    return occupiedCount < minSlots;
}
void DumpInventoryItems(GameHandle* gh) {
    if (!gh || !gh->hProcess) return;
    std::vector<InvSlot> items;
    int count = ScanInventory(gh, &items);
    Logf("背包", "===== 背包物品 (共%d個) =====", count);
    for (const auto& item : items) {
        bool isProtected = false;
        EnterCriticalSection(&g_cfg.cs_protected);
        for (int pid : g_cfg.protected_item_ids) {
            if (pid == (int)item.itemId && pid != 0) { isProtected = true; break; }
        }
        LeaveCriticalSection(&g_cfg.cs_protected);
        Logf("背包", "格%d: ID=0x%X (%d個)%s",
            item.slotIdx, item.itemId, item.count,
            isProtected ? " [已保護]" : "");
    }
    Log("背包", "===== 掃描完成 =====");
}
static InvSlot s_guiInvCache[78];
static int s_guiInvCount = 0;
static CRITICAL_SECTION s_guiInvCs;
static volatile LONG s_guiInvCsInit = 0;
// Win7兼容的延遲初始化
static void EnsureGuiInvCsReady() {
    if (InterlockedCompareExchange(&s_guiInvCsInit, 1, 0) == 0) {
        InitializeCriticalSection(&s_guiInvCs);
    }
}
int ScanInventoryForGui() {
    EnsureGuiInvCsReady();
    EnterCriticalSection(&s_guiInvCs);
    if (!IsLicenseValid()) {
        s_guiInvCount = 0;
        LeaveCriticalSection(&s_guiInvCs);
        return 0;
    }
    GameHandle gh = GetGameHandle();
    std::vector<InvSlot> items;
    s_guiInvCount = ScanInventory(&gh, &items);
    int n = (int)items.size();
    if (n > 78) n = 78;
    for (int i = 0; i < n; i++) {
        s_guiInvCache[i] = items[i];
    }
    LeaveCriticalSection(&s_guiInvCs);
    return s_guiInvCount;
}
bool GetGuiInvSlot(int idx, int* outItemId, int* outCount) {
    if (idx < 0 || idx >= s_guiInvCount) return false;
    if (outItemId) *outItemId = (int)s_guiInvCache[idx].itemId;
    if (outCount) *outCount = (int)s_guiInvCache[idx].count;
    return true;
}
void AddProtectedItem(int itemId) {
    EnterCriticalSection(&g_cfg.cs_protected);
    for (int id : g_cfg.protected_item_ids) {
        if (id == itemId) { LeaveCriticalSection(&g_cfg.cs_protected); return; }
    }
    g_cfg.protected_item_ids.push_back(itemId);
    LeaveCriticalSection(&g_cfg.cs_protected);
    Logf("保護", "已添加物品ID=0x%X 到保護列表", itemId);
}
void RemoveProtectedItem(int itemId) {
    EnterCriticalSection(&g_cfg.cs_protected);
    for (auto it = g_cfg.protected_item_ids.begin(); it != g_cfg.protected_item_ids.end(); ) {
        if (*it == itemId) {
            it = g_cfg.protected_item_ids.erase(it);
            Logf("保護", "已移除物品ID=0x%X", itemId);
        } else {
            ++it;
        }
    }
    LeaveCriticalSection(&g_cfg.cs_protected);
}
int GetProtectedItemCount() {
    int count = 0;
    EnterCriticalSection(&g_cfg.cs_protected);
    count = (int)g_cfg.protected_item_ids.size();
    LeaveCriticalSection(&g_cfg.cs_protected);
    return count;
}
int GetProtectedItemId(int index) {
    int id = 0;
    EnterCriticalSection(&g_cfg.cs_protected);
    if (index >= 0 && index < (int)g_cfg.protected_item_ids.size()) {
        id = g_cfg.protected_item_ids[index];
    }
    LeaveCriticalSection(&g_cfg.cs_protected);
    return id;
}
bool IsRowProtected(int row) {
    if (row < 0 || row >= BotConfig::MAX_INVENTORY_ROWS) return false;
    return g_cfg.protected_rows[row];
}
void SetRowProtected(int row, bool protect) {
    if (row >= 0 && row < BotConfig::MAX_INVENTORY_ROWS) {
        g_cfg.protected_rows[row] = protect;
    }
}
int GetProtectedRowCount() {
    int count = 0;
    for (int i = 0; i < BotConfig::MAX_INVENTORY_ROWS; i++) {
        if (g_cfg.protected_rows[i]) count++;
    }
    return count;
}
int GetCurrentSkillIndex() {
    return g_cfg.currentSkillIndex.load();
}
int GetHuntPointIndex() {
    return s_huntPointIndex;
}
int GetHuntPointCount() {
    EnterCriticalSection(&g_cfg.cs_protected);
    int count = (int)g_cfg.huntWaypoints.size();
    LeaveCriticalSection(&g_cfg.cs_protected);
    return count > 0 ? count : 1;
}
int GetCombatIntentState() {
    return (int)s_combatIntent;
}
DWORD GetKillCount() {
    return s_killCount;
}
void GetPlayerName(char* outName, int maxLen) {
    if (!outName || maxLen <= 0) return;
    outName[0] = '\0';
    GameHandle gh = GetGameHandle();
    if (!gh.attached || !gh.hProcess) return;
    DWORD charAddr = GetLocalCharPtrExternal(&gh);
    if (!charAddr) return;
    SIZE_T bytesRead = 0;
    int readLen = (maxLen < 21) ? maxLen : 21;

    // 先讀取原始位元組
    char rawName[22] = {0};
    ReadProcessMemory(gh.hProcess, (LPCVOID)(charAddr + OffsetConfig::PlayerName()),
                     rawName, readLen, &bytesRead);

    // 轉換編碼：Big5/GBK -> UTF-8
    int wideLen = MultiByteToWideChar(950, 0, rawName, -1, NULL, 0);  // 950 = Big5
    if (wideLen > 0) {
        wchar_t* wideBuf = new wchar_t[wideLen + 1];
        MultiByteToWideChar(950, 0, rawName, -1, wideBuf, wideLen);
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wideBuf, -1, NULL, 0, NULL, NULL);
        if (utf8Len > 0 && utf8Len < maxLen) {
            WideCharToMultiByte(CP_UTF8, 0, wideBuf, -1, outName, utf8Len, NULL, NULL);
        } else {
            WideCharToMultiByte(CP_UTF8, 0, wideBuf, -1, outName, maxLen, NULL, NULL);
        }
        delete[] wideBuf;
    } else {
        // fallback：直接複製
        strncpy_s(outName, maxLen, rawName, maxLen - 1);
    }
    outName[maxLen - 1] = '\0';
}
void CacheInventory(GameHandle* gh) {
    EnterCriticalSection(&s_invCacheCs);
    s_invCacheCount = 0;
    s_invCacheTime = 0;

    // ── 嘗試 NetHook 共享記憶體（若可用）──
    if (NetHookShmem_IsConnected()) {
        ShmemInvItem items[78];
        int count = NetHookShmem_EnumerateInventory(items, 78);
        if (count > 0) {
            for (int i = 0; i < count && s_invCacheCount < 78; i++) {
                s_invCache[s_invCacheCount].slotIdx = items[i].slot;
                s_invCache[s_invCacheCount].itemId = items[i].itemId;
                s_invCache[s_invCacheCount].count = items[i].count;
                s_invCache[s_invCacheCount].valid = (items[i].itemId != 0);
                s_invCacheCount++;
            }
            s_invCacheTime = GetTickCount();
            LeaveCriticalSection(&s_invCacheCs);
            return;
        }
    }

    // ── Fallback: 直接記憶體掃描 ──
    std::vector<InvSlot> slots;
    ScanInventory(gh, &slots);
    for (size_t i = 0; i < slots.size() && i < 78; i++) {
        s_invCache[i] = slots[i];
        s_invCacheCount++;
    }
    s_invCacheTime = GetTickCount();
    LeaveCriticalSection(&s_invCacheCs);
}
int GetCachedInvCount() {
    EnterCriticalSection(&s_invCacheCs);
    int c = s_invCacheCount;
    LeaveCriticalSection(&s_invCacheCs);
    return c;
}
bool GetCachedInvSlot(int idx, int* outItemId, int* outCount) {
    EnterCriticalSection(&s_invCacheCs);
    bool ok = false;
    if (idx >= 0 && idx < s_invCacheCount && idx < 78) {
        if (outItemId) *outItemId = (int)s_invCache[idx].itemId;
        if (outCount) *outCount = (int)s_invCache[idx].count;
        ok = s_invCache[idx].valid;
    }
    LeaveCriticalSection(&s_invCacheCs);
    return ok;
}
DWORD GetCachedInvTime() {
    return s_invCacheTime;
}
bool FindNearestMonster(GameHandle* gh, Entity* out) {
    if (!gh || !out) return false;
    int range = g_cfg.attack_range.load();
    std::vector<Entity> monsters;
    ScanEntities(gh, &monsters, NULL);
    if (monsters.empty()) return false;
    Entity* best = nullptr;
    float bestScore = -1.0f;
    for (auto& e : monsters) {
        if (e.isDead || e.hp <= 0) continue;
        if (e.dist > (float)range) continue;
        float hpRatio = (e.maxHp > 0) ? (float)e.hp / (float)e.maxHp : 1.0f;
        float hpScore = (1.0f - hpRatio) * 50.0f;
        float distScore = (1.0f / (e.dist + 1.0f)) * 30.0f;
        float score = hpScore + distScore;
        if (score > bestScore) {
            bestScore = score;
            best = &e;
        }
    }
    if (best) {
        *out = *best;
        Logf("戰鬥", "目標: ID=0x%X, dist=%.1f, HP=%d/%d (score=%.1f)",
            out->id, out->dist, out->hp, out->maxHp, bestScore);
        return true;
    }
    return false;
}
bool FindNearestNPC(GameHandle* gh, Entity* out) {
    std::vector<Entity> npcs;
    ScanEntities(gh, NULL, &npcs);
    if (npcs.empty()) return false;
    auto it = std::min_element(npcs.begin(), npcs.end(),
        [](const Entity& a, const Entity& b) { return a.dist < b.dist; });
    if (it == npcs.end()) return false;
    if (out) *out = *it;
    return true;
}
bool GetMonsterById(GameHandle* gh, DWORD id, Entity* out) {
    std::vector<Entity> monsters;
    ScanEntities(gh, &monsters, NULL);
    for (const Entity& e : monsters) {
        if (e.id == id) {
            if (out) *out = e;
            return true;
        }
    }
    return false;
}
bool IsTargetDying(GameHandle* gh) {
    PlayerState st;
    if (!ReadPlayerState(gh, &st)) return false;
    if (!st.hasTarget || st.targetId == 0xFFFFFFFF || st.targetId == 0) return false;
    Entity e;
    if (!GetMonsterById(gh, st.targetId, &e)) return false;
    return e.isDead || (e.maxHp > 0 && e.hp <= 0);
}
BotState GetBotState() {
    return (BotState)g_State.load();
}
static const char* GetStateName(BotState s) {
    switch (s) {
        case BotState::IDLE: return "IDLE";
        case BotState::HUNTING: return "HUNTING";
        case BotState::DEAD: return "DEAD";
        case BotState::RETURNING: return "RETURNING";
        case BotState::TOWN_SUPPLY: return "TOWN_SUPPLY";
        case BotState::BACK_TO_FIELD: return "BACK_TO_FIELD";
        case BotState::TRAVELING: return "TRAVELING";
        case BotState::PAUSED: return "PAUSED";
        case BotState::RANDOM_EVADE: return "RANDOM_EVADE";  // 逃生/脫困狀態
        case BotState::EMERGENCY_STOP: return "EMERGENCY_STOP";  // VLM 看門狗觸發
        case BotState::RECOVERY: return "RECOVERY";  // VLM 驅動脫困
        default: return "UNKNOWN";
    }
}
static void BuildStateSnapshot(const PlayerState* st, char* buf, size_t bufSize) {
    if (!buf || bufSize == 0) return;
    if (!st) {
        strncpy_s(buf, bufSize, "Map=? x=? z=?", _TRUNCATE);
        return;
    }
    _snprintf_s(buf, bufSize, _TRUNCATE, "Map=%d x=%.1f z=%.1f HP=%d/%d",
        st->mapId, st->x, st->z, st->hp, st->maxHp);
}
void ResetCombatRuntimeState() {
    s_currentTargetId.store(0);
    s_combatIntent.store((int)CombatIntent::SEEKING);
    s_relativeOnlyCombatMode.store(false);
    g_cfg.currentSkillIndex.store(0);
    g_cfg.lastRightClickTime.store(0);
    g_cfg.lastSkillTime.store(0);
}
static bool CanUseRelativeOnlyCombat(GameHandle* gh) {
    return gh && gh->attached && gh->hWnd && IsWindow(gh->hWnd);
}
static void SetRelativeOnlyCombatMode(bool enabled, const char* reason) {
    bool previous = s_relativeOnlyCombatMode.exchange(enabled);
    if (previous == enabled) return;

    if (enabled) {
        Logf("戰鬥", "↪️ 啟用純相對座標戰鬥模式: %s",
            reason && reason[0] ? reason : "玩家資料不可用");
        UIAddLog("[Bot] 已切換為純相對座標戰鬥模式 (%s)",
            reason && reason[0] ? reason : "玩家資料不可用");
    } else {
        Logf("戰鬥", "↩️ 關閉純相對座標戰鬥模式: %s",
            reason && reason[0] ? reason : "玩家資料已恢復");
        UIAddLog("[Bot] 已恢復一般戰鬥模式 (%s)",
            reason && reason[0] ? reason : "玩家資料已恢復");
    }
}
static void TransitionState(BotState nextState, const char* reason, const PlayerState* st) {
    EnsureUICacheReady();  // 確保鎖已初始化
    EnterCriticalSection(&s_stateTransitionCs);
    BotState oldState = (BotState)g_State.load();
    if (oldState == nextState) {
        LeaveCriticalSection(&s_stateTransitionCs);
        return;
    }
    IStateHandler* oldHandler = StateHandlerRegistry::Instance().Get(oldState);
    IStateHandler* nextHandler = StateHandlerRegistry::Instance().Get(nextState);

    const PlayerState* snapshot = st ? st : (s_hasLastValidPlayerState ? &s_lastValidPlayerState : NULL);
    char snapBuf[128];
    BuildStateSnapshot(snapshot, snapBuf, sizeof(snapBuf));

    DWORD now = GetTickCount();
    DWORD lastValidAge = s_lastValidPlayerStateTime ? (now - s_lastValidPlayerStateTime) : 0;

    if (oldState == BotState::HUNTING) {
        s_enteredHunting = false;
        s_wasInHunting = false;
    }
    if (oldState == BotState::DEAD) {
        s_enteredDeadState = false;
        s_loggedDead = false;
    }

    if (oldHandler) {
        oldHandler->OnExit();
    }

    g_State.store((int)nextState);

    Logf("狀態轉換", "[狀態機] %s -> %s [原因: %s]",
        GetStateName(oldState), GetStateName(nextState), reason ? reason : "n/a");
    Logf("狀態轉換", "  └- 資料: %s | 最近有效讀值: %s | 偏移來源: %s",
        snapBuf,
        s_lastValidPlayerStateTime ? "有" : "無",
        OffsetConfig::GetLoadSource());
    if (s_lastValidPlayerStateTime) {
        Logf("狀態轉換", "  └- 最近有效讀值距今: %lu ms", (unsigned long)lastValidAge);
    }

    switch (nextState) {
        case BotState::RETURNING:
            Log("狀態機", "  └- RETURNING: 交由 ReturningHandler 發送起點卡");
            break;
        case BotState::TOWN_SUPPLY:
            Log("狀態機", "  └- TOWN_SUPPLY: 啟動視覺補給");
            break;
        case BotState::BACK_TO_FIELD:
            Log("狀態機", "  └- BACK_TO_FIELD: 交由 BackToFieldHandler 發送前點卡");
            break;
        case BotState::HUNTING:
            ResetCombatRuntimeState();
            s_enteredHunting = false;
            s_wasInHunting = false;
            Log("狀態機", "  └- HUNTING: 重置戰鬥意向與技能輪替");
            break;
        case BotState::DEAD:
            s_deathStartTime = now;  // ✅ R2 FIX: 用 TransitionState 裡已存在的 now（而非延後到 TickDead）
            s_deadRecoverySeenTime = 0;
            s_reviveClicked = false;
            s_reviveRetryCount = 0;
            s_enteredDeadState = false;
            s_loggedDead = false;
            Log("狀態機", "  └- DEAD: 記錄死亡時間，重置復活狀態");
            break;
        case BotState::IDLE:
            VisualSupply::Reset();
            ResetCombatRuntimeState();
            s_deadRecoverySeenTime = 0;
            s_reviveClicked = false;
            s_reviveRetryCount = 0;
            Log("狀態機", "  └- IDLE: 清空戰鬥/補給殘留狀態");
            break;
        case BotState::PAUSED:
            s_pausedPreviousState = oldState;  // 記住 PAUSED 之前的狀態
            Logf("狀態機", "  └- PAUSED: 轉轉樂安全碼已暫停（之前: %s）", GetStateName(s_pausedPreviousState));
            break;
        case BotState::RECOVERY:
            InitRecoverySystem();
            Log("狀態機", "  └- RECOVERY: VLM 驅動脫困系統已啟動");
            break;
        default:
            break;
    }
    if (nextHandler) {
        nextHandler->OnEnter(snapshot);
    }
    LeaveCriticalSection(&s_stateTransitionCs);
}
void SetBotState(BotState s) {
    TransitionState(s, "SetBotState", NULL);
}
BotConfig* GetBotConfig() {
    return &g_cfg;
}
void StopBot() {
    if (s_stopInProgress) return;
    BotState prevState = (BotState)g_State.load();
    if (!g_cfg.active.load() && prevState == BotState::IDLE) return;
    s_stopInProgress = true;
    Log("狀態機", "========================================");
    Logf("狀態機", "[STOP] Bot 停止！");
    Logf("狀態機", "  └- 之前狀態: %s -> IDLE", GetStateName(prevState));
    Log("狀態機", "  └- 動作: 停止所有 Bot 活動");
    Log("狀態機", "========================================");
    g_cfg.active.store(false);
    TransitionState(BotState::IDLE, "StopBot", NULL);
    s_stopInProgress = false;
}
void ForceStopBot() {
    StopBot();
}
void RequestRecovery(const char* reason) {
    BotState cur = (BotState)g_State.load();
    if (cur == BotState::RECOVERY || cur == BotState::EMERGENCY_STOP) {
        Logf("Recovery", "RequestRecovery: 已在 %s，忽略", GetStateName(cur));
        return;
    }
    Logf("Recovery", "[RequestRecovery] 請求進入 Recovery: %s", reason ? reason : "未知原因");
    TransitionState(BotState::RECOVERY, "RequestRecovery", NULL);
}

// === 新版啟動邏輯（強制相對模式 + 避免卡死）===
void ToggleBotActive() {
    if (g_cfg.active.load()) {
        g_cfg.active.store(false);
        TransitionState(BotState::IDLE, "UserStop", NULL);
        UIAddLog("[Bot] 已停止 (F11)");
        return;
    }

    // 強制使用純相對座標模式（不管記憶體讀不讀得到）
    SetRelativeOnlyCombatMode(true, "使用者強制啟動");

    Log("狀態機", "========================================");
    Log("狀態機", "[開始] 強制純相對座標模式啟動（忽略記憶體讀取錯誤）");
    Log("狀態機", "========================================");

    g_cfg.active.store(true);

    // 直接進入 HUNTING，不走 TOWN_SUPPLY 判斷
    TransitionState(BotState::HUNTING, "ForceRelativeStart", NULL);
    UIAddLog("[Bot] 已開始（純相對座標模式）(F11)");
}
void ResetBotTarget() {
    ResetCombatRuntimeState();
}
// ============================================================
// 意圖模式控制（F1/F2 切換）
// ============================================================
IntentMode GetIntentMode() {
    return g_cfg.intentMode.load();
}
void SetIntentMode(IntentMode mode) {
    g_cfg.intentMode.store(mode);
    const char* modeName = GetIntentModeName(mode);
    Logf("意圖", "切換到 %s 模式", modeName);
    UIAddLog("[意圖] %s 模式", modeName);
}
const char* GetIntentModeName(IntentMode mode) {
    return (mode == IntentMode::COMBAT) ? "攻擊" : "輔助";
}
void CycleCombatIntent() {
    DWORD now = GetTickCount();
    DWORD interval = (DWORD)g_cfg.intentCycleIntervalMs.load();
    static DWORD s_lastCycleTime = 0;
    if (now - s_lastCycleTime < interval) return;
    s_lastCycleTime = now;

    g_cfg.intentMode.store(IntentMode::COMBAT);
    int nextIntent = ((int)s_combatIntent.load() + 1) % 3;
    s_combatIntent.store(nextIntent);

    const char* intentNames[] = {"SEEKING", "ENGAGING", "LOOTING"};
    Logf("意圖", "[F1] 戰鬥意向: %s", intentNames[nextIntent]);
    UIAddLog("[F1] 戰鬥意向: %s", intentNames[nextIntent]);
}
void CycleSupportSkill(int delta) {
    g_cfg.intentMode.store(IntentMode::SUPPORT);
    int current = g_cfg.selectedSupportSkill.load();
    int newSkill = (current + delta + 10) % 10;
    g_cfg.selectedSupportSkill.store(newSkill);
    Logf("意圖", "[F2] 選擇輔助技能 %d", newSkill + 1);
    UIAddLog("[F2] 輔助技能: %d", newSkill + 1);
}
void CycleCombatSkill(int delta) {
    g_cfg.intentMode.store(IntentMode::COMBAT);
    int current = g_cfg.selectedCombatSkill.load();
    int newSkill = (current + delta + 10) % 10;
    g_cfg.selectedCombatSkill.store(newSkill);
    Logf("意圖", "[數字鍵] 選擇攻擊技能 %d", newSkill + 1);
    UIAddLog("[數字鍵] 攻擊技能: %d", newSkill + 1);
}
int GetMonsterCount(GameHandle* gh) {
    if (!IsLicenseValid()) return 0;
    std::vector<Entity> monsters;
    return ScanEntities(gh, &monsters, NULL) >= 0 ? (int)monsters.size() : 0;
}
int GetInventoryCount(GameHandle* gh) {
    if (!IsLicenseValid()) return 0;
    return ScanInventory(gh, NULL);
}
int GetSkillLockState(GameHandle* gh) {
    if (!gh || !gh->hProcess || !gh->baseAddr) return 0;
    return SafeRPM<int>(gh->hProcess, gh->baseAddr + OffsetConfig::TargetLockedState(), 0);
}
int GetAttackCount(GameHandle* gh) {
    if (!IsLicenseValid()) return 0;
    if (!gh || !gh->hProcess || !gh->baseAddr) return 0;
    return SafeRPM<int>(gh->hProcess, gh->baseAddr + 0x724, 0);
}
int GetAttackRange(GameHandle* gh) {
    if (!gh || !gh->hProcess || !gh->baseAddr) return 0;
    return SafeRPM<int>(gh->hProcess, gh->baseAddr + 0x728, 0);
}
void SetMoveTarget(float x, float z) {
    EnterCriticalSection(&g_cfg.cs_protected);
    Waypoint wp;
    wp.x = x;
    wp.z = z;
    wp.name = "UI";
    g_cfg.huntWaypoints.push_back(wp);
    LeaveCriticalSection(&g_cfg.cs_protected);
}
// ============================================================
// 復活點擊邏輯（統一的復活函式）
// ============================================================
static bool DoReviveClick(HWND hWnd, const char* imgName, CalibIndex idx, int fx, int fz) {
    if (!hWnd || !IsWindow(hWnd)) return false;

    Coords::Point clickPt = ResolveReviveClickPoint(idx, fx, fz);
    const char* source = CoordCalibrator::Instance().IsCalibrated(idx) ? "校正" : "fallback";

    // 截圖找圖已禁用，直接使用校正/fallback 座標
    // （視覺模式已禁用，ScreenshotAssist 為空殼）
    //     GameHandle tmpGh = GetGameHandle();
    //     if (tmpGh.hWnd) {
    //         char picPath[MAX_PATH];
    //         snprintf(picPath, sizeof(picPath), "%s%s", DM_VISUAL_IMAGE_PATH, imgName);
    //         int dmX = -1, dmY = -1;
    //         bool dmFound = DMWrapper::g_dm.FindPic(0, 0, 1024, 768, picPath, 0.8f, 0, &dmX, &dmY);
    //         if (dmFound && dmX >= 0 && dmY >= 0) {
    //             Logf("復活", "[DM找圖] %s found at (%d,%d)", imgName, dmX, dmY);
    //             clickPt = Coords::Point(dmX, dmY);
    //             source = "DM找圖";
    //         } else {
    //             Logf("復活", "[DM找圖] %s not found (ret=%d x=%d y=%d)",
    //                 imgName, dmFound ? 1 : 0, dmX, dmY);
    //         }
    //     }
    // }

    // 使用預設座標或找到的座標點擊
    Logf("復活", "[%s] click=(%d,%d)", source, clickPt.x, clickPt.z);
    return ClickRelativeWithSendInput(hWnd, clickPt.x, clickPt.z, source);
}

// 公開的復活執行函式（供 FSM DeadHandler 呼叫）
bool ExecuteRevive(HWND hWnd) {
    int mode = g_cfg.revive_mode.load();
    bool success = false;

    switch (mode) {
        case 0: { // 歸魂珠優先
            CalibIndex idx = CalibIndex::REVIVE_SOUL_PEARL;
            success = DoReviveClick(hWnd, "歸魂珠.png", idx,
                Coords::歸魂珠復活.x, Coords::歸魂珠復活.z);
            if (!success) {
                // fallback 到原地復活
                success = DoReviveClick(hWnd, "原地.png", CalibIndex::REVIVE原地,
                    Coords::復活按鈕.x, Coords::復活按鈕.z);
            }
            break;
        }
        case 1: // 原地復活
            success = DoReviveClick(hWnd, "原地.png", CalibIndex::REVIVE原地,
                Coords::復活按鈕.x, Coords::復活按鈕.z);
            break;
        case 2: // 基本復活
            success = DoReviveClick(hWnd, "基本.png", CalibIndex::REVIVE_基本,
                Coords::基本復活.x, Coords::基本復活.z);
            break;
    }

    Logf("復活", "[ExecuteRevive] mode=%d success=%d", mode, success);
    return success;
}

static void ResetDeadRecoveryRuntime() {
    s_enteredDeadState = false;
    s_deathStartTime = 0;
    s_deadRecoverySeenTime = 0;
    s_reviveRetryCount = 0;
    s_reviveClicked = false;
    s_loggedDead = false;
    if (DMVisual::IsInited()) {
        DMVisual::ResetDeadState();
    }
}

static void FinishDeadRecovery(GameHandle* gh, const PlayerState& st, const char* reason) {
    (void)gh;
    int mode = g_cfg.revive_mode.load();
    bool autoSupply = g_cfg.auto_supply.load();
    bool inTown = IsTownMap(st.mapId);
    bool usablePos = HasUsableWorldPos(st.x, st.z);
    bool likelyField = !inTown && (mode == 0 || mode == 1 || usablePos || !IsReliableTownMapId(st.mapId));

    if (!autoSupply) {
        Logf("狀態機", "[DEAD -> HUNTING] HP恢復，自動補給關閉 mode=%d Map=%d x=%.1f z=%.1f",
            mode, st.mapId, st.x, st.z);
        TransitionState(BotState::HUNTING, reason ? reason : "DeadRecoveredNoSupply", &st);
        ResetDeadRecoveryRuntime();
        return;
    }

    if (likelyField && mode != 2) {
        Logf("狀態機", "[DEAD -> RETURNING] HP恢復，先回城補給 mode=%d Map=%d x=%.1f z=%.1f",
            mode, st.mapId, st.x, st.z);
        TransitionState(BotState::RETURNING, reason ? reason : "DeadRecoveredNeedReturn", &st);
        ResetDeadRecoveryRuntime();
        return;
    }

    const char* townNames[] = { "聖門", "商洞", "玄巖", "鳳凰" };
    int townIdx = g_cfg.town_index.load();
    Logf("狀態機", "[DEAD -> TOWN_SUPPLY] HP恢復，進入補給 mode=%d town=%d/%s Map=%d x=%.1f z=%.1f",
        mode, townIdx, (townIdx >= 0 && townIdx <= 3) ? townNames[townIdx] : "?",
        st.mapId, st.x, st.z);
    TransitionState(BotState::TOWN_SUPPLY, reason ? reason : "DeadRecoveredSupply", &st);
    ResetDeadRecoveryRuntime();
}

// ============================================================
// 遊戲時間自動傳送
// ============================================================
static int s_lastReturnTriggerDay = -1;   // 回城觸發日
static int s_lastFieldTriggerDay = -1;    // 返回野外觸發日

// 計算日期序列號（年*10000 + 月*100 + 日）
// 確保跨年比較正確，並防止時間往回調時的問題
static int GetDateSerial() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return st.wYear * 10000 + st.wMonth * 100 + st.wDay;
}

// 檢查是否到回城時間
bool CheckGameTimeReturn(GameHandle* gh) {
    if (!gh || !gh->hProcess) return false;
    if (!g_cfg.auto_game_time.load()) return false;

    DWORD gameHour = SafeRPM<DWORD>(gh->hProcess, gh->baseAddr + OffsetConfig::GameTimeHour(), 0xFF);
    DWORD gameMinute = SafeRPM<DWORD>(gh->hProcess, gh->baseAddr + OffsetConfig::GameTimeMinute(), 0xFF);

    int targetHour = g_cfg.game_time_hour.load();
    int targetMin = g_cfg.game_time_min.load();

    int today = GetDateSerial();

    // 防時間往回調：如果今天 < 上次觸發日，說明時間被調整過，跳過此次
    if (today < s_lastReturnTriggerDay) {
        s_lastReturnTriggerDay = today;  // 同步到當前日期
        return false;
    }

    if ((int)gameHour == targetHour && (int)gameMinute == targetMin) {
        if (today != s_lastReturnTriggerDay) {
            s_lastReturnTriggerDay = today;
            Logf("遊戲時間", "[時間到] %02d:%02d → 回城", gameHour, gameMinute);
            return true;
        }
    }
    return false;
}

// 檢查是否到返回野外時間
bool CheckGameTimeBackToField(GameHandle* gh) {
    if (!gh || !gh->hProcess) return false;
    if (!g_cfg.auto_game_time.load()) return false;
    if (!g_cfg.auto_return_to_field.load()) return false;

    DWORD gameHour = SafeRPM<DWORD>(gh->hProcess, gh->baseAddr + OffsetConfig::GameTimeHour(), 0xFF);
    DWORD gameMinute = SafeRPM<DWORD>(gh->hProcess, gh->baseAddr + OffsetConfig::GameTimeMinute(), 0xFF);

    int targetHour = g_cfg.game_time_return_hour.load();
    int targetMin = g_cfg.game_time_return_min.load();

    int today = GetDateSerial();

    // 防時間往回調
    if (today < s_lastFieldTriggerDay) {
        s_lastFieldTriggerDay = today;
        return false;
    }

    if ((int)gameHour == targetHour && (int)gameMinute == targetMin) {
        if (today != s_lastFieldTriggerDay) {
            s_lastFieldTriggerDay = today;
            Logf("遊戲時間", "[時間到] %02d:%02d → 返回野外", gameHour, gameMinute);
            return true;
        }
    }
    return false;
}

static bool ClickRelativeWithSendInput(HWND hWnd, int rx, int rz, const char* reason) {
    if (!hWnd || !IsWindow(hWnd)) return false;

    // 使用統一 CoordConv 命名空間（0-1000 標準化座標）
    int cx, cy;
    int clientW, clientH;
    if (!CoordConv::GetClientRect(hWnd, &clientW, &clientH)) return false;
    CoordConv::RelToClient(rx, rz, &cx, &cy, clientW, clientH);

    // Clamp to client area
    if (cx < 0) cx = 0;
    if (cx >= clientW) cx = clientW - 1;
    if (cy < 0) cy = 0;
    if (cy >= clientH) cy = clientH - 1;

    int sx, sy;
    if (!CoordConv::ClientToScreenPt(hWnd, cx, cy, &sx, &sy)) return false;

    // 激活窗口（DirectX 必須）- 已禁用以避免黑屏
    // 使用 PostMessage 方式無需 SetForegroundWindow

    // 移動滑鼠到目標位置
    SetCursorPos(sx, sy);
    Sleep(10);

    // 點擊
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    UINT sent = SendInput(2, inputs, sizeof(INPUT));

    Logf("復活", "[SendInput] 點擊%s rel=(%d,%d) screen=(%d,%d) sent=%u",
        reason && reason[0] ? reason : "",
        rx, rz, sx, sy, sent);
    return sent == 2;
}
static Coords::Point ResolveReviveClickPoint(CalibIndex idx, int fx, int fz) {
    CoordCalibrator& calib = CoordCalibrator::Instance();
    if (calib.IsCalibrated(idx)) {
        Coords::Point p(calib.GetX(idx), calib.GetZ(idx));
        Logf("復活", "[座標] 使用GUI校正 %s click=(%d,%d)",
            CoordCalibrator::Instance().GetLabel(idx), p.x, p.z);
        return p;
    }

    // 使用傳入的預設座標（來自 coords.h）
    return Coords::Point(fx, fz);
}

// BotTick 現在使用簡化的 FSM 框架
// 舊的冗長 BotTick 已重構為各狀態 Handler
void BotTick(GameHandle* gh) {
    // 使用 FSM 簡化版本
    // 框架會自動處理所有狀態轉換和看門狗計時器

    static bool s_fsmInited = false;
    if (!s_fsmInited) {
        InitFSM();
        s_fsmInited = true;
    }

    BotTickSimplified(gh);
}
