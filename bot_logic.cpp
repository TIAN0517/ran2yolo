#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include <windows.h>
#include <atomic>

#include "bot_logic.h"
#include "game_process.h"
#include "input_sender.h"
#include "memory_reader.h"
#include "offset_config.h"
#include "coords.h"
#include "nethook_shmem.h"
#include "attack_packet.h"
#include "offline_license.h"
#include "screenshot.h"
#include "screenshot_assist.h"
#include "coord_calib.h"
#include "target_lock.h"
#include "visionentity.h"
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
static void Log(const char* tag, const char* msg);
static void Logf(const char* tag, const char* fmt, ...);
static void SupplyForceReset(void);
// ============================================================
// Global state
// ============================================================
volatile bool g_Running = true;
BotConfig g_cfg;
static std::atomic<int> g_State{(int)BotState::IDLE};
static PlayerState s_uiPlayerCache;
static CRITICAL_SECTION s_uiCacheCs;
static CRITICAL_SECTION s_stateTransitionCs;  // BUG-C002: 狀態轉換需要鎖保護

// ✅ 離線卡密驗證狀態
std::atomic<bool> g_licenseValid{false};
bool IsLicenseValid() { return g_licenseValid.load(); }
void SetLicenseValid(bool valid) { g_licenseValid.store(valid); }

// ═══════════════ YOLO 設定存取 ════════════════
bool GetYoloMode() { return g_cfg.use_yolo_mode.load(); }
void SetYoloMode(bool enabled) {
    g_cfg.use_yolo_mode.store(enabled);
    // 互斥邏輯：開啟 YOLO 時自動關閉像素視覺模式
    if (enabled) {
        g_cfg.use_visual_mode.store(false);
    }
}
float GetYoloConfidence() { return g_cfg.yolo_confidence.load(); }
void SetYoloConfidence(float conf) {
    // 限幅 [0.0, 1.0]
    if (conf < 0.0f) conf = 0.0f;
    if (conf > 1.0f) conf = 1.0f;
    g_cfg.yolo_confidence.store(conf);
}

static volatile LONG s_uiCacheCsInit = 0;
static volatile LONG s_invCacheCsInited = 0;
static DWORD s_currentTargetId = 0;
static DWORD s_lastPickupTime = 0;
static DWORD s_lastDrinkCheck = 0;
static char  s_curBar = (char)0xFF;  // 目前技能列（0xFF=未初始化，確保第一次 SwitchBar 一定發送）
static DWORD s_lastStatusLog = 0;
static DWORD s_returnStartTime = 0;
static bool s_returnCardSent = false;
static DWORD s_backToFieldStartTime = 0;
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
static bool s_loggedReturn = false;
static BotState s_pausedPreviousState = BotState::IDLE;  // PAUSED 之前的狀態（用於正確恢復）
static DWORD s_lastReadFailLog = 0;
static DWORD s_lastNoHwndLog = 0;
static DWORD s_consecutiveReadFail = 0;
static DWORD s_lastInvalidStateLog = 0;
static DWORD s_lastInventoryDiagLog = 0;
static DWORD s_lastLegacyOffsetLog = 0;
static DWORD s_lastRelativeCombatLog = 0;
static DWORD s_lastValidPlayerStateTime = 0;
static bool s_hasLastValidPlayerState = false;
static PlayerState s_lastValidPlayerState = {};
static std::atomic<bool> s_relativeOnlyCombatMode{false};
static int s_relativeScanIndex = 0;
static int s_relativeSkillIndex = 0;
static int s_returnSourceMapId = -1;
static float s_returnSourceX = 0.0f;
static float s_returnSourceZ = 0.0f;
static bool s_returnSourceValid = false;
static int s_backToFieldSourceMapId = -1;
static float s_backToFieldSourceX = 0.0f;
static float s_backToFieldSourceZ = 0.0f;
static bool s_backToFieldSourceValid = false;
static bool s_backToFieldSourceWasTown = false;
// ═══════════════ 戰鬥意向狀態機 ═══════════════
enum class CombatIntent {
    SEEKING = 0,   // 尋找目標中
    ENGAGING = 1,  // 已在攻擊範圍，施放技能中
    LOOTING  = 2,  // 目標死亡，等待撿物品
};
static CombatIntent s_combatIntent = CombatIntent::SEEKING;
static DWORD s_killCount = 0;             // 擊殺計數器
static DWORD s_targetLostTime = 0;        // 目標丟失/死亡時間
static DWORD s_lootDelay = 800;           // 死後撿物品延遲(ms)
static DWORD s_engageStartTime = 0;       // 開始攻擊的時間
static DWORD s_targetLockTime = 0;        // 目標鎖定時間
static DWORD s_lastTargetHp = 0;          // 上次記錄的目標 HP
static DWORD s_hpCheckTime = 0;           // 上次 HP 檢查時間
// 失敗目標追蹤：記住打不到的怪，避免重複鎖定
static const int MAX_FAILED_TARGETS = 10;
struct FailedTarget { DWORD id; DWORD failTime; };
static FailedTarget s_failedTargets[MAX_FAILED_TARGETS] = {};
static int s_failedCount = 0;
static DWORD s_lastFailedPurge = 0;        // 上次清理失敗列表的時間
// 超時設定
static const DWORD ENGAGE_HP_TIMEOUT = 12000;   // 12 秒 HP 沒下降 → 放棄
static const DWORD ENGAGE_HARD_TIMEOUT = 30000;  // 30 秒硬超時 → 強制放棄
static const DWORD FAILED_TARGET_COOLDOWN = 60000; // 60 秒內不再鎖定失敗目標
static bool IsFailedTarget(DWORD targetId);
// 每個技能的獨立冷卻時間（毫秒）
static DWORD s_skillLastTime[BotConfig::MAX_SKILLS] = {0};
// 防止同一 tick 內多次施放
static DWORD s_lastCombatTick = 0;
// 當前技能索引
static int s_currentSkillIndex = 0;
// 意向切換防抖：避免狀態機在高頻率下當掉（100ms 間隔）
static DWORD s_lastIntentChange = 0;
// 鎖定目標冷卻：防止高頻率點擊
static DWORD s_lastTargetClick = 0;
// ═══════════════ 攻擊圓圈範圍（1024x768 相對座標中心 + 真圓半徑）═══════════════
static const int ATTACK_CENTER_X = IsWin7Platform() ? 520 : 500;
static const int ATTACK_CENTER_Y = IsWin7Platform() ? 390 : 370;
static const int ATTACK_TARGET_RADIUS = 260;
// ============================================================
// 轉轉樂安全碼防檢測
// ============================================================
struct SecurityWindowSearchCtx {
    bool found;
};
static BOOL CALLBACK EnumSecurityWindowProc(HWND hwnd, LPARAM lParam) {
    SecurityWindowSearchCtx* ctx = (SecurityWindowSearchCtx*)lParam;
    if (!IsWindowVisible(hwnd)) return TRUE;
    wchar_t title[256] = {0};
    int len = GetWindowTextW(hwnd, title, 256);
    if (len <= 0) return TRUE;
    if (wcsstr(title, L"轉轉樂") || wcsstr(title, L"抽獎")) {
        ctx->found = true;
        return FALSE;
    }
    return TRUE;
}
static bool CheckSecurityCodeWindow()
{
    static bool s_alreadyNotified = false;
    bool detected = false;
    HWND h1 = FindWindowW(NULL, L"抽獎轉轉樂");
    HWND h2 = FindWindowW(NULL, L"轉轉樂安全碼");
    if ((h1 && IsWindowVisible(h1)) || (h2 && IsWindowVisible(h2))) {
        detected = true;
    }
    if (!detected) {
        SecurityWindowSearchCtx ctx = { false };
        EnumWindows(EnumSecurityWindowProc, (LPARAM)&ctx);
        if (ctx.found) detected = true;
    }
    if (detected) {
        BotState oldState = (BotState)g_State.load();
        if (oldState != BotState::PAUSED) {
            SetBotState(BotState::PAUSED);
            Log("防偵測", "★★★ 偵測到轉轉樂安全碼視窗！已強制暫停機器人 ★★★");
            Log("防偵測", "  └- 請手動輸入安全碼");
            Log("防偵測", "  └- 輸入完後按 F11 繼續 bot");
        }
        if (!s_alreadyNotified) {
            s_alreadyNotified = true;
            MessageBoxW(NULL,
                L"偵測到轉轉樂安全碼視窗！\n\n"
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
// ============================================================
// 防呆自動移動功能
// ============================================================
static DWORD s_antiStuckLastMove = 0;
static int   s_antiStuckPhase = 0;
static DWORD s_antiStuckPhaseStart = 0;
// ============================================================
// SupplyTick / BACK_TO_FIELD 統一狀態機靜態變數
// ============================================================
static int s_supplyPhase = 0;
static DWORD s_supplyPhaseStart = 0;
static int s_supplyRetryCount = 0;
static DWORD s_npcId = 0;
static DWORD s_invBase = 0;
static int s_huntPointIndex = 0;
static int s_phase0SubStep = 0;
static DWORD s_phase0RClickTime = 0;
static int s_buySubPhase = 0;
static bool s_supplyEntered = false;
static DWORD s_globalTimeout = 0;
static int s_lastSupplyPhase = -1;
static DWORD s_buyPhaseStart = 0;
static bool s_reentryGuard = false;
static bool s_sellSessionActive = false;
static DWORD s_lastSellAction = 0;
static DWORD s_backToFieldCardSent = 0;  // 時間戳，當發送時設置，用於檢測

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
// M1 fix: 修正位址範圍（0x10000 是 guard page，應從 0x1000 起）
// ✅ BUG-001 FIX: 增加更多調試標記檢測，防止無限期迴圈
static bool IsGoodPtr(DWORD p) {
    // 排除常見調試標記值
    if (p == 0xCDCDCDCD || p == 0xDDDDDDDD || p == 0xABABABAB ||
        p == 0xBAADF00D || p == 0xFEEEFEEE || p == 0xFEEEBAAD ||
        p == 0xCCCCCCCC || p == 0xBABABABA || p == 0xFEEEFEEE) return false;
    // 核心範圍檢查
    return p >= 0x1000 && p < 0x7FFF0000;
}
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
    index %= 9;  // F1 ~ F9 = 9 keys
    if (index < 0) index = 0;
    return VK_F1 + index;  // VK_F1 = 0x70 = 112
}
static bool IsTownMap(int mapId) {
    bool found = false;
    EnterCriticalSection(&g_cfg.cs_protected);
    for (int id : g_cfg.townMapIds) {
        if (id == mapId) { found = true; break; }
    }
    LeaveCriticalSection(&g_cfg.cs_protected);
    return found;
}
static bool HasTownMapConfig() {
    bool hasConfig = false;
    EnterCriticalSection(&g_cfg.cs_protected);
    hasConfig = !g_cfg.townMapIds.empty();
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
    // ✅ 擴展地圖 ID 範圍：原 0-32767 可能不夠
    // 允許 0 到 65535 (WORD 範圍) 或 -1 (未知)
    return (mapId >= 0 && mapId <= 65535) || mapId == -1;
}
static bool TryReadLegacyStaticMapId(GameHandle* gh, int* outMapId, DWORD* outOffset) {
    if (outMapId) *outMapId = -1;
    if (outOffset) *outOffset = 0;
    if (!gh || !gh->hProcess || !gh->baseAddr) return false;

    static const DWORD kMapCandidates[] = {
        0x92E044,
        0x92E048,
    };

    int zeroFallback = -1;
    DWORD zeroFallbackOffset = 0;
    for (DWORD candidate : kMapCandidates) {
        int mapId = SafeRPM<int>(gh->hProcess, gh->baseAddr + candidate, -1);
        if (!IsPlausibleMapId(mapId)) continue;
        if (mapId == 0) {
            if (zeroFallbackOffset == 0) {
                zeroFallback = mapId;
                zeroFallbackOffset = candidate;
            }
            continue;
        }
        if (outMapId) *outMapId = mapId;
        if (outOffset) *outOffset = candidate;
        return true;
    }
    if (zeroFallbackOffset != 0) {
        if (outMapId) *outMapId = zeroFallback;
        if (outOffset) *outOffset = zeroFallbackOffset;
        return true;
    }
    return false;
}
static bool TryReadLegacyStaticPos(GameHandle* gh, float* outX, float* outY, float* outZ,
    DWORD* outXOffset, DWORD* outYOffset, DWORD* outZOffset) {
    if (outX) *outX = 0.0f;
    if (outY) *outY = 0.0f;
    if (outZ) *outZ = 0.0f;
    if (outXOffset) *outXOffset = 0;
    if (outYOffset) *outYOffset = 0;
    if (outZOffset) *outZOffset = 0;
    if (!gh || !gh->hProcess || !gh->baseAddr) return false;

    struct PosCandidate {
        DWORD x;
        DWORD y;
        DWORD z;
    };
    static const PosCandidate kPosCandidates[] = {
        {0x92E050, 0x92E058, 0x92E054},
        {0x92E050, 0x92E05A, 0x92E054},
    };

    for (const PosCandidate& candidate : kPosCandidates) {
        float x = SafeRPM<float>(gh->hProcess, gh->baseAddr + candidate.x, 0.0f);
        float z = SafeRPM<float>(gh->hProcess, gh->baseAddr + candidate.z, 0.0f);
        if (!HasUsableWorldPos(x, z)) continue;

        float y = SafeRPM<float>(gh->hProcess, gh->baseAddr + candidate.y, 0.0f);
        if (outX) *outX = x;
        if (outY) *outY = y;
        if (outZ) *outZ = z;
        if (outXOffset) *outXOffset = candidate.x;
        if (outYOffset) *outYOffset = candidate.y;
        if (outZOffset) *outZOffset = candidate.z;
        return true;
    }
    return false;
}
static void ApplyLegacyStaticPlayerFallbacks(GameHandle* gh, PlayerState* st,
    bool* usedMapFallback, bool* usedPosFallback) {
    if (usedMapFallback) *usedMapFallback = false;
    if (usedPosFallback) *usedPosFallback = false;
    if (!gh || !st) return;

    DWORD mapOffset = 0;
    if (!IsPlausibleMapId(st->mapId)) {
        int legacyMapId = -1;
        if (TryReadLegacyStaticMapId(gh, &legacyMapId, &mapOffset)) {
            st->mapId = legacyMapId;
            if (usedMapFallback) *usedMapFallback = true;
        }
    }

    DWORD posXOffset = 0, posYOffset = 0, posZOffset = 0;
    if (!HasUsableWorldPos(st->x, st->z)) {
        float legacyX = 0.0f, legacyY = 0.0f, legacyZ = 0.0f;
        if (TryReadLegacyStaticPos(gh, &legacyX, &legacyY, &legacyZ,
            &posXOffset, &posYOffset, &posZOffset)) {
            st->x = legacyX;
            st->y = legacyY;
            st->z = legacyZ;
            if (usedPosFallback) *usedPosFallback = true;
        }
    }

    DWORD now = GetTickCount();
    if ((mapOffset || posXOffset) && now - s_lastLegacyOffsetLog > 5000) {
        char detail[192] = {0};
        if (mapOffset && posXOffset) {
            _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "Map[%d]=0x%08X Pos[(%.1f,%.1f,%.1f)]=0x%08X/0x%08X/0x%08X",
                st->mapId, mapOffset, st->x, st->y, st->z, posXOffset, posYOffset, posZOffset);
        } else if (mapOffset) {
            _snprintf_s(detail, sizeof(detail), _TRUNCATE, "Map[%d]=0x%08X", st->mapId, mapOffset);
        } else {
            _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "Pos[(%.1f,%.1f,%.1f)]=0x%08X/0x%08X/0x%08X",
                st->x, st->y, st->z, posXOffset, posYOffset, posZOffset);
        }
        Logf("讀取", "↩️ 啟用 legacy 靜態偏移 fallback: %s", detail);
        s_lastLegacyOffsetLog = now;
    }
}
static bool IsPlausibleTargetState(int hasTarget, DWORD targetId) {
    if (hasTarget == 0) {
        return targetId == 0 || targetId == 0xFFFFFFFF;
    }
    if (hasTarget == 1) {
        return targetId != 0 && targetId != 0xFFFFFFFF;
    }
    return false;
}
static void ApplyLegacyStaticTargetFallbacks(GameHandle* gh, PlayerState* st) {
    if (!gh || !gh->hProcess || !gh->baseAddr || !st) return;

    DWORD currentTargetId = st->targetId;
    int currentHasTarget = st->hasTarget;

    DWORD legacyTargetId = SafeRPM<DWORD>(gh->hProcess, gh->baseAddr + 0x92F0E8, 0xFFFFFFFF);
    int legacyHasTarget = SafeRPM<int>(gh->hProcess, gh->baseAddr + 0x92FCB4, 0);

    bool currentPlausible = IsPlausibleTargetState(currentHasTarget, currentTargetId);
    bool legacyPlausible = IsPlausibleTargetState(legacyHasTarget, legacyTargetId);

    if (!legacyPlausible) return;
    if (currentPlausible && !(legacyHasTarget == 1 && currentHasTarget != 1)) return;

    st->targetId = legacyTargetId;
    st->hasTarget = legacyHasTarget;
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
static void Log(const char* tag, const char* msg) {
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
static void Logf(const char* tag, const char* fmt, ...) {
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
static void InitAntiDebugHooks() {
    Log("防偵測", "正在安裝 Inline Patch Anti-Debug 保護...");
    HMODULE hK32 = GetModuleHandleA("kernel32.dll");
    if (!hK32) {
        Log("防偵測", "⚠️ 無法取得 kernel32.dll 句柄");
        return;
    }
    DWORD_PTR pIsDbg = (DWORD_PTR)GetProcAddress(hK32, "IsDebuggerPresent");
    if (pIsDbg) {
        DWORD oldProt = 0;
        if (VirtualProtect((LPVOID)pIsDbg, 3, PAGE_EXECUTE_READWRITE, &oldProt)) {
            BYTE patch[] = { 0x31, 0xC0, 0xC3 };
            memcpy((void*)pIsDbg, patch, sizeof(patch));
            VirtualProtect((LPVOID)pIsDbg, 3, oldProt, &oldProt);
            Log("防偵測", "✓ IsDebuggerPresent Patch 成功");
        }
    }
    DWORD_PTR pDbgBrk = (DWORD_PTR)GetProcAddress(hK32, "DebugBreak");
    if (pDbgBrk) {
        DWORD oldProt = 0;
        if (VirtualProtect((LPVOID)pDbgBrk, 1, PAGE_EXECUTE_READWRITE, &oldProt)) {
            BYTE patch[] = { 0xC3 };
            memcpy((void*)pDbgBrk, patch, sizeof(patch));
            VirtualProtect((LPVOID)pDbgBrk, 1, oldProt, &oldProt);
            Log("防偵測", "✓ DebugBreak Patch 成功");
        }
    }
    DWORD_PTR pChkRemote = (DWORD_PTR)GetProcAddress(hK32, "CheckRemoteDebuggerPresent");
    if (pChkRemote) {
        DWORD oldProt = 0;
        if (VirtualProtect((LPVOID)pChkRemote, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
            BYTE patch[] = {
                0x8B, 0x44, 0x24, 0x08,
                0xC7, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x31, 0xC0,
                0x40,
                0xC2, 0x08, 0x00
            };
            memcpy((void*)pChkRemote, patch, sizeof(patch));
            VirtualProtect((LPVOID)pChkRemote, 16, oldProt, &oldProt);
            Log("防偵測", "✓ CheckRemoteDebuggerPresent Patch 成功");
        }
    }
    Log("防偵測", "Inline Patch Anti-Debug 保護安裝完成");
}
// ============================================================
// Cached player state
// ============================================================
static void UpdateUICache(const PlayerState& st) {
    EnsureUICacheReady();
    EnterCriticalSection(&s_uiCacheCs);
    s_uiPlayerCache = st;
    LeaveCriticalSection(&s_uiCacheCs);
}
PlayerState GetCachedPlayerState() {
    EnsureUICacheReady();
    PlayerState st;
    EnterCriticalSection(&s_uiCacheCs);
    st = s_uiPlayerCache;
    LeaveCriticalSection(&s_uiCacheCs);
    return st;
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

    st.hp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerHP(), 0);
    st.maxHp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMaxHP(), 0);
    st.mp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMP(), 0);
    st.maxMp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMaxMP(), 0);
    st.sp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerSP(), 0);
    st.maxSp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMaxSP(), 0);
    st.gold = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerGold(), 0);
    st.level = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerLevel(), 0);
    st.mapId = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMapID(), 0);
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

    // ✅ 驗證玩家數據有效性：需要至少一項有效數據
    bool hasValidData = (st.maxHp >= 1 || st.maxMp >= 1 || st.maxSp >= 1 ||
                         st.level > 0 || st.gold > 0 || st.combatPower > 0 ||
                         st.physAtkMin > 0 || st.sprAtkMin > 0 ||
                         (st.targetId != 0 && st.targetId != 0xFFFFFFFF));
    if (!hasValidData) {
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
    if (s_entityPoolKnownBad) {
        DWORD now = GetTickCount();
        if (now - s_entityPoolBadTime < 2000) {
            // 冷卻期（2秒），返回 0 讓 EnumerateCrows 使用 Fallback
            return 0;
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
    float x, y, z;
    DWORD hp;
    DWORD maxHp;
    DWORD crowDataPtr;
};
// ============================================================
// EnumerateCrows_Fallback - 直接記憶體掃描（基於用戶 CE 分析）
// 掃描範圍: gameBase + 0x1D0000 ~ gameBase + 0x2D0000
// 怪物結構: HP @ base+0x7B0, ServerID @ base+0x91C
// ============================================================
static int EnumerateCrows_Fallback(GameHandle* gh, CrowInfo* outCrows, int maxCrows) {
    if (!gh || !gh->hProcess || !gh->baseAddr || !outCrows) return 0;

    // 掃描範圍：遊戲基址往上一段記憶體（怪物集中區域）
    DWORD scanBase = gh->baseAddr + 0x1D0000;
    DWORD scanEnd = gh->baseAddr + 0x2D0000;
    const int SCAN_STEP = 0x1000;  // 4KB 顆粒度
    const int MAX_HP_REASONABLE = 10000000;  // 最大合理 HP

    int count = 0;
    DWORD addr = scanBase;

    while (addr < scanEnd && count < maxCrows) {
        // 快速檢查是否為有效的記憶體頁
        MEMORY_BASIC_INFORMATION memInfo{};
        if (VirtualQueryEx(gh->hProcess, reinterpret_cast<LPCVOID>((DWORD_PTR)addr), &memInfo, sizeof(memInfo))) {
            if (memInfo.State == MEM_COMMIT && (memInfo.Protect & (PAGE_READONLY | PAGE_READWRITE))) {
                // 檢查這個區塊內的候選 HP 地址（每 0x1000 位址檢查多個候選）
                for (DWORD offset = 0; offset < 0x1000 && count < maxCrows; offset += 0x100) {
                    DWORD candidate = addr + offset;
                    DWORD hp = SafeRPM<DWORD>(gh->hProcess, candidate + 0x7B0, 0);

                    // HP 合理性檢查：非零、小於合理最大值
                    if (hp > 0 && hp < MAX_HP_REASONABLE) {
                        // 進一步驗證：檢查 ServerID 是否有效
                        DWORD serverId = SafeRPM<DWORD>(gh->hProcess, candidate + 0x91C, 0);

                        // 有效 ServerID：非零、非FFFFFFFF、在合理範圍
                        // MID應該 < 1024, SID < 64
                        DWORD mid = (serverId >> 8) & 0x3FF;
                        DWORD sid = serverId & 0x3F;

                        if (serverId != 0 && serverId != 0xFFFFFFFF &&
                            mid < 1024 && sid < 64 && mid > 0) {

                            CrowInfo& ci = outCrows[count];
                            ci.serverId = serverId;
                            ci.crowDataPtr = candidate;
                            ci.hp = hp;
                            WORD maxHp = SafeRPM<WORD>(gh->hProcess, candidate + OffsetConfig::CrowMaxHP(), 0);
                            ci.maxHp = maxHp > 0 ? (DWORD)maxHp : hp;
                            ci.x = SafeRPM<float>(gh->hProcess, candidate + 0x878, 0.0f);
                            ci.y = SafeRPM<float>(gh->hProcess, candidate + 0x87C, 0.0f);
                            ci.z = SafeRPM<float>(gh->hProcess, candidate + 0x880, 0.0f);

                            if (count < 5) {
                                Logf("掃描", "怪物[%d]: ID=0x%X MID=%d SID=%d HP=%d @ 0x%X",
                                    count, serverId, mid, sid, hp, candidate);
                            }
                            count++;
                        }
                    }
                }
            }
        }
        addr += SCAN_STEP;
    }

    if (count > 0) {
        Logf("掃描", "直接掃描找到 %d 隻怪物", count);
    }

    return count;
}

static int EnumerateCrows(GameHandle* gh, CrowInfo* outCrows, int maxCrows) {
    if (!gh || !gh->baseAddr || !outCrows) return 0;

    // ── 嘗試 NetHook 共享記憶體（若可用）──
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
    if (!gh->hProcess) return 0;
    DWORD headNode = GetCROWListHead(gh);
    if (headNode) {
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
        if (count > 0) return count;
    }

    // ── Fallback：直接記憶體掃描 ──
    static DWORD s_lastFallbackScan = 0;
    static int s_lastFallbackCount = 0;
    static CrowInfo s_fallbackCache[200] = {};
    DWORD now = GetTickCount();
    int cacheLimit = (int)(sizeof(s_fallbackCache) / sizeof(s_fallbackCache[0]));

    if (now - s_lastFallbackScan <= 5000 && s_lastFallbackCount > 0) {
        int copyCount = s_lastFallbackCount;
        if (copyCount > maxCrows) copyCount = maxCrows;
        memcpy(outCrows, s_fallbackCache, sizeof(CrowInfo) * copyCount);
        return copyCount;
    }

    int fallbackCount = EnumerateCrows_Fallback(gh, s_fallbackCache, cacheLimit);
    if (fallbackCount > 0) {
        s_lastFallbackScan = now;
        s_lastFallbackCount = fallbackCount;
        int copyCount = fallbackCount;
        if (copyCount > maxCrows) copyCount = maxCrows;
        memcpy(outCrows, s_fallbackCache, sizeof(CrowInfo) * copyCount);
        return copyCount;
    }

    s_lastFallbackScan = now;
    s_lastFallbackCount = 0;
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
void InitBotLogic() {
    EnsureUICacheReady();
    InitializeCriticalSection(&s_invCacheCs);
    InterlockedExchange(&s_invCacheCsInited, 1);
    g_State.store((int)BotState::IDLE);
    g_cfg.active.store(false);
    s_entityPoolKnownBad = false;
    s_curBar = -1;
    srand((unsigned int)time(nullptr));
    DWORD now = GetTickCount();
    DWORD initTime = now - 500;
    for (int i = 0; i < BotConfig::MAX_SKILLS; i++) {
        s_skillLastTime[i] = initTime;
    }
    s_lastCombatTick = 0;
    s_returnSourceValid = false;
    s_backToFieldSourceValid = false;
    s_backToFieldSourceWasTown = false;
    s_hasLastValidPlayerState = false;
    s_lastValidPlayerStateTime = 0;
    s_consecutiveReadFail = 0;
    s_enteredHunting = false;
    s_wasInHunting = false;
    s_enteredDeadState = false;
    s_loggedDead = false;
    s_loggedReturn = false;
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
    // ✅ BUG-007 FIX: InitAttackSender 非阻塞（連線在背景線程完成）
    // InitAttackSender 現在是非阻塞的，TCP 連線在後台線程完成
    // 這樣 InitBotLogic 不會因為伺服器無回應而卡住
    InitAttackSender("210.64.10.55", 6870);
    UIAddLog("[Bot] 攻擊封包發送器已初始化（非阻塞）");
    if (ScreenshotAssist_Init()) {
        UIAddLog("[截圖] 輔助比對已就緒");
    } else {
        UIAddLog("[截圖] 輔助比對初始化失敗");
    }
    InitAntiDebugProtection();
    InitAntiDebugHooks();

    // ── YOLO 設定載入（INI 持久化）──
    extern bool OffsetConfig_LoadYolo();
    bool yoloLoaded = OffsetConfig_LoadYolo();
    if (yoloLoaded) {
        UIAddLog("[YOLO] 已載入設定 (mode=%d, conf=%.2f)",
            g_cfg.use_yolo_mode.load(), g_cfg.yolo_confidence.load());
    } else {
        UIAddLog("[YOLO] 使用預設設定");
    }

    // ── YOLO 偵測器初始化（延遲載入）──
    // 這裡只初始化，實際使用在視覺掃描時
    extern bool InitYoloDetector(const char*);
    if (InitYoloDetector("models\\best.onnx")) {
        float conf = g_cfg.yolo_confidence.load();
        float nms = g_cfg.yolo_nms_threshold.load() / 100.0f;
        extern void SetYoloThresholds(float, float);
        SetYoloThresholds(conf, nms);
        UIAddLog("[YOLO] 偵測器已就緒 (conf=%.2f, nms=%.2f)", conf, nms);
        if (!g_cfg.use_yolo_mode.load()) {
            g_cfg.use_yolo_mode.store(true);
            g_cfg.use_visual_mode.store(false);
            UIAddLog("[YOLO] 已自動啟用 YOLO 優先掃描");
        }
    } else {
        UIAddLog("[YOLO] 偵測器初始化失敗，將使用像素掃描");
    }

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
            UIAddLog("[License] 驗證成功，剩餘 %d 天", info.days_left);
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
    // ── YOLO 資源釋放 ──
    extern void DestroyYoloDetector();
    DestroyYoloDetector();

    ShutdownAttackSender();
    ScreenshotAssist_Shutdown();
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
    if (st.hp <= 0 || st.hp > st.maxHp * 20 ||
        st.mp < 0 || st.mp > st.maxMp * 20 ||
        st.sp < 0 || st.sp > st.maxSp * 20) {
        _snprintf_s(reason, reasonSize, _TRUNCATE, "屬性數值越界 HP/MP/SP=%d/%d/%d",
            st.hp, st.mp, st.sp);
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
            // BUG-H001 修復：strcpy_s(NULL) 會崩潰
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
    out->level = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerLevel(), 0);
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
    out->mapId = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMapID(), 0);

    static DWORD s_lastDebugTime = 0;
    DWORD now = GetTickCount();
    DWORD debugInterval = s_relativeOnlyCombatMode.load() ? 15000 : 5000;
    bool shouldLog = (now - s_lastDebugTime > debugInterval);

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
        WORD wx = SafeRPM<WORD>(gh->hProcess, base + OffsetConfig::PlayerPosX(), 0);
        WORD wz = SafeRPM<WORD>(gh->hProcess, base + OffsetConfig::PlayerPosZ(), 0);
        if (shouldLog) {
            Logf("讀取", "DEBUG: from base+0x930DF8: wx=%u wz=%u", wx, wz);
            s_lastDebugTime = now;
        }
        if (HasUsableWorldPos((float)wx, (float)wz)) {
            out->x = (float)wx;
            out->z = (float)wz;
            WORD wy = SafeRPM<WORD>(gh->hProcess, base + OffsetConfig::PlayerPosY(), 0);
            out->y = (float)wy;
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
    ApplyLegacyStaticTargetFallbacks(gh, out);

    bool usedLegacyMap = false;
    bool usedLegacyPos = false;
    ApplyLegacyStaticPlayerFallbacks(gh, out, &usedLegacyMap, &usedLegacyPos);

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
    ReadProcessMemory(gh.hProcess, (LPCVOID)(charAddr + OffsetConfig::PlayerName()),
                     outName, readLen, &bytesRead);
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
        if (IsFailedTarget(e.id)) continue;
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
static void ResetCombatRuntimeState() {
    s_currentTargetId = 0;
    s_combatIntent = CombatIntent::SEEKING;
    s_targetLostTime = 0;
    s_engageStartTime = 0;
    s_targetLockTime = 0;
    s_lastTargetHp = 0;
    s_hpCheckTime = 0;
    s_lastCombatTick = 0;
    s_lastTargetClick = 0;
    s_curBar = (char)0xFF;
    s_relativeOnlyCombatMode.store(false);
    s_relativeScanIndex = 0;
    s_relativeSkillIndex = 0;
    g_cfg.currentSkillIndex.store(0);
    g_cfg.lastRightClickTime.store(0);
    g_cfg.lastSkillTime.store(0);
    // 重置失敗目標追蹤（避免舊資料殘留）
    memset(s_failedTargets, 0, sizeof(s_failedTargets));
    s_failedCount = 0;
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

    const PlayerState* snapshot = st ? st : (s_hasLastValidPlayerState ? &s_lastValidPlayerState : NULL);
    char snapBuf[128];
    BuildStateSnapshot(snapshot, snapBuf, sizeof(snapBuf));

    DWORD now = GetTickCount();
    DWORD lastValidAge = s_lastValidPlayerStateTime ? (now - s_lastValidPlayerStateTime) : 0;

    if (oldState == BotState::HUNTING) {
        s_enteredHunting = false;
        s_wasInHunting = false;
    }
    if (oldState == BotState::RETURNING) {
        s_loggedReturn = false;
    }
    if (oldState == BotState::DEAD) {
        s_enteredDeadState = false;
        s_loggedDead = false;
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
            s_returnStartTime = 0;
            s_returnCardSent = false;
            s_loggedReturn = false;
            s_returnSourceValid = false;
            if (snapshot) {
                s_returnSourceMapId = snapshot->mapId;
                s_returnSourceX = snapshot->x;
                s_returnSourceZ = snapshot->z;
                s_returnSourceValid = true;
            }
            Log("狀態機", "  └- RETURNING: 重置回城計時器和卡片發送標記");
            break;
        case BotState::TOWN_SUPPLY:
            SupplyForceReset();
            // Bug 4 修復：如果來自 DEAD，先確認位置
            if (oldState == BotState::DEAD) {
                s_returnCardSent = false;
                s_returnStartTime = 0;
                Log("狀態機", "  └- TOWN_SUPPLY: 來自 DEAD，重置回城卡片狀態");
            }
            Log("狀態機", "  └- TOWN_SUPPLY: 重置補給 FSM 狀態");
            break;
        case BotState::BACK_TO_FIELD:
            s_backToFieldStartTime = now;
            s_backToFieldCardSent = 0;
            s_backToFieldSourceValid = false;
            s_backToFieldSourceWasTown = false;
            if (snapshot) {
                s_backToFieldSourceMapId = snapshot->mapId;
                s_backToFieldSourceX = snapshot->x;
                s_backToFieldSourceZ = snapshot->z;
                s_backToFieldSourceValid = true;
                s_backToFieldSourceWasTown = IsTownMap(snapshot->mapId);
            }
            Log("狀態機", "  └- BACK_TO_FIELD: 開始返回野外計時");
            break;
        case BotState::HUNTING:
            ResetCombatRuntimeState();
            s_enteredHunting = false;
            s_wasInHunting = false;
            s_supplyPhase = 0;  // 防止非正常退出 TOWN_SUPPLY 時的狀態污染
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
            SupplyForceReset();
            ResetCombatRuntimeState();
            s_returnStartTime = 0;
            s_returnCardSent = false;
            s_backToFieldStartTime = 0;
            s_backToFieldCardSent = 0;
            s_deadRecoverySeenTime = 0;
            s_reviveClicked = false;
            s_reviveRetryCount = 0;
            s_returnSourceValid = false;
            s_backToFieldSourceValid = false;
            s_backToFieldSourceWasTown = false;
            s_supplyPhase = 0;  // 確保非正常退出時狀態乾淨
            Log("狀態機", "  └- IDLE: 清空戰鬥/補給殘留狀態");
            break;
        case BotState::PAUSED:
            s_pausedPreviousState = oldState;  // 記住 PAUSED 之前的狀態
            Logf("狀態機", "  └- PAUSED: 轉轉樂安全碼已暫停（之前: %s）", GetStateName(s_pausedPreviousState));
            break;
        default:
            break;
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
void ToggleBotActive() {
    bool wasActive = g_cfg.active.load();
    UIAddLog("[Bot] 切換啟動: auth=%d active=%d state=%s",
        IsLicenseValid() ? 1 : 0,
        wasActive ? 1 : 0,
        GetStateName((BotState)g_State.load()));
    if (!IsLicenseValid()) {
        g_cfg.active.store(false);
        Log("認證", "❌ 卡密未驗證，拒絕啟動 Bot");
        UIAddLog("[Bot] 卡密未驗證，無法啟動");
        if (GetBotState() != BotState::IDLE) {
            TransitionState(BotState::IDLE, "AuthRequired", NULL);
        }
        return;
    }
    if (g_State.load() == (int)BotState::PAUSED) {
        g_cfg.active.store(true);
        BotState prevState = s_pausedPreviousState;
        // 恢復到 PAUSED 之前的狀態，而不是硬編碼 HUNTING
        if (prevState == BotState::IDLE || prevState == BotState::HUNTING) {
            TransitionState(prevState, "ResumeFromPaused", NULL);
        } else {
            // 如果之前狀態不對，回 IDLE
            TransitionState(BotState::IDLE, "ResumeFromPausedInvalid", NULL);
        }
        Logf("防偵測", "✓ F11 已恢復 bot（從 PAUSED 恢復到 %s）", GetStateName(prevState));
        UIAddLog("[Bot] F11 pressed - resuming bot from PAUSED to %s", GetStateName(prevState));
        return;
    }
    g_cfg.active.store(!wasActive);
    if (!wasActive) {
        if (g_State.load() == (int)BotState::IDLE) {
            // ── BUG-002 FIX: 非阻塞式啟動，允許 UI 響應 ──
            // 先檢查遊戲是否已就緒
            GameHandle gh = GetGameHandle();
            bool needAttach = !gh.hProcess || !gh.attached || !gh.baseAddr;

            if (needAttach) {
                // 嘗試一次附加
                GameHandle attachGh;
                memset(&attachGh, 0, sizeof(attachGh));
                if (FindGameProcess(&attachGh)) {
                    SetGameHandle(&attachGh);
                    gh = GetGameHandle();
                    Logf("狀態機", "✅ FindGameProcess 回傳成功: pid=%u hProcess=%p baseAddr=0x%08X attached=%d",
                        attachGh.pid, attachGh.hProcess, attachGh.baseAddr, attachGh.attached);
                } else {
                    // 遊戲未啟動，非阻塞等待
                    UIAddLog("[Bot] 等待遊戲啟動...");
                    Log("狀態機", "⚠️ 遊戲未啟動，等待中...");
                    // 讓 BotTick 處理後續重試，而不是在這裡阻塞
                }
            }

            // 嘗試讀取玩家資料
            PlayerState startState = {};
            char startReason[160] = {};
            PlayerStateReadStatus startStatus = PlayerStateReadStatus::READ_FAILED;

            if (gh.hProcess && gh.attached && gh.baseAddr) {
                startStatus = ReadPlayerStateDetailed(&gh, &startState, startReason, sizeof(startReason));
                if (startStatus == PlayerStateReadStatus::OK) {
                    Logf("狀態機", "✅ 玩家資料讀取成功: HP=%d/%d MP=%d/%d MAP=%d",
                        startState.hp, startState.maxHp, startState.mp, startState.maxMp, startState.mapId);
                } else {
                    Logf("狀態機", "⚠️ 讀取玩家資料失敗: %s", startReason);
                }
            }

            // 如果玩家資料不可用，檢查是否可以純相對座標模式運行
            if (startStatus != PlayerStateReadStatus::OK && !CanUseRelativeOnlyCombat(&gh)) {
                g_cfg.active.store(false);
                Logf("狀態機", "❌ 玩家資料不可用: %s", startReason[0] ? startReason : "未知原因");
                UIAddLog("[Bot] 啟動失敗：玩家資料不可用");
                return;
            }

            // 初始化技能冷卻
            DWORD now = GetTickCount();
            for (int i = 0; i < BotConfig::MAX_SKILLS; i++) {
                s_skillLastTime[i] = now - 500;
            }
            g_cfg.currentSkillIndex.store(0);

            Log("狀態機", "========================================");
            if (startStatus == PlayerStateReadStatus::OK) {
                Log("狀態機", "[開始] Bot 開始狩獵！");
                SetRelativeOnlyCombatMode(false, "啟動前讀值正常");
            } else {
                Logf("狀態機", "[開始] 純相對座標模式啟動: %s", startReason[0] ? startReason : "遊戲未就緒");
                SetRelativeOnlyCombatMode(true, startReason);
            }
            Log("狀態機", "========================================");

            TransitionState(BotState::HUNTING, "ToggleBotActiveStart", NULL);
        }
        UIAddLog("[Bot] 已開始狩獵 (F11)");
    } else {
        ForceStopBot();
        UIAddLog("[Bot] 已暫停狩獵 (F11)");
    }
}
void ResetBotTarget() {
    ResetCombatRuntimeState();
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
// 切換技能欄位
// ============================================================
static void SwitchBar(HWND hWnd, BYTE barKey) {
    if (!hWnd) return;
    if (s_curBar == barKey) return;
    SendKeyDirect(hWnd, barKey);
    SleepJitter(80);
    s_curBar = (char)barKey;
    Logf("技能", "[欄位] 切換%s欄 (%s)",
        barKey == VK_F1 ? "攻擊" : "輔助",
        barKey == VK_F1 ? "F1" : "F2");
}
// ============================================================
// 喝水邏輯
// ============================================================
static void DoPotionTick(HWND hWnd, const PlayerState& st) {
    static int callCount = 0;
    static DWORD lastCallLog = 0;
    DWORD now = GetTickCount();
    callCount++;
    if (now - lastCallLog > 5000) {
        Logf("喝水", "[DoPotionTick 被調用 %d 次]", callCount);
        lastCallLog = now;
    }
    if (!hWnd) {
        static DWORD lastErrLog = 0;
        if (now - lastErrLog > 5000) {
            Log("喝水", "❌ HWND 為 NULL，無法發送按鍵！");
            lastErrLog = now;
        }
        return;
    }
    int hpPct = st.maxHp > 0 ? (st.hp * 100) / st.maxHp : 100;
    int mpPct = st.maxMp > 0 ? (st.mp * 100) / st.maxMp : 100;
    int spPct = st.maxSp > 0 ? (st.sp * 100) / st.maxSp : 100;
    int hpTh = g_cfg.hp_potion_pct.load();
    int mpTh = g_cfg.mp_potion_pct.load();
    int spTh = g_cfg.sp_potion_pct.load();
    BYTE hpKey = g_cfg.key_hp_potion;
    BYTE mpKey = g_cfg.key_mp_potion;
    BYTE spKey = g_cfg.key_sp_potion;
    static DWORD lastDiagLog = 0;
    if (now - lastDiagLog > 10000) {
        Logf("喝水", "狀態: HP=%d/%d=%d%%(閾值<%d%%) MP=%d/%d=%d%%(閾值<%d%%) SP=%d/%d=%d%%(閾值<%d%%)",
            st.hp, st.maxHp, hpPct, hpTh,
            st.mp, st.maxMp, mpPct, mpTh,
            st.sp, st.maxSp, spPct, spTh);
        lastDiagLog = now;
    }
    if (hpPct < hpTh) {
        if (hpKey != 0) {
            SendKeyInputFocused(hpKey, hWnd);
        }
    }
    if (mpPct < mpTh) {
        if (mpKey != 0) {
            SendKeyInputFocused(mpKey, hWnd);
        }
    }
    if (spPct < spTh) {
        if (spKey != 0) {
            SendKeyInputFocused(spKey, hWnd);
        }
    }
}

// ============================================================
// 寵物餵食邏輯
// ============================================================
static DWORD s_lastPetFeedTime = 0;
static void DoPetFeedTick(HWND hWnd) {
    if (!g_cfg.feed_pet.load()) return;
    if (!hWnd) return;

    DWORD now = GetTickCount();
    int intervalSec = g_cfg.feed_pet_interval.load();
    if (intervalSec <= 0) intervalSec = 60;
    DWORD intervalMs = (DWORD)intervalSec * 1000;

    if (now - s_lastPetFeedTime < intervalMs) return;
    s_lastPetFeedTime = now;

    Log("寵物", "[餵食] 開始餵食...");
    // 1. 點擊食料（拿起）
    ClickAtDirect(hWnd, Coords::食料.x, Coords::食料.z);
    Sleep(200);
    // 2. 拖到寵物卡上方，右鍵使用餵食
    DragFeedToPet(hWnd, Coords::食料.x, Coords::食料.z, Coords::寵物卡.x, Coords::寵物卡.z);
    Sleep(200);
    // 3. 左鍵放回原位
    ClickAtDirect(hWnd, Coords::食料.x, Coords::食料.z);
    Sleep(100);
    Log("寵物", "[餵食] 完成");
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

// ============================================================
// 失敗目標管理
// ============================================================
static void AddFailedTarget(DWORD targetId) {
    DWORD now = GetTickCount();
    for (int i = 0; i < s_failedCount; i++) {
        if (s_failedTargets[i].id == targetId) {
            s_failedTargets[i].failTime = now;
            return;
        }
    }
    if (s_failedCount < MAX_FAILED_TARGETS) {
        s_failedTargets[s_failedCount].id = targetId;
        s_failedTargets[s_failedCount].failTime = now;
        s_failedCount++;
    } else {
        int oldest = 0;
        for (int i = 1; i < MAX_FAILED_TARGETS; i++) {
            if (s_failedTargets[i].failTime < s_failedTargets[oldest].failTime) oldest = i;
        }
        s_failedTargets[oldest].id = targetId;
        s_failedTargets[oldest].failTime = now;
    }
}
static bool IsFailedTarget(DWORD targetId) {
    DWORD now = GetTickCount();
    // 每 60 秒清理一次過期項目（使用 s_lastFailedPurge 避免每次都遍歷）
    if (now - s_lastFailedPurge > 60000) {
        for (int i = s_failedCount - 1; i >= 0; i--) {
            if (now - s_failedTargets[i].failTime > FAILED_TARGET_COOLDOWN) {
                for (int j = i; j < s_failedCount - 1; j++) s_failedTargets[j] = s_failedTargets[j+1];
                s_failedCount--;
            }
        }
        s_lastFailedPurge = now;
    }
    for (int i = 0; i < s_failedCount; i++) {
        if (s_failedTargets[i].id == targetId) return true;
    }
    return false;
}
static void ClearFailedTarget(DWORD targetId) {
    for (int i = 0; i < s_failedCount; i++) {
        if (s_failedTargets[i].id == targetId) {
            for (int j = i; j < s_failedCount - 1; j++) s_failedTargets[j] = s_failedTargets[j+1];
            s_failedCount--;
            return;
        }
    }
}
// ============================================================
// 放棄目標
// ============================================================
static void GiveUpTarget(GameHandle* gh, const char* reason) {
    DWORD now = GetTickCount();
    Logf("戰鬥", "放棄目標 ID=0x%08X 原因: %s (擊殺:%d)",
        s_currentTargetId, reason, s_killCount);
    AddFailedTarget(s_currentTargetId);
    s_currentTargetId = 0;
    if (now - s_lastIntentChange > 100) {
        s_combatIntent = CombatIntent::SEEKING;
        s_lastIntentChange = now;
    }
    s_engageStartTime = 0;
    if (gh && gh->hProcess && gh->baseAddr) {
        SafeNtWPM(gh->hProcess, gh->baseAddr + OffsetConfig::TargetID(), (DWORD)0xFFFFFFFF);
        SafeNtWPM(gh->hProcess, gh->baseAddr + OffsetConfig::TargetHasTarget(), (int)0);
    }
    // ── 清除遊戲內鎖定框（按 ESC 取消目標瞄準）──
    if (gh && gh->hWnd) {
        SendKeyDirect(gh->hWnd, VK_ESCAPE);
        Sleep(30);
    }
}
// ============================================================
// 輔助技能
// ============================================================
static void DoSupportSkill(HWND hWnd) {
    if (!hWnd || !g_cfg.auto_support.load()) return;
    DWORD now = GetTickCount();
    int intervalSec = g_cfg.buffCastInterval.load();
    if (intervalSec <= 0) intervalSec = 30;
    if (now - g_cfg.lastBuffTime.load() < (DWORD)(intervalSec * 1000)) return;
    int buffCount = g_cfg.buffSkillCount.load();
    if (buffCount < 1) buffCount = 1;
    if (buffCount > BotConfig::MAX_AUX_SKILLS) buffCount = BotConfig::MAX_AUX_SKILLS;
    static int s_buffIndex = 0;
    int idx = s_buffIndex % buffCount;
    BYTE numKey = (BYTE)('1' + idx);
    SendKeyDirect(hWnd, g_cfg.buffBarKey.load());
    SleepJitter(60);
    SendKeyDirect(hWnd, numKey);
    SleepJitter(60);
    SendKeyDirect(hWnd, g_cfg.attackBarKey.load());
    g_cfg.lastBuffTime.store(now);
    s_buffIndex = (idx + 1) % buffCount;
    Logf("技能", ">>> [輔助] F2 技能 %d，%ds 後再觸發", idx + 1, intervalSec);
}
// ============================================================
// TryAutoTarget - 直接視覺鎖怪（Win7~Win11 通用，跳過記憶體偏移）
// ============================================================
static void TryAutoTarget(HWND hWnd, GameHandle* gh) {
    if (s_combatIntent != CombatIntent::SEEKING) return;
    if (!gh || !hWnd) return;

    // 直接使用視覺鎖怪（像素辨識血條）
    TargetLockResult tl = {};
    if (TargetLock_Click(hWnd, &tl)) {
        s_currentTargetId = 0;
        s_targetLockTime = GetTickCount();
        s_engageStartTime = GetTickCount();
        s_lastTargetHp = 0;
        s_hpCheckTime = GetTickCount();
        s_combatIntent = CombatIntent::ENGAGING;

        Logf("戰鬥", "🎯 視覺鎖怪成功 (%d,%d)", tl.gamePt.x, tl.gamePt.y);
        SleepJitter(120);
    }
}

// ============================================================
// DoCombatTick - 視覺鎖怪 + 固定點攻擊（純座標版）
// ============================================================
static void DoCombatTick(HWND hWnd, GameHandle* gh) {
    if (s_combatIntent != CombatIntent::ENGAGING) return;
    if (!gh || !hWnd) return;

    DWORD now = GetTickCount();
    DWORD interval = (DWORD)g_cfg.attackSkillInterval.load();
    if (interval < 10) interval = 10;
    if (now - s_lastCombatTick < interval) return;
    s_lastCombatTick = now;

    // 每3秒嘗試重新視覺鎖怪
    if (now - s_targetLockTime > 3000) {
        TargetLockResult tl = {};
        if (TargetLock_Click(hWnd, &tl)) {
            s_targetLockTime = now;
        } else {
            // 找不到怪物，回到尋怪狀態
            s_combatIntent = CombatIntent::SEEKING;
            return;
        }
    }

    // 攻擊
    static int castIndex = 0;
    int skillCount = g_cfg.attackSkillCount.load();
    if (skillCount < 1) skillCount = 1;
    if (skillCount > 10) skillCount = 10;

    int skillIndex = castIndex % skillCount;
    BYTE skillKey = SkillKeyFromIndex(skillIndex);
    SendKeyDirect(hWnd, skillKey);
    Sleep(IsWin7Platform() ? 25 : 15);

    int pointIndex = castIndex % Coords::ATTACK_SCAN_COUNT;
    ClickAttackPoint(hWnd, pointIndex);
    castIndex = (castIndex + 1) % Coords::ATTACK_SCAN_COUNT;
    g_cfg.currentSkillIndex.store(castIndex % skillCount);
}

static void RunRelativeOnlyCombatTick(GameHandle* gh, DWORD now) {
    if (!gh || !gh->hWnd) return;

    if (!s_enteredHunting) {
        Log("狀態機", "========================================");
        Log("狀態機", "[HUNTING] 進入純相對座標固定點戰鬥模式");
        Logf("狀態機", "  └- 中心點: (%d,%d) | 掃打點數: %d",
            ATTACK_CENTER_X, ATTACK_CENTER_Y, Coords::ATTACK_SCAN_COUNT);
        Log("狀態機", "========================================");
        s_combatIntent = CombatIntent::ENGAGING;
        s_currentTargetId = 0;
        s_curBar = -1;
        s_relativeScanIndex = 0;
        s_relativeSkillIndex = 0;
        SwitchBar(gh->hWnd, g_cfg.attackBarKey.load());
        s_enteredHunting = true;
    }

    if (g_cfg.auto_pickup.load()) {
        DWORD pickupDelay = (DWORD)g_cfg.pickup_interval_ms.load();
        if (pickupDelay < 200) pickupDelay = 200;
        if (now - s_lastPickupTime > pickupDelay) {
            s_lastPickupTime = now;
            BYTE key = g_cfg.key_pickup;
            SendKeyInputFocused(key, gh->hWnd);
        }
    }

    DoSupportSkill(gh->hWnd);

    DWORD interval = (DWORD)g_cfg.attackSkillInterval.load();
    if (interval < 10) interval = 10;
    if (now - s_lastCombatTick < interval) return;
    s_lastCombatTick = now;

    int skillCount = g_cfg.attackSkillCount.load();
    if (skillCount < 1) skillCount = 1;
    if (skillCount > 10) skillCount = 10;

    int skillIndex = s_relativeSkillIndex % skillCount;
    BYTE skillKey = SkillKeyFromIndex(skillIndex);
    SendKeyDirect(gh->hWnd, skillKey);
    Sleep(IsWin7Platform() ? 25 : 15);

    int pointIndex = s_relativeScanIndex % Coords::ATTACK_SCAN_COUNT;
    ClickAttackPoint(gh->hWnd, pointIndex);

    s_relativeSkillIndex = (s_relativeSkillIndex + 1) % skillCount;
    s_relativeScanIndex = (s_relativeScanIndex + 1) % Coords::ATTACK_SCAN_COUNT;
    g_cfg.currentSkillIndex.store(s_relativeSkillIndex);

    static int s_logCounter = 0;
    if (++s_logCounter >= 16) {
        const Coords::ScanPoint* scanPoints = Coords::GetAttackScanPoints();
        const Coords::ScanPoint& pt = scanPoints[pointIndex];
        Logf("戰鬥", "[%c] 固定點%d/%d(%d,%d)",
            skillKey, pointIndex + 1, Coords::ATTACK_SCAN_COUNT, pt.x, pt.z);
        s_logCounter = 0;
    }
    s_wasInHunting = true;
}
static int CountPotionSlotsInRange(const std::vector<InvSlot>& slots, int start, int end) {
    int occupiedCount = 0;
    for (const auto& slot : slots) {
        if (slot.slotIdx >= start && slot.slotIdx <= end && slot.itemId != 0) {
            occupiedCount++;
        }
    }
    return occupiedCount;
}
static DWORD GetTransitionConfirmTimeoutMs(DWORD minDelayMs, DWORD floorMs) {
    if (minDelayMs < floorMs) minDelayMs = floorMs;
    DWORD timeout = minDelayMs * 4;
    if (timeout < floorMs) timeout = floorMs;
    if (timeout > 30000) timeout = 30000;
    return timeout;
}
static bool ConfirmReturnArrival(const PlayerState& st, char* reason, size_t reasonSize) {
    bool validPos = HasUsableWorldPos(st.x, st.z);
    bool mapChanged = !s_returnSourceValid || st.mapId != s_returnSourceMapId;
    bool inTown = IsTownMap(st.mapId);
    bool townConfigured = HasTownMapConfig();

    if (inTown) {
        _snprintf_s(reason, reasonSize, _TRUNCATE, "命中城鎮 MapId=%d", st.mapId);
        return true;
    }
    if (validPos && mapChanged) {
        _snprintf_s(reason, reasonSize, _TRUNCATE, townConfigured ?
            "Map 已變更且座標有效 (Map=%d)" :
            "未配置 townMapIds，改用 map 變更確認 (Map=%d)", st.mapId);
        return true;
    }
    _snprintf_s(reason, reasonSize, _TRUNCATE,
        "尚未確認到城 (Map=%d, mapChanged=%d, inTown=%d)",
        st.mapId, mapChanged ? 1 : 0, inTown ? 1 : 0);
    return false;
}
static bool ConfirmBackToFieldArrival(const PlayerState& st, char* reason, size_t reasonSize) {
    bool validPos = HasUsableWorldPos(st.x, st.z);
    bool mapChanged = !s_backToFieldSourceValid || st.mapId != s_backToFieldSourceMapId;
    bool inTown = IsTownMap(st.mapId);

    if (s_backToFieldSourceWasTown) {
        if (validPos && !inTown && mapChanged) {
            _snprintf_s(reason, reasonSize, _TRUNCATE, "已離開城鎮且切換地圖 (Map=%d)", st.mapId);
            return true;
        }
        _snprintf_s(reason, reasonSize, _TRUNCATE,
            "仍未確認離開城鎮 (Map=%d, mapChanged=%d, inTown=%d)",
            st.mapId, mapChanged ? 1 : 0, inTown ? 1 : 0);
        return false;
    }

    if (validPos && !inTown) {
        _snprintf_s(reason, reasonSize, _TRUNCATE,
            mapChanged ? "野外確認完成且地圖已變更 (Map=%d)" :
            "野外確認完成，使用有效座標恢復戰鬥 (Map=%d)",
            st.mapId);
        return true;
    }
    _snprintf_s(reason, reasonSize, _TRUNCATE,
        "返回野外尚未確認 (Map=%d, inTown=%d)", st.mapId, inTown ? 1 : 0);
    return false;
}

// ============================================================
// BotTick - 主迴圈
// ============================================================

// ── 敵人接近偵測 ──────────────────────────────────────
struct EntityPosHistory {
    DWORD id = 0;
    float x = 0, z = 0;
    DWORD lastSeen = 0;
};
static EntityPosHistory s_entityHistory[64];
static DWORD s_lastApproachCheck = 0;
static bool s_approachThreatTriggered = false;  // 防止短時間重複觸發

// 檢查是否有 entity 高速接近玩家，是則觸發回城
static bool CheckApproachingThreats(GameHandle* gh, const PlayerState& st, DWORD now) {
    if (!g_cfg.enemy_approach_detect.load()) return false;

    DWORD interval = 500;  // 每 500ms 檢查一次
    if (now - s_lastApproachCheck < interval) return false;
    s_lastApproachCheck = now;

    float speedThresh = g_cfg.enemy_approach_speed.load();
    float distThresh = g_cfg.enemy_approach_dist.load();

    std::vector<Entity> monsters;
    ScanEntities(gh, &monsters, NULL);
    if (monsters.empty()) return false;

    bool threatFound = false;
    for (const auto& m : monsters) {
        if (m.dist > distThresh) continue;  // 超過偵測距離

        // 在歷史中找這個 entity
        EntityPosHistory* prev = nullptr;
        for (int i = 0; i < 64; i++) {
            if (s_entityHistory[i].id == m.id) { prev = &s_entityHistory[i]; break; }
        }

        if (!prev) {
            // 新 entity，記錄下來
            for (int i = 0; i < 64; i++) {
                if (s_entityHistory[i].id == 0) {
                    s_entityHistory[i].id = m.id;
                    s_entityHistory[i].x = m.x;
                    s_entityHistory[i].z = m.z;
                    s_entityHistory[i].lastSeen = now;
                    break;
                }
            }
            continue;
        }

        // 計算時間差（秒）
        float dt = (now - prev->lastSeen) / 1000.0f;
        if (dt < 0.1f) continue;  // 間隔太短不計算

        // 計算位移和速度
        float dx = m.x - prev->x;
        float dz = m.z - prev->z;
        float moveSpeed = std::sqrt(dx * dx + dz * dz) / dt;

        // 計算朝向玩家的徑向速度（負值=接近，正值=遠離）
        float prevDist = std::sqrt((prev->x - st.x) * (prev->x - st.x) + (prev->z - st.z) * (prev->z - st.z));
        float curDist = m.dist;
        float radialSpeed = (curDist - prevDist) / dt;

        // 速度足夠大且正在接近（radialSpeed < 0）
        if (radialSpeed < -speedThresh && moveSpeed > speedThresh * 0.5f) {
            threatFound = true;
            if (!s_approachThreatTriggered) {
                Logf("狀態機", "[接近威脅] ID=%d 速度=%.1f 距離=%.1f → 觸發回城！",
                    m.id, moveSpeed, curDist);
                s_approachThreatTriggered = true;
            }
            break;
        }

        // 更新歷史
        prev->x = m.x;
        prev->z = m.z;
        prev->lastSeen = now;
    }

    // 清理過期歷史（5秒沒看到就移除）
    for (int i = 0; i < 64; i++) {
        if (s_entityHistory[i].id != 0 && now - s_entityHistory[i].lastSeen > 5000) {
            s_entityHistory[i].id = 0;
        }
    }

    // 威脅消失後解除鎖定（讓下次可以再觸發）
    if (!threatFound) s_approachThreatTriggered = false;

    if (threatFound && !s_approachThreatTriggered) {
        // 有威脅但已被處理過（本輪已觸發），不再重複觸發
        return true;
    }

    return threatFound;
}
void BotTick(GameHandle* gh) {
    static bool s_platformLogged = false;
    if (!s_platformLogged) {
        InitPlatformDetect();
        Logf("系統", "=== 平台檢測: %s === (輸入模式: Game.exe 視窗內輸入 / YOLO 優先)",
            IsWin7Platform() ? "Windows 7" : "Windows 10/11");
        s_platformLogged = true;
    }

    // ── 更新遊戲視窗 HWND（校正系統用）──
    {
        extern void SetGameHwndForCalib(HWND);
        if (gh && gh->hWnd) SetGameHwndForCalib(gh->hWnd);
    }

    // ── HWND 失效防呆：若 gh 的 HWND 失效，嘗試重新取得 ──
    if (gh) {
        bool hwndValid = (gh->hWnd != NULL) && IsWindow(gh->hWnd);
        if (!hwndValid) {
            GameHandle tmpGh2;
            if (FindGameProcess(&tmpGh2) && tmpGh2.hWnd) {
                gh->hWnd = tmpGh2.hWnd;
                gh->pid = tmpGh2.pid;
                gh->baseAddr = tmpGh2.baseAddr;
                gh->attached = tmpGh2.attached;
                Log("Bot", "HWND 失效，已重新取得");
            }
        } else if (!gh->baseAddr || !gh->attached) {
            // baseAddr 失效但 HWND 還活著，單獨刷新 baseAddr（每 10 秒一次）
            static DWORD s_lastBaseAddrRefresh = 0;
            DWORD now = GetTickCount();
            if (now - s_lastBaseAddrRefresh > 10000) {
                DWORD newBase = RefreshGameBaseAddress(gh);
                if (newBase) {
                    Logf("Bot", "✅ baseAddr 刷新成功: 0x%08X -> 0x%08X", gh->baseAddr, newBase);
                } else {
                    Log("Bot", "⚠️ baseAddr 刷新失敗（遊戲可能已關閉）");
                }
                s_lastBaseAddrRefresh = now;
            }
        }
    }

    // ── 遊戲視窗解析度驗證（僅首次）──
    {
        static bool s_resolutionChecked = false;
        if (!s_resolutionChecked && gh && gh->hWnd) {
            RECT rc;
            if (GetClientRect(gh->hWnd, &rc)) {
                int w = rc.right;
                int h = rc.bottom;
                if (w != 1024 || h != 768) {
                    Logf("解析度", "⚠️ 遊戲視窗並非 1024x768（目前: %dx%d），可能影響外掛精確度", w, h);
                } else {
                    Logf("解析度", "✅ 遊戲視窗解析度正常 (%dx%d)", w, h);
                }
            }
            s_resolutionChecked = true;
        }
    }

    // ── 嘗試連接 NetHook 共享記憶體（每 5 秒重試一次，避免刷屏）──
    // ✅ BUG-005 FIX: 添加節流，避免每 tick 都嘗試連線
    static bool s_nethookConnected = false;
    static DWORD s_lastNethookRetry = 0;
    if (!s_nethookConnected || !NetHookShmem_IsConnected()) {
        DWORD nowRetry = GetTickCount();
        if (nowRetry - s_lastNethookRetry > 5000) {
            if (NetHookShmem_Connect()) {
                s_nethookConnected = true;
                Log("NetHook", "✅ 已連接到 NetHook 共享記憶體");
            } else {
                s_nethookConnected = false;
            }
            s_lastNethookRetry = nowRetry;
        }
    }

    // gh fallback - 增強版本：始終同步最新的全域句柄
    static GameHandle s_ghFallback;
    static DWORD s_lastGhLog = 0;
    DWORD nowGh = GetTickCount();

    // 始終嘗試獲取最新的全域句柄（即使傳入的 gh 有效）
    GameHandle tmpGh = GetGameHandle();
    // 修復：只要 hProcess 有效就同步，baseAddr=0 時仍需同步以便後續刷新
    if (tmpGh.hProcess) {
        s_ghFallback = tmpGh;
        gh = &s_ghFallback;
        if (nowGh - s_lastGhLog > 5000) {
            Logf("Bot", "✅ gh 同步: hProcess=%p base=0x%08X attached=%d hWnd=%p",
                s_ghFallback.hProcess, s_ghFallback.baseAddr, s_ghFallback.attached, s_ghFallback.hWnd);
            s_lastGhLog = nowGh;
        }
    } else if (!gh || !gh->hProcess) {
        // 沒有有效的 gh
        static DWORD s_lastGhFailLog = 0;
        if (nowGh - s_lastGhFailLog > 5000) {
            Logf("Bot", "❌ gh 無效: 參數=%p hProcess=%p 全域 hProcess=%p",
                gh, gh ? gh->hProcess : 0, tmpGh.hProcess);
            s_lastGhFailLog = nowGh;
        }
    }

    // ✅ BUG-004 FIX: 減少 UI 刷新頻率，降低鎖競爭（500ms → 2000ms）
    static DWORD s_lastUiRefresh = 0;
    if (gh && gh->attached && nowGh - s_lastUiRefresh > 2000) {
        RefreshUiPlayerSnapshot(gh);
        s_lastUiRefresh = nowGh;
    }

    // ── Kami 卡密驗證 ──
    if (!IsLicenseValid()) {
        static DWORD s_lastKamiLog = 0;
        DWORD nowKami = GetTickCount();
        if (nowKami - s_lastKamiLog > 5000) {
            Log("認證", "❌ 卡密未驗證，功能已鎖定");
            s_lastKamiLog = nowKami;
        }
        Sleep(1000);
        return;
    }

    if (!g_cfg.active.load()) {
        Sleep(100);
        return;
    }
    // 背包資料先走共享記憶體 / 實體掃描，必要時回退 ScanInventory()
    {
        static DWORD s_lastInvCache = 0;
        DWORD nowCache = GetTickCount();
        if (nowCache - s_lastInvCache > 1000) {
            CacheInventory(gh);
            s_lastInvCache = nowCache;
        }
    }

    // ═══════════════════════════════════════════════════════════
    // 視覺模式（視覺辨識為主，取代記憶體讀取）
    // 支援兩種模式：
    //   - use_visual_mode: 像素掃描（原有邏輯）
    //   - use_yolo_mode: YOLO 物件偵測（整合到 ScanVisualMonsters）
    // ═══════════════════════════════════════════════════════════
    if (g_cfg.use_visual_mode.load() || g_cfg.use_yolo_mode.load()) {

        // ── YOLO 模式初始化檢查 ──
        if (g_cfg.use_yolo_mode.load()) {
            extern bool IsYoloReady();
            extern float GetYoloInferenceTime();
            static bool s_yoloWarned = false;
            if (!IsYoloReady() && !s_yoloWarned) {
                Log("YOLO", "⚠️ YOLO 偵測器未就緒，使用像素掃描");
                s_yoloWarned = true;
            } else if (IsYoloReady() && s_yoloWarned) {
                // YOLO 恢復了
                s_yoloWarned = false;
                Log("YOLO", "✅ YOLO 偵測器已就緒");
            }
        }
        // ── HWND 驗證 ──
        if (!gh || !gh->hWnd || !IsWindow(gh->hWnd)) {
            Sleep(100);
            return;
        }

        static DWORD s_lastVisualLog = 0;
        DWORD nowVisual = GetTickCount();
        if (nowVisual - s_lastVisualLog > 5000) {
            Logf("視覺", "BotTick 視覺模式運行中, state=%d", GetBotState());
            s_lastVisualLog = nowVisual;
        }

        BotState currentState = GetBotState();

        // ── 讀取視覺玩家狀態 ──
        VisualPlayerState vs = {};
        static VisualPlayerState s_lastVs = {};
        if (ReadVisualPlayerState(gh->hWnd, &vs) && vs.found) {
            s_lastVs = vs;
        } else {
            // 血條讀取失敗時保留上一筆視覺狀態；怪物掃描仍優先走 YOLO。
            vs = s_lastVs;
            static DWORD s_visualFailLog = 0;
            if (nowVisual - s_visualFailLog > 3000) {
                Log("視覺", "⚠️ 血條讀取失敗，本幀沿用上一筆；怪物掃描仍優先 YOLO");
                s_visualFailLog = nowVisual;
            }
        }

        // ── 視覺怪物掃描 ──
        VisualMonster vMonsters[32] = {};
        int vMonsterCount = 0;
        DWORD scanStart = GetTickCount();
        if (currentState == BotState::HUNTING || currentState == BotState::IDLE) {
            vMonsterCount = ScanVisualMonsters(gh->hWnd, vMonsters, 32);

            // ── YOLO 推論時間日誌（每 5 秒一次）──
            if (g_cfg.use_yolo_mode.load()) {
                extern float GetYoloInferenceTime();
                float inferenceMs = GetYoloInferenceTime();
                if (inferenceMs > 0) {
                    static DWORD s_lastYoloLog = 0;
                    if (nowVisual - s_lastYoloLog > 5000) {
                        Logf("YOLO", "推論時間: %.2f ms, 偵測: %d 個目標", inferenceMs, vMonsterCount);
                        s_lastYoloLog = nowVisual;
                    }
                }
            }

            if (vMonsterCount > 0) {
                // 視覺模式鎖怪
                if (s_combatIntent == CombatIntent::SEEKING && vMonsterCount > 0) {
                    VisualMonster* best = &vMonsters[0];  // 已按優先級排序
                    ClickAtDirect(gh->hWnd, best->relX, best->relY);
                    s_combatIntent = CombatIntent::ENGAGING;
                    s_targetLockTime = nowVisual;
                    if (nowVisual - s_lastVisualLog > 5000 || s_lastVisualLog == 0) {
                        const char* modeTag = g_cfg.use_yolo_mode.load() ? "YOLO" : "視覺";
                        Logf(modeTag, "🎯 鎖怪成功 (%d,%d) hp=%d%%", best->relX, best->relY, best->hpPct);
                    }
                }

                // 視覺模式攻擊
                if (s_combatIntent == CombatIntent::ENGAGING) {
                    if (nowVisual - s_targetLockTime > 3000) {
                        // 3秒後重新鎖怪
                        if (vMonsterCount > 0) {
                            VisualMonster* best = &vMonsters[0];
                            ClickAtDirect(gh->hWnd, best->relX, best->relY);
                            s_targetLockTime = nowVisual;
                        } else {
                            s_combatIntent = CombatIntent::SEEKING;
                        }
                    } else {
                        // 攻擊技能 - 使用 F1~F9 功能鍵
                        static int s_visualSkillIndex = 0;
                        int skillCount = g_cfg.attackSkillCount.load();
                        if (skillCount < 1) skillCount = 1;
                        int skillIdx = s_visualSkillIndex % skillCount;
                        BYTE skillKey = VK_F1 + skillIdx;
                        if (skillKey > VK_F9) skillKey = VK_F9;
                        SendKeyDirect(gh->hWnd, skillKey);
                        Logf("技能", "按鍵: F%d (skillIdx=%d)", skillIdx + 1, skillIdx);
                        // 攻擊定點
                        static int s_visualPointIndex = 0;
                        ClickAttackPoint(gh->hWnd, s_visualPointIndex % Coords::ATTACK_SCAN_COUNT);
                        s_visualPointIndex++;
                        s_visualSkillIndex++;
                    }
                }
            } else {
                // 找不到怪物，回到 SEEKING
                if (s_combatIntent == CombatIntent::ENGAGING) {
                    s_combatIntent = CombatIntent::SEEKING;
                }
            }
        }

        // ── 視覺掃描超時保護 ──
        DWORD scanElapsed = GetTickCount() - scanStart;
        if (scanElapsed > (DWORD)g_cfg.visual_scan_timeout_ms.load()) {
            static DWORD s_scanTimeoutLog = 0;
            if (nowVisual - s_scanTimeoutLog > 5000) {
                Logf("視覺", "⚠️ 視覺掃描超時 %dms（限制 %dms），跳過此幀",
                    scanElapsed, g_cfg.visual_scan_timeout_ms.load());
                s_scanTimeoutLog = nowVisual;
            }
            Sleep(20);
            return;
        }

        // ── 自動喝水（視覺模式）──
        if (vs.found && (currentState == BotState::HUNTING)) {
            if (vs.hpPct < g_cfg.hp_potion_pct.load() && vs.hpPct > 0) {
                SendKeyDirect(gh->hWnd, g_cfg.key_hp_potion.load());
            }
            if (vs.mpPct < g_cfg.mp_potion_pct.load() && vs.mpPct > 0) {
                SendKeyDirect(gh->hWnd, g_cfg.key_mp_potion.load());
            }
            if (vs.spPct < g_cfg.sp_potion_pct.load() && vs.spPct > 0) {
                SendKeyDirect(gh->hWnd, g_cfg.key_sp_potion.load());
            }
        }

        // ── 死亡檢測（視覺模式）──
        // found=true 表示成功讀到 HP 條，hpPct<=0 才判死亡
        // 調試日誌：顯示視覺讀取狀態
        static DWORD s_lastVisualDeadDebug = 0;
        if (nowVisual - s_lastVisualDeadDebug > 5000) {
            Logf("視覺", "  └ 調試: vs.found=%d vs.hpPct=%d vs.mpPct=%d vs.spPct=%d curState=%s",
                vs.found ? 1 : 0, vs.hpPct, vs.mpPct, vs.spPct, GetStateName(currentState));
            s_lastVisualDeadDebug = nowVisual;
        }

        if (vs.found && vs.hpPct <= 0 && currentState == BotState::HUNTING) {
            Log("視覺", "========================================");
            Log("視覺", "[HUNTING -> DEAD] 視覺檢測到死亡");
            Log("視覺", "========================================");
            TransitionState(BotState::DEAD, "VisualHpZero", NULL);
            Sleep(100);
            return;
        }

        // ── 狀態機其他狀態 ──
        if (currentState == BotState::RETURNING) {
            if (!s_returnCardSent) {
                SendKeyInputFocused(g_cfg.key_waypoint_start, gh->hWnd);
                s_returnCardSent = true;
            }
            Sleep(100);
            return;
        }

        if (currentState == BotState::DEAD) {
            // 視覺模式復活 - BUG-007/008 修復：使用全局 s_deathStartTime + 超時保護
            DWORD elapsed = nowVisual - s_deathStartTime;

            // 超時保護：60秒強制退出
            if (elapsed > 60000) {
                Log("狀態機", "❌ [視覺DEAD] 超時 60 秒，強制退出");
                TransitionState(BotState::IDLE, "VisualDeadTimeout60s", NULL);
                g_cfg.active.store(false);
                return;
            }

            // 30秒後強制回 HUNTING
            if (elapsed > 30000) {
                Log("狀態機", "[視覺DEAD] 30秒超時，強制回 HUNTING");
                TransitionState(BotState::HUNTING, "VisualDeadTimeout30s", NULL);
                return;
            }

            // 檢測 HP 是否已恢復（可能在城鎮自動復活）
            if (vs.found && vs.hpPct > 0) {
                if (s_deadRecoverySeenTime == 0) {
                    s_deadRecoverySeenTime = nowVisual;
                }
                if (nowVisual - s_deadRecoverySeenTime < 1200) {
                    SleepJitter(150);
                    return;
                }
                Log("狀態機", "[視覺DEAD -> HUNTING] HP 已恢復");
                TransitionState(BotState::HUNTING, "VisualDeadRecovered", NULL);
                return;
            }

            if (g_cfg.auto_revive.load()) {
                int reviveWait = g_cfg.revive_delay_ms.load();
                if (elapsed < (DWORD)reviveWait) {
                    SleepJitter(200);
                    return;
                }
                if (!s_reviveClicked) {
                    // BUG-010 修復：添加截圖輔助補救（與記憶體模式一致）
                    int mode = g_cfg.revive_mode.load();

                    // 調試：顯示 HWND 和座標
                    HWND hWnd = gh->hWnd;
                    int cliW = 0, cliH = 0;
                    Coords::GetGameWindowSize(hWnd, &cliW, &cliH);
                    Coords::Point revivePt;
                    if (mode == 0) revivePt = Coords::歸魂珠復活;
                    else if (mode == 1) revivePt = Coords::復活按鈕;
                    else revivePt = Coords::基本復活;
                    int clientX = cliW > 0 ? (revivePt.x * cliW) / 1024 : 0;
                    int clientY = cliH > 0 ? (revivePt.z * cliH) / 768 : 0;
                    Logf("狀態機", "[視覺DEAD] 嘗試復活: mode=%d retry=%d hWnd=%p 客戶=%dx%d 遊戲=(%d,%d) → 像素=(%d,%d)",
                        mode, s_reviveRetryCount, hWnd, cliW, cliH, revivePt.x, revivePt.z, clientX, clientY);

                    auto TryReviveClick = [&](const char* imgName, CalibIndex idx, int fx, int fz) {
                        // 每 3 次重試後嘗試截圖補救
                        if (s_reviveRetryCount >= 3 && ((s_reviveRetryCount % 3) == 0)) {
                            int ix = -1, iy = -1, score = 0;
                            if (ScreenshotAndFind(gh->hWnd, imgName, &ix, &iy, &score)) {
                                int sw = 0, sh = 0;
                                GetScreenshotWH(&sw, &sh);
                                if (sw <= 0) sw = 1024;
                                if (sh <= 0) sh = 768;
                                int rx = (int)((int64_t)ix * 1024 / sw);
                                int rz = (int)((int64_t)iy * 768 / sh);
                                Logf("狀態機", "  └ [截圖補救] %s found (%d,%d) score=%d → (%d,%d)",
                                    imgName, ix, iy, score, rx, rz);
                                ClickAtDirect(gh->hWnd, rx, rz);
                                return;
                            }
                            Logf("狀態機", "  └ [截圖補救] %s not found, fallback", imgName);
                        }
                        ClickAtCalib(gh->hWnd, (int)fx, (int)fz, idx);
                    };

                    if (mode == 0) {
                        if (s_reviveRetryCount >= 4) {
                            TryReviveClick("resurrection.png", CalibIndex::REVIVE_基本, Coords::基本復活.x, Coords::基本復活.z);
                        } else {
                            TryReviveClick("Soul_Returning_Pearl.png", CalibIndex::REVIVE_SOUL_PEARL, Coords::歸魂珠復活.x, Coords::歸魂珠復活.z);
                        }
                    } else if (mode == 1) {
                        TryReviveClick("resurrection.png", CalibIndex::REVIVE原地, Coords::復活按鈕.x, Coords::復活按鈕.z);
                    } else {
                        TryReviveClick("resurrection.png", CalibIndex::REVIVE_基本, Coords::基本復活.x, Coords::基本復活.z);
                    }

                    s_reviveClicked = true;
                    s_reviveRetryCount++;
                    Sleep(1000);
                } else {
                    s_reviveClicked = false;
                }
            }
            return;
        }

        if (currentState == BotState::TOWN_SUPPLY || currentState == BotState::BACK_TO_FIELD) {
            SupplyTick(gh);
            Sleep(50);
            return;
        }

        if (currentState == BotState::IDLE) {
            Sleep(100);
            return;
        }

        // ── 自動拾取（視覺模式）──
        if (g_cfg.auto_pickup.load() && currentState == BotState::HUNTING) {
            static DWORD s_lastPickup = 0;
            DWORD pickupDelay = (DWORD)g_cfg.pickup_interval_ms.load();
            if (nowVisual - s_lastPickup > pickupDelay) {
                s_lastPickup = nowVisual;
                BYTE key = g_cfg.key_pickup;
                SendKeyInputFocused(key, gh->hWnd);
            }
        }

        // ── 輔助技能（視覺模式）──
        if (g_cfg.buffEnabled.load() && currentState == BotState::HUNTING) {
            DoSupportSkill(gh->hWnd);
        }

        Sleep(20);
        return;
    }
    // ═══════════════════════════════════════════════════════════
    // 原有記憶體模式（向後兼容）
    // ═══════════════════════════════════════════════════════════
    // 安全碼檢測
    if (CheckSecurityCodeWindow()) {
        Sleep(100);
        return;
    }
    if (GetBotState() == BotState::PAUSED) {
        Sleep(100);
        return;
    }
    if (!gh || !gh->attached) {
        Sleep(100);
        return;
    }
    // 認證檢查已統一到上方，無需重複檢查
    static DWORD lastTickLog = 0;
    DWORD now = GetTickCount();
    if (now - lastTickLog > 5000) {
        Logf("Bot", "BotTick 正常運行中, active=%d, state=%d",
            g_cfg.active.load(), GetBotState());
        lastTickLog = now;
    }
    // ── 讀取玩家狀態（帶防呆）──
    PlayerState st;
    char readReason[160] = {0};
    PlayerStateReadStatus readStatus = ReadPlayerStateDetailed(gh, &st, readReason, sizeof(readReason));
    if (readStatus != PlayerStateReadStatus::OK) {
        BotState curState = GetBotState();
        PlayerState partialState = {};
        bool hasPartialState = RefreshUiPlayerSnapshot(gh, &partialState);

        // ✅ 放寬條件：只要讀到任何非零的 maxHp/maxMp/maxSp 就認為有基本屬性
        // 死亡時 maxHp 可能為 0 或 1，所以使用 >= 0
        bool hasBasicVitals = hasPartialState &&
            (partialState.maxHp >= 0 || partialState.maxMp >= 0 || partialState.maxSp >= 0);

        // ✅ 調試：顯示 partialState 內容
        static DWORD s_lastDebugLog = 0;
        if (now - s_lastDebugLog > 5000) {
            Logf("Debug", "PartialState: has=%d maxHp=%d maxMp=%d maxSp=%d hp=%d mp=%d sp=%d hasVitals=%d",
                hasPartialState, partialState.maxHp, partialState.maxMp, partialState.maxSp,
                partialState.hp, partialState.mp, partialState.sp, hasBasicVitals);
            Logf("Debug", "curState=%s auto_revive=%d", GetStateName(curState), g_cfg.auto_revive.load());
            s_lastDebugLog = now;
        }

        // ✅ 額外檢查：如果 partialState 有 hp 信息（即使 maxHp/maxMp/maxSp 為 0）
        bool hasHpInfo = hasPartialState;

        if (hasBasicVitals || hasHpInfo) {
            if (curState != BotState::DEAD && partialState.maxHp > 0 && partialState.hp <= 0) {
                Log("狀態機", "========================================");
                Logf("狀態機", "[%s -> DEAD] 座標失效，但 HP 已歸零，切入死亡處理",
                    GetStateName(curState));
                Log("狀態機", "========================================");
                TransitionState(BotState::DEAD, "HpReachedZeroPartial", &partialState);
                Sleep(100);
                return;
            }

            // ✅ 死亡判定：必須先確認 maxHp > 0（記憶體讀值有效）才能判死亡
            if (curState != BotState::DEAD && partialState.hp <= 0 && partialState.maxHp > 0) {
                Log("狀態機", "========================================");
                Logf("狀態機", "[%s -> DEAD] HP 歸零 (maxHp=%d)，切入死亡處理",
                    GetStateName(curState));
                Log("狀態機", "========================================");
                TransitionState(BotState::DEAD, "HpZeroPartial", &partialState);
                Sleep(100);
                return;
            }

            if (curState == BotState::DEAD && partialState.hp > 0) {
                if (s_deadRecoverySeenTime == 0) {
                    s_deadRecoverySeenTime = now;
                }
                if (now - s_deadRecoverySeenTime < 1200) {
                    Sleep(100);
                    return;
                }
                // 復活後直接進 TOWN_SUPPLY，不依賴 MapID
                const char* townNames[] = { "聖門", "商洞", "玄巖", "鳳凰" };
                int townIdx = g_cfg.town_index.load();
                Logf("狀態機", "[DEAD -> TOWN_SUPPLY] 座標失效，但 HP 已恢復 (%d/%d) town=%d/%s",
                    partialState.hp, partialState.maxHp, townIdx,
                    (townIdx >= 0 && townIdx <= 3) ? townNames[townIdx] : "?");
                Log("狀態機", "========================================");
                TransitionState(BotState::TOWN_SUPPLY, "DeadRecoveredPartial", &partialState);
                s_deadRecoverySeenTime = 0;
                Sleep(100);
                return;
            }

            if (curState == BotState::HUNTING &&
                now - s_lastDrinkCheck >= 800) {
                s_lastDrinkCheck = now;
                DoPotionTick(gh ? gh->hWnd : NULL, partialState);
            }
        }

        if (curState == BotState::HUNTING && CanUseRelativeOnlyCombat(gh)) {
            SetRelativeOnlyCombatMode(true, readReason);
            if (now - s_lastRelativeCombatLog > 5000) {
                Logf("狀態機", "⚠️ 玩家資料%s，切換純相對座標戰鬥: %s",
                    readStatus == PlayerStateReadStatus::READ_FAILED ? "讀取失敗" : "無效",
                    readReason[0] ? readReason : GetPlayerStateReadStatusName(readStatus));
                s_lastRelativeCombatLog = now;
            }
            RunRelativeOnlyCombatTick(gh, now);
            Sleep(50);
            return;
        }
        if (curState == BotState::IDLE) {
            Sleep(100);
            return;
        }

        // Bug 5 修復：所有失敗路徑都必須遞增計數
        s_consecutiveReadFail++;
        if (now - s_lastReadFailLog > 3000) {
            Logf("狀態機", "⚠️ 玩家資料%s（第 %d 次），停留在 [%s] 狀態: %s",
                readStatus == PlayerStateReadStatus::READ_FAILED ? "讀取失敗" : "無效",
                s_consecutiveReadFail, GetStateName(curState),
                readReason[0] ? readReason : GetPlayerStateReadStatusName(readStatus));
            s_lastReadFailLog = now;
        }

        if (curState == BotState::DEAD && g_cfg.auto_revive.load()) {
            HWND hWnd = gh && gh->hWnd ? gh->hWnd : NULL;
            if (!hWnd) {
                GameHandle tmpGh;
                if (FindGameProcess(&tmpGh) && tmpGh.hWnd) {
                    hWnd = tmpGh.hWnd;
                    gh->hWnd = tmpGh.hWnd;
                    gh->pid = tmpGh.pid;
                    gh->attached = tmpGh.attached;
                    gh->baseAddr = tmpGh.baseAddr;
                }
            }
            if (!hWnd) {
                if (now - s_lastNoHwndLog > 5000) {
                    Log("復活", "❌ [DEAD] hWnd 無法獲取，無法復活");
                    s_lastNoHwndLog = now;
                }
                Sleep(200);
                return;
            }
            if (!s_reviveClicked) {
                int mode = g_cfg.revive_mode.load();
                Coords::Point revivePt;
                if (mode == 0) revivePt = Coords::歸魂珠復活;
                else if (mode == 1) revivePt = Coords::復活按鈕;
                else revivePt = Coords::基本復活;

                // 調試：顯示視窗大小和座標轉換
                RECT rc = {};
                if (GetClientRect(hWnd, &rc)) {
                    Logf("復活", "[DEAD] 調試: IsWin11=%d, mode=%d, 視窗=%dx%d, 相對座標=(%d,%d)",
                        Coords::IsWin11() ? 1 : 0, mode,
                        rc.right - rc.left, rc.bottom - rc.top,
                        revivePt.x, revivePt.z);
                }

                Logf("復活", "[DEAD] 點擊復活 mode=%d, 座標=(%d,%d), hWnd=%p",
                    mode, revivePt.x, revivePt.z, hWnd);
                ClickAtDirect(hWnd, revivePt.x, revivePt.z);
                s_reviveClicked = true;
                s_reviveRetryCount++;
            } else {
                s_reviveClicked = false;
            }
            Sleep(500);
            return;
        }

        if (s_consecutiveReadFail >= 10) {
            Logf("狀態機", "❌ 玩家資料連續異常 10 次，從 [%s] 強制回 IDLE", GetStateName(curState));
            TransitionState(BotState::IDLE, "PlayerStateUnavailable", NULL);
            g_cfg.active.store(false);
            s_consecutiveReadFail = 0;
        }
        Sleep(100);
        return;
    }
    s_consecutiveReadFail = 0;
    if (IsRelativeOnlyCombatMode()) {
        SetRelativeOnlyCombatMode(false, "玩家資料已恢復");
    }

    BotState currentState = GetBotState();
    int hpPct = st.maxHp > 0 ? (st.hp * 100) / st.maxHp : 100;
    int mpPct = st.maxMp > 0 ? (st.mp * 100) / st.maxMp : 100;
    bool autoSupplyEnabled = g_cfg.auto_supply.load();
    // ── RETURNING ──
    if (currentState == BotState::RETURNING) {
        if (!s_loggedReturn) {
            Log("狀態機", "========================================");
            Logf("狀態機", "[RETURNING] 進入回城狀態！");
            Logf("狀態機", "  └- 地圖: MapId=%d, x=%.1f, z=%.1f", st.mapId, st.x, st.z);
            s_loggedReturn = true;
        }
        if (!s_returnCardSent) {
            BYTE waypointKey = g_cfg.key_waypoint_start;
            SendKeyInputFocused(waypointKey, gh->hWnd);
            s_returnCardSent = true;
        }
        if (s_returnStartTime == 0) s_returnStartTime = now;
        DWORD returnElapsed = now - s_returnStartTime;
        DWORD teleportDelay = (DWORD)g_cfg.teleport_delay_ms.load();
        if (returnElapsed >= teleportDelay) {
            char confirmReason[160] = {0};
            if (ConfirmReturnArrival(st, confirmReason, sizeof(confirmReason))) {
                Logf("狀態機", "[RETURNING -> TOWN_SUPPLY] 傳送確認成功（%dms）: %s",
                    returnElapsed, confirmReason);
                TransitionState(BotState::TOWN_SUPPLY, confirmReason, &st);
                Sleep(100);
                return;
            }
            DWORD confirmTimeout = GetTransitionConfirmTimeoutMs(teleportDelay, 12000);
            if (returnElapsed >= confirmTimeout) {
                Logf("狀態機", "❌ [RETURNING] 傳送確認超時（%dms）: %s",
                    returnElapsed, confirmReason);
                TransitionState(BotState::IDLE, "ReturningConfirmTimeout", &st);
                g_cfg.active.store(false);
                Sleep(100);
                return;
            }
        }
        Sleep(100);
        return;
    }
    // ── TOWN_SUPPLY / BACK_TO_FIELD ──
    if (currentState == BotState::TOWN_SUPPLY || currentState == BotState::BACK_TO_FIELD) {
        SupplyTick(gh);
        Sleep(50);
        return;
    }
    // ── DEAD ──
    if (currentState == BotState::DEAD) {
        // ✅ 添加調試日誌確認 DEAD 狀態被觸發
        static DWORD s_lastDeadLog = 0;
        if (now - s_lastDeadLog > 3000) {
            Logf("Debug", "[DEAD] st.hp=%d st.maxHp=%d elapsed=%lu auto_revive=%d",
                st.hp, st.maxHp, now - s_deathStartTime, g_cfg.auto_revive.load());
            s_lastDeadLog = now;
        }
        if (!s_enteredDeadState) {
            Log("狀態機", "========================================");
            Logf("狀態機", "[DEAD] 進入死亡狀態！HP=%d/%d", st.hp, st.maxHp);
            Log("狀態機", "========================================");
            s_enteredDeadState = true;
            s_reviveRetryCount = 0;
            s_reviveClicked = false;
        }
        DWORD elapsed = now - s_deathStartTime;
        // DEAD 超时保护：超过 60 秒强制退出（防止复活失败时无限卡死）
        if (elapsed > 60000) {
            Log("狀態機", "❌ [DEAD] 超時 60 秒，強制退出");
            TransitionState(BotState::IDLE, "DeadTimeout60s", NULL);
            g_cfg.active.store(false);
            return;
        }
        // Bug 2 修復：30秒後如果還沒復活，強制放棄並回到 HUNTING
        if (elapsed > 30000) {
            Log("狀態機", "[DEAD] 30秒超時，嘗試復活失敗，強制回 HUNTING");
            TransitionState(BotState::HUNTING, "DeadTimeout30sForceRecover", NULL);
            return;
        }
        if (st.hp > 0) {
            if (s_deadRecoverySeenTime == 0) {
                s_deadRecoverySeenTime = now;
            }
            if (now - s_deadRecoverySeenTime < 1200) {
                SleepJitter(150);
                return;
            }
            s_enteredDeadState = false;
            // 檢查角色位置：如果坐標顯示在野外，需要先回城
            bool inField = (st.x != 0.0f || st.z != 0.0f) && (st.mapId == 0 || st.mapId == -1);
            if (inField && g_cfg.auto_revive.load()) {
                // 坐標無效或為 0 表示可能在城鎮；坐標有效但 mapId=0 也視為可能在城鎮
                // 如果確認在野外，需要先發回城卡
                const char* townNames[] = { "聖門", "商洞", "玄巖", "鳳凰" };
                int townIdx = g_cfg.town_index.load();
                Logf("狀態機", "[DEAD] HP恢復，在野外，需要回城 town=%d/%s",
                    townIdx, (townIdx >= 0 && townIdx <= 3) ? townNames[townIdx] : "?");
                // 發送回城卡，進入 RETURNING 流程
                SendKeyInputFocused(g_cfg.key_waypoint_start, gh->hWnd);
                s_returnCardSent = true;
                s_returnStartTime = now;
                TransitionState(BotState::RETURNING, "DeadRecoverNeedReturn", &st);
                s_deathStartTime = 0;
                s_deadRecoverySeenTime = 0;
                return;
            }
            // 在城鎮或無法判斷位置，直接進入 TOWN_SUPPLY
            char reason[64] = {0};
            const char* townNames[] = { "聖門", "商洞", "玄巖", "鳳凰" };
            int townIdx = g_cfg.town_index.load();
            _snprintf_s(reason, sizeof(reason), _TRUNCATE, "DeadRecoveredTown%d_%s", townIdx,
                       (townIdx >= 0 && townIdx <= 3) ? townNames[townIdx] : "Unknown");
            Logf("狀態機", "[DEAD -> TOWN_SUPPLY] HP恢復 (%d > 0, town=%d/%s)",
                 st.hp, townIdx, (townIdx >= 0 && townIdx <= 3) ? townNames[townIdx] : "?");
            TransitionState(BotState::TOWN_SUPPLY, reason, &st);
            s_deathStartTime = 0;
            s_deadRecoverySeenTime = 0;
            return;
        }
        s_deadRecoverySeenTime = 0;
        if (g_cfg.auto_revive.load()) {
            int reviveWait = g_cfg.revive_delay_ms.load();
            if (elapsed < (DWORD)reviveWait) {
                SleepJitter(200);
                return;
            }
            if (!s_reviveClicked) {
                int mode = g_cfg.revive_mode.load();
                Logf("狀態機", "[DEAD] 嘗試復活: mode=%d retry=%d", mode, s_reviveRetryCount);

                // 復活優先使用校正座標；多次失敗後才退回截圖輔助
                auto TryReviveClick = [&](const char* imgName, CalibIndex idx, int fx, int fz) {
                    if (s_reviveRetryCount >= 3 && ((s_reviveRetryCount % 3) == 0)) {
                        int ix = -1, iy = -1, score = 0;
                        if (ScreenshotAndFind(gh->hWnd, imgName, &ix, &iy, &score)) {
                            int sw = 0, sh = 0;
                            GetScreenshotWH(&sw, &sh);
                            if (sw <= 0) sw = 1024;
                            if (sh <= 0) sh = 768;
                            // 遊戲內座標 0-1023 / 0-767
                            int rx = (int)((int64_t)ix * 1024 / sw);
                            int rz = (int)((int64_t)iy * 768 / sh);
                            Logf("狀態機", "  └ [截圖補救] %s found pixel(%d,%d) score=%d → 遊戲座標(%d,%d)",
                                imgName, ix, iy, score, rx, rz);
                            ClickAtDirect(gh->hWnd, rx, rz);
                            return;
                        }
                        Logf("狀態機", "  └ [截圖補救] %s not found, fallback (%d, %d)", imgName, fx, fz);
                    }
                    ClickAtCalib(gh->hWnd, (int)fx, (int)fz, idx);
                };

                if (mode == 0) {
                    if (s_reviveRetryCount >= 4) {
                        TryReviveClick("resurrection.png", CalibIndex::REVIVE_基本, Coords::基本復活.x, Coords::基本復活.z);
                    } else {
                        TryReviveClick("Soul_Returning_Pearl.png", CalibIndex::REVIVE_SOUL_PEARL, Coords::歸魂珠復活.x, Coords::歸魂珠復活.z);
                    }
                } else if (mode == 1) {
                    TryReviveClick("resurrection.png", CalibIndex::REVIVE原地, Coords::復活按鈕.x, Coords::復活按鈕.z);
                } else {
                    TryReviveClick("resurrection.png", CalibIndex::REVIVE_基本, Coords::基本復活.x, Coords::基本復活.z);
                }
                s_reviveClicked = true;
                s_reviveRetryCount++;
                Sleep(1000);
                return;
            }
            PlayerState st2;
            if (ReadPlayerState(gh, &st2) && st2.hp > 0) {
                s_enteredDeadState = false;
                // 復活成功後根據設定進入對應的 TOWN_SUPPLY
                const char* townNames[] = { "聖門", "商洞", "玄巖", "鳳凰" };
                int townIdx = g_cfg.town_index.load();
                Logf("狀態機", "[DEAD -> TOWN_SUPPLY] 復活成功！HP=%d town=%d/%s",
                     st2.hp, townIdx, (townIdx >= 0 && townIdx <= 3) ? townNames[townIdx] : "?");
                TransitionState(BotState::TOWN_SUPPLY, "ReviveSuccess", &st2);
                s_deathStartTime = 0;
                s_deadRecoverySeenTime = 0;
                s_reviveRetryCount = 0;
                s_loggedDead = false;
                return;
            }
            s_reviveRetryCount++;
            if (s_reviveRetryCount >= 20) {
                Log("狀態機", "❌ [DEAD] 復活失敗 20 次，強制回 IDLE");
                TransitionState(BotState::IDLE, "ReviveRetryExceeded", &st);
                g_cfg.active.store(false);
                s_reviveRetryCount = 0;
                s_reviveClicked = false;
                return;
            }
            s_reviveClicked = false;
            SleepJitter(500);
            return;
        }
        if (!g_cfg.auto_revive.load()) {
            static DWORD lastLog = 0;
            if (now - lastLog > 5000) {
                Log("狀態機", "[DEAD] auto_revive 關閉，等待手動復活");
                lastLog = now;
            }
        }
        SleepJitter(300);
        return;
    }
    if (!g_cfg.active.load() || currentState == BotState::IDLE) {
        static DWORD lastIdleLog = 0;
        if (now - lastIdleLog > 5000) {
            Logf("狀態機", "[IDLE] active=%d, state=%d", g_cfg.active.load(), currentState);
            lastIdleLog = now;
        }
        Sleep(100);
        return;
    }
    // ── HUNTING ──
    if (currentState == BotState::HUNTING) {
        if (!s_enteredHunting) {
            Log("狀態機", "========================================");
            Logf("狀態機", "[HUNTING] 進入戰鬥狀態！");
            Logf("狀態機", "  └- 座標: x=%.1f, z=%.1f, Map=%d", st.x, st.z, st.mapId);
            Logf("狀態機", "  └- HP: %d/%d (%d%%), MP: %d/%d (%d%%)", st.hp, st.maxHp, hpPct, st.mp, st.maxMp, mpPct);
            Log("狀態機", "========================================");
            s_combatIntent = CombatIntent::SEEKING;
            s_currentTargetId = 0;
            s_curBar = -1;
            SwitchBar(gh->hWnd, g_cfg.attackBarKey.load());
            s_enteredHunting = true;
        }
        if (st.hp <= 0) {
            Log("狀態機", "========================================");
            Logf("狀態機", "[HUNTING -> DEAD] 角色死亡！HP=0");
            Log("狀態機", "========================================");
            TransitionState(BotState::DEAD, "HpReachedZero", &st);
            return;
        }
        // ── 敵人接近偵測 ──
        if (CheckApproachingThreats(gh, st, now)) {
            Log("狀態機", "[HUNTING] 偵測到敵人高速接近，準備回城...");
            SendKeyInputFocused(g_cfg.key_waypoint_start, gh->hWnd);
            s_returnStartTime = now;
            s_returnCardSent = true;
            TransitionState(BotState::RETURNING, "EnemyApproach", &st);
            Sleep(100);
            return;
        }
        // ── 先補血（嘗試用 Q 救）──
        if (now - s_lastDrinkCheck >= 800) {
            s_lastDrinkCheck = now;
            DoPotionTick(gh->hWnd, st);
        }

        // ── 寵物餵食 ──
        DoPetFeedTick(gh->hWnd);

        // ── 補完後再次檢查 HP ──
        PlayerState st2;
        if (ReadPlayerState(gh, &st2)) {
            int hpAfterPotion = st2.maxHp > 0 ? (st2.hp * 100 / st2.maxHp) : 100;

            // 如果補完還是低血 → 回城
            if (autoSupplyEnabled && hpAfterPotion < g_cfg.hp_return_pct.load()) {
                Logf("狀態機", "[HUNTING] HP還是低(%d%% < %d%%)，準備回城...",
                    hpAfterPotion, g_cfg.hp_return_pct.load());
                SendKeyInputFocused(g_cfg.key_waypoint_start, gh->hWnd);
                s_returnStartTime = now;
                s_returnCardSent = true;
                TransitionState(BotState::RETURNING, "LowHpAfterPotion", &st2);
                Sleep(100);
                return;
            }
        }

        std::vector<InvSlot> inventorySlots;
        int inventoryCount = 0;
        InventoryScanStatus inventoryStatus = InventoryScanStatus::INVALID_HANDLE;
        bool needInventoryScan = autoSupplyEnabled &&
            (g_cfg.inventory_return.load() || g_cfg.potion_check_enable.load());
        if (needInventoryScan) {
            inventoryStatus = ScanInventoryDetailed(gh, &inventorySlots, &inventoryCount);
        }

        if (autoSupplyEnabled && g_cfg.inventory_return.load()) {
            if (IsInventoryScanUsable(inventoryStatus)) {
                int pct = (inventoryCount * 100) / OffsetConfig::InvMaxSlots();
                if (pct >= g_cfg.inventory_full_pct.load()) {
                    Logf("狀態機", "[HUNTING] 背包滿了 (%d%%)，準備回城...", pct);
                    SendKeyInputFocused(g_cfg.key_waypoint_start, gh->hWnd);
                    s_returnStartTime = now;
                    s_returnCardSent = true;
                    TransitionState(BotState::RETURNING, "InventoryFull", &st);
                    Sleep(100);
                    return;
                }
            } else {
                static DWORD s_lastInvUnknownLog = 0;
                if (now - s_lastInvUnknownLog > 5000) {
                    Logf("狀態機", "[HUNTING] 背包狀態未知，跳過回城判定 (%s)",
                        GetInventoryScanStatusName(inventoryStatus));
                    s_lastInvUnknownLog = now;
                }
            }
        }

        if (autoSupplyEnabled && g_cfg.potion_check_enable.load()) {
            if (IsInventoryScanUsable(inventoryStatus)) {
                int potionStart = g_cfg.potion_slot_start.load();
                int potionEnd = g_cfg.potion_slot_end.load();
                int minPotionSlots = g_cfg.min_potion_slots.load();
                if (minPotionSlots < 1) minPotionSlots = 1;
                int occupiedCount = CountPotionSlotsInRange(inventorySlots, potionStart, potionEnd);
                if (occupiedCount < minPotionSlots) {
                    Logf("狀態機", "[HUNTING] 藥水格不足 (%d < %d)，準備回城...",
                        occupiedCount, minPotionSlots);
                    SendKeyInputFocused(g_cfg.key_waypoint_start, gh->hWnd);
                    s_returnStartTime = now;
                    s_returnCardSent = true;
                    TransitionState(BotState::RETURNING, "PotionSlotsLow", &st);
                    Sleep(100);
                    return;
                } else {
                    static DWORD s_lastPotionOkLog = 0;
                    if (now - s_lastPotionOkLog > 15000) {
                        Logf("狀態機", "[HUNTING] 藥水格正常 (%d/%d)，繼續戰鬥",
                            occupiedCount, minPotionSlots);
                        s_lastPotionOkLog = now;
                    }
                }
            } else {
                static DWORD s_lastPotionUnknownLog = 0;
                if (now - s_lastPotionUnknownLog > 5000) {
                    Logf("狀態機", "[HUNTING] 藥水狀態未知，跳過回城判定 (%s)",
                        GetInventoryScanStatusName(inventoryStatus));
                    s_lastPotionUnknownLog = now;
                }
            }
        }

        // 遊戲時間自動回城
        if (g_cfg.auto_game_time.load() && CheckGameTimeReturn(gh)) {
            Log("狀態機", "[HUNTING] 遊戲時間到，回城");
            SendKeyInputFocused(g_cfg.key_waypoint_start, gh->hWnd);
            s_returnStartTime = now;
            s_returnCardSent = true;
            TransitionState(BotState::RETURNING, "GameTimeReturn", &st);
            Sleep(100);
            return;
        }

        // 遊戲時間自動返回野外
        if (g_cfg.auto_game_time.load() && g_cfg.auto_return_to_field.load() &&
            CheckGameTimeBackToField(gh)) {
            Log("狀態機", "[HUNTING] 遊戲時間到，返回野外");
            SendKeyInputFocused(g_cfg.key_waypoint_end, gh->hWnd);
            s_returnStartTime = now;
            s_returnCardSent = true;
            TransitionState(BotState::BACK_TO_FIELD, "GameTimeBackToField", &st);
            Sleep(100);
            return;
        }

        // 撿物品
        static DWORD lastPickupLog = 0;
        if (g_cfg.auto_pickup.load()) {
            DWORD pickupDelay = (DWORD)g_cfg.pickup_interval_ms.load();
            if (pickupDelay < 200) pickupDelay = 200;
            if (now - s_lastPickupTime > pickupDelay) {
                s_lastPickupTime = now;
                BYTE key = g_cfg.key_pickup;
                SendKeyInputFocused(key, gh->hWnd);
            }
            if (now - lastPickupLog > 10000) {
                Logf("撿物", "auto_pickup=ON, interval=%dms", pickupDelay);
                lastPickupLog = now;
            }
        }
        // 輔助技能
        DoSupportSkill(gh->hWnd);
        // 自動鎖怪
        TryAutoTarget(gh->hWnd, gh);
        // 攻擊施放
        DoCombatTick(gh->hWnd, gh);
        s_wasInHunting = true;
    } else {
        if (s_wasInHunting) {
            Logf("狀態機", "[HUNTING] 離開戰鬥狀態, 前往: %s", GetStateName(currentState));
            s_wasInHunting = false;
        }
    }
    // 狀態日誌
    if (now - s_lastStatusLog > 5000) {
        const char* stateName = GetStateName(currentState);
        Logf("狀態", "Lv=%d HP=%d/%d(%d%%) MP=%d/%d(%d%%) Map=%d [%s]",
            st.level, st.hp, st.maxHp, hpPct, st.mp, st.maxMp, mpPct, st.mapId, stateName);
        int skillCount = g_cfg.attackSkillCount.load();
        if (skillCount > 0) {
            int idx = g_cfg.currentSkillIndex.load() % skillCount;
            BYTE numKey = (idx < 9) ? (BYTE)('1' + idx) : (BYTE)'0';
            Logf("狀態", "攻擊技能: %d/%d (按鍵:'%c')  喝水: HP<%d%% MP<%d%% SP<%d%%",
                idx + 1, skillCount, (char)numKey,
                g_cfg.hp_potion_pct.load(),
                g_cfg.mp_potion_pct.load(),
                g_cfg.sp_potion_pct.load());
        }
        s_lastStatusLog = now;
    }
    Sleep(50);
}
// ============================================================
// SupplyTick 子函數（SupplyForceReset 已於 line 68 前向宣告）
// ============================================================
static void ForceResetToBackToField(GameHandle* gh);
static void SupplyCheckGlobalTimeout(GameHandle* gh);
static void SupplyOnEnterPhase(int phase);
static void SupplyOnExitPhase(int phase);
static void SupplyForceReset(void) {
    s_supplyPhase = 0;
    s_supplyPhaseStart = 0;
    s_supplyRetryCount = 0;
    s_invBase = 0;
    s_phase0SubStep = 0;
    s_phase0RClickTime = 0;
    s_buySubPhase = 0;
    s_buyPhaseStart = 0;
    s_supplyEntered = false;
    s_globalTimeout = 0;
    s_lastSupplyPhase = -1;
    s_sellSessionActive = false;
    s_lastSellAction = 0;
    s_backToFieldCardSent = 0;
    Log("狀態機", "[SupplyForceReset] 所有靜態變數已重置");
}
// DWORD 繞環安全比較：now 是否已超過 deadline（處理 GetTickCount 49.7天繞環）
static inline bool IsTickExpired(DWORD now, DWORD deadline) {
    return (now - deadline) < 0x80000000;
}
static void SupplyCheckGlobalTimeout(GameHandle* gh) {
    (void)gh;
    DWORD now = GetTickCount();
    if (s_globalTimeout > 0 && IsTickExpired(now, s_globalTimeout)) {
        Log("狀態機", "★★★ [TOWN_SUPPLY] 全域超時 45 秒！強制返回野外 ★★★");
        SupplyForceReset();
        TransitionState(BotState::BACK_TO_FIELD, "SupplyGlobalTimeout45s", NULL);
    }
}
static void SupplyOnEnterPhase(int phase) {
    switch (phase) {
        case 0: Log("狀態機", "[Phase 0] 走向 NPC 並開啟對話"); break;
        case 1: Log("狀態機", "[Phase 1] 發送 SPACE 開商店"); break;
        case 2: Log("狀態機", "[Phase 2] 自動賣物"); break;
        case 3: Log("狀態機", "[Phase 3] 自動買藥"); s_buySubPhase = 0; break;
        case 4: Log("狀態機", "[Phase 4] 關閉商店 → 返回野外"); break;
    }
}
static void SupplyOnExitPhase(int phase) {
    (void)phase;
}
static void ForceResetToBackToField(GameHandle* gh) {
    if (g_State.load() == (int)BotState::BACK_TO_FIELD && s_backToFieldCardSent) {
        s_backToFieldStartTime = GetTickCount();
        return;
    }
    TransitionState(BotState::BACK_TO_FIELD, "ForceResetToBackToField", NULL);
    s_backToFieldCardSent = 0;
    Log("狀態機", "→ 已強制切換到 BACK_TO_FIELD");
    SendKeyInputFocused(g_cfg.key_waypoint_end, gh->hWnd);
    s_backToFieldCardSent = GetTickCount();
}
// ============================================================
// SupplyTick - 聖門專用診斷版（90秒超時）
// 特點：
//   1. 聖門座標 Hardcoded，確保精確
//   2. 90秒全域超時（比舊版 45秒更寬鬆）
//   3. 詳細 Logf 逐階段追蹤
//   4. 前景點擊 + FocusGameWindow 保證輸入抵達
// ============================================================
void SupplyTick(GameHandle* gh) {
    if (!gh || !gh->attached || !gh->hWnd) {
        static DWORD s_lastNoGhLog = 0;
        DWORD now = GetTickCount();
        if (now - s_lastNoGhLog > 3000) {
            Log("狀態機", "⚠️ [SupplyTick] gh 未就緒，跳過");
            s_lastNoGhLog = now;
        }
        return;
    }

    if (s_reentryGuard) {
        static DWORD s_lastReentryLog = 0;
        DWORD now = GetTickCount();
        if (now - s_lastReentryLog > 3000) {
            Log("狀態機", "⚠️ [SupplyTick] 偵測到重入，略過本次 tick");
            s_lastReentryLog = now;
        }
        return;
    }

    ReentryGuard guard(s_reentryGuard);  // RAII 自動防重入

    HWND hWnd = gh->hWnd;
    DWORD now = GetTickCount();

    // ── 前景焦點包裝巨集──────────────────────────────
#define FOCUS_CLICK(x, y) do { \
        FocusGameWindow(hWnd); \
        ClickAtDirect(hWnd, x, y); \
    } while(0)
#define FOCUS_RCLICK(x, y) do { \
        FocusGameWindow(hWnd); \
        RClickAtDirect(hWnd, x, y); \
    } while(0)

    // ── BACK_TO_FIELD 分支─────────────────────────────
    if (g_State.load() == (int)BotState::BACK_TO_FIELD) {
        if (!s_backToFieldCardSent) {
            FocusGameWindow(hWnd);
            SendKeyInputFocused(g_cfg.key_waypoint_end, hWnd);
            s_backToFieldCardSent = now;
            s_backToFieldStartTime = now;
            Log("狀態機", "[BACK_TO_FIELD] 發送前點卡");
        }
        DWORD minDelay = (DWORD)g_cfg.teleport_delay_ms.load();
        if (minDelay < 8000) minDelay = 8000;
        DWORD elapsed = now - s_backToFieldStartTime;

        // Bug 1 修復：15秒超時保護，防止 NPC 對話或傳送延遲卡死
        DWORD hardTimeout = 15000;
        if (elapsed > hardTimeout) {
            Logf("狀態機", "[BACK_TO_FIELD] 超時（%dms > %dms），強制回到 HUNTING", elapsed, hardTimeout);
            SupplyForceReset();
            TransitionState(BotState::HUNTING, "BackToFieldTimeout", NULL);
            return;
        }

        if (elapsed >= minDelay) {
            PlayerState fieldState;
            char confirmReason[160] = {0};
            if (ReadPlayerStateDetailed(gh, &fieldState, confirmReason, sizeof(confirmReason)) == PlayerStateReadStatus::OK &&
                ConfirmBackToFieldArrival(fieldState, confirmReason, sizeof(confirmReason))) {
                Logf("狀態機", "[BACK_TO_FIELD → HUNTING] 傳送確認成功（%dms）: %s",
                    elapsed, confirmReason);
                SupplyForceReset();
                TransitionState(BotState::HUNTING, confirmReason, &fieldState);
                return;
            }

            DWORD confirmTimeout = GetTransitionConfirmTimeoutMs(minDelay, 16000);
            if (elapsed >= confirmTimeout) {
                Logf("狀態機", "❌ [BACK_TO_FIELD] 傳送確認超時（%dms）: %s",
                    elapsed, confirmReason[0] ? confirmReason : "讀值仍未有效");
                SupplyForceReset();
                // Bug 1 修復：超時回 HUNTING 而非 IDLE，避免用戶需要手動重啟
                TransitionState(BotState::HUNTING, "BackToFieldConfirmTimeout", NULL);
                return;
            }
        }
        return;
    }

    // ── 全域超時檢查（90秒）──────────────────────────
    if (s_globalTimeout > 0 && IsTickExpired(now, s_globalTimeout)) {
        Log("狀態機", "★★★ [TOWN_SUPPLY] 全域超時 90 秒！強制返回野外 ★★★");
        SupplyForceReset();
        TransitionState(BotState::BACK_TO_FIELD, "SupplyGlobalTimeout90s", NULL);
        s_backToFieldCardSent = 0;
        return;
    }

    if (!s_supplyEntered) {
        PlayerState supplyState;
        char supplyReason[160] = {0};
        PlayerStateReadStatus supplyRead = ReadPlayerStateDetailed(gh, &supplyState, supplyReason, sizeof(supplyReason));
        if (supplyRead != PlayerStateReadStatus::OK) {
            Logf("狀態機", "❌ [TOWN_SUPPLY] 進入前讀值無效: %s", supplyReason);
            TransitionState(BotState::IDLE, "SupplyEntryReadInvalid", NULL);
            g_cfg.active.store(false);
            return;
        }
        if (!IsTownMap(supplyState.mapId)) {
            char confirmReason[160] = {0};
            if (!ConfirmReturnArrival(supplyState, confirmReason, sizeof(confirmReason))) {
                Logf("狀態機", "❌ [TOWN_SUPPLY] 尚未確認到城，拒絕進入補給: %s", confirmReason);
                TransitionState(BotState::IDLE, "SupplyEntryNotConfirmed", &supplyState);
                g_cfg.active.store(false);
                return;
            }
        }
        Log("狀態機", "=== [TOWN_SUPPLY] 進入聖門補給狀態，全域重置 ===");
        SupplyForceReset();
        s_supplyEntered = true;
        s_globalTimeout = now + 90000;  // 90秒超時
        s_lastSupplyPhase = -1;
        s_phase0RClickTime = 0;
    }

    DWORD phaseElapsed = (s_supplyPhaseStart == 0) ? 0 : (now - s_supplyPhaseStart);

    // ═══════════════════════════════════════════════════════════
    // Phase 0: 走向 NPC (577,140) → 右鍵對話 → 購買物品
    // ═══════════════════════════════════════════════════════════
    if (s_supplyPhase == 0) {
        if (s_lastSupplyPhase != 0) {
            SupplyOnEnterPhase(0);
            s_lastSupplyPhase = 0;
            Logf("狀態機", "[Phase 0] 開始 - 點擊 NPC (577,140)");
        }

        // Step 0: 左鍵點擊 NPC
        if (s_phase0SubStep == 0) {
            FOCUS_CLICK(577, 140);  // NPC coordinates
            Logf("狀態機", "[Phase 0] Step0: 左鍵點擊 NPC");
            s_phase0SubStep = 1;
            s_phase0RClickTime = now;
            Sleep(1200);
            return;
        }

        // Step 1: 右鍵點擊 NPC（等待 4.5 秒後）
        if (s_phase0SubStep == 1) {
            if (now - s_phase0RClickTime > 4500) {
                FOCUS_RCLICK(577, 140);  // NPC coordinates
                Logf("狀態機", "[Phase 0] Step1: 右鍵點擊 NPC @ %dms", now - s_phase0RClickTime);
                s_phase0SubStep = 2;
                s_phase0RClickTime = now;
                Sleep(800);
            }
            return;
        }

        // Step 2: 點擊「購買物品」
        if (s_phase0SubStep == 2) {
            if (now - s_phase0RClickTime > 3500) {
                FOCUS_CLICK(440, 390);  // Buy button coordinates
                Logf("狀態機", "[Phase 0] Step2: 點擊購買物品 @ %dms", now - s_phase0RClickTime);
                s_phase0SubStep = 3;
                s_phase0RClickTime = now;
                Sleep(1000);
            }
            return;
        }

        // Step 3: 等待商店開啟
        if (s_phase0SubStep == 3) {
            if (now - s_phase0RClickTime > 6000) {
                Logf("狀態機", "[Phase 0 → Phase 1] 商店應已開啟 @ %dms", now - s_phase0RClickTime);
                SupplyOnExitPhase(0);
                s_supplyPhase = 1;
                s_supplyPhaseStart = now;
                s_phase0SubStep = 0;
            }
            return;
        }

        return;
    }

    // ═══════════════════════════════════════════════════════════
    // Phase 1: 發送 SPACE 確認商店開啟
    // ═══════════════════════════════════════════════════════════
    if (s_supplyPhase == 1) {
        if (s_lastSupplyPhase != 1) {
            SupplyOnEnterPhase(1);
            s_lastSupplyPhase = 1;
            Log("狀態機", "[Phase 1] 開始 - 發送 SPACE");
        }

        FocusGameWindow(hWnd);
        SendKeyInputFocused(VK_SPACE, hWnd);
        Logf("狀態機", "[Phase 1] 已發送 SPACE (已耗時 %dms)", phaseElapsed);

        if (phaseElapsed > 6500) {
            Log("狀態機", "[Phase 1 → Phase 2] 商店已就緒");
            SupplyOnExitPhase(1);
            s_supplyPhase = 2;
            s_supplyPhaseStart = now;
        }
        return;
    }

    // ═══════════════════════════════════════════════════════════
    // Phase 2: 自動賣物
    // ═══════════════════════════════════════════════════════════
    if (s_supplyPhase == 2) {
        if (s_lastSupplyPhase != 2) {
            SupplyOnEnterPhase(2);
            s_lastSupplyPhase = 2;
            Log("狀態機", "[Phase 2] 開始 - 賣物");
        }

        if (!s_sellSessionActive) {
            // 掃描背包，過濾保護物品
            std::vector<InvSlot> slots;
            if (ScanInventory(gh, &slots) > 0) {
                BotConfig* cfg = GetBotConfig();
                EnterCriticalSection(&cfg->cs_protected);
                for (auto it = slots.begin(); it != slots.end(); ) {
                    bool isProtected = false;
                    if (it->itemId == 0 || it->itemId == 0xFFFFFFFF) {
                        isProtected = true;
                    } else {
                        for (size_t i = 0; i < cfg->protected_item_ids.size(); i++) {
                            DWORD protId = (DWORD)cfg->protected_item_ids[i];
                            if (protId != 0 && it->itemId == protId) { isProtected = true; break; }
                        }
                        if (!isProtected) {
                            int row = it->slotIdx / 6;
                            if (row >= 0 && row < BotConfig::MAX_INVENTORY_ROWS) {
                                if (cfg->protected_rows[row]) isProtected = true;
                            }
                        }
                    }
                    if (isProtected) it = slots.erase(it);
                    else ++it;
                }
                LeaveCriticalSection(&cfg->cs_protected);

                if (!slots.empty()) {
                    Logf("賣物", "[Phase 2] 找到 %d 個可賣物品，點擊賣物資", (int)slots.size());
                    ClickAtDirect(gh->hWnd, Coords::GetNPCSellItemPos().x, Coords::GetNPCSellItemPos().z);
                    Sleep(800);
                    s_sellSessionActive = true;
                    s_lastSellAction = now;
                } else {
                    Log("狀態機", "[Phase 2] 沒有可賣物品 → Phase 3");
                    s_sellSessionActive = false;
                    SupplyOnExitPhase(2);
                    s_supplyPhase = 3;
                    s_supplyPhaseStart = now;
                    return;
                }
            } else {
                // ScanInventory 返回 0（背包讀取失敗或背包空的）→ 直接進 Phase 3
                Log("賣物", "[Phase 2] 背包掃描返回 0 → 跳過賣物，直接 Phase 3");
                s_supplyPhase = 3;
                s_supplyPhaseStart = now;
                return;
            }
        } else {
            DWORD sellElapsed = now - s_lastSellAction;
            if (sellElapsed > 5000) {
                Log("狀態機", "[Phase 2] 賣物超時 → Phase 3");
                s_sellSessionActive = false;
                SupplyOnExitPhase(2);
                s_supplyPhase = 3;
                s_supplyPhaseStart = now;
                return;
            }
            if (sellElapsed > 1200) {
                Logf("賣物", "[Phase 2] 點擊確認");
                ClickAtDirect(gh->hWnd, Coords::GetNPCSellConfirmPos().x, Coords::GetNPCSellConfirmPos().z);
                s_lastSellAction = now;
            }
        }

        if (phaseElapsed > 25000) {
            Log("狀態機", "[Phase 2] 超時 25s → Phase 3");
            s_sellSessionActive = false;
            SupplyOnExitPhase(2);
            s_supplyPhase = 3;
            s_supplyPhaseStart = now;
        }
        return;
    }

    // ═══════════════════════════════════════════════════════════
    // Phase 3: 買藥水（消耗品分頁 → HP/MP/SP）
    // ═══════════════════════════════════════════════════════════
    if (s_supplyPhase == 3) {
        if (s_lastSupplyPhase != 3) {
            s_buyPhaseStart = now;
            SupplyOnEnterPhase(3);
            s_lastSupplyPhase = 3;
            Log("狀態機", "[Phase 3] 開始 - 買藥");
        }

        BotConfig* cfg = GetBotConfig();
        if (!cfg->auto_buy.load()) {
            Log("狀態機", "[Phase 3] auto_buy=OFF → Phase 4");
            SupplyOnExitPhase(3);
            s_supplyPhase = 4;
            s_supplyPhaseStart = now;
            return;
        }

        DWORD buyElapsed = now - s_buyPhaseStart;
        if (buyElapsed > 25000) {
            Logf("狀態機", "[Phase 3] 買藥超時 25s → Phase 4");
            SupplyOnExitPhase(3);
            s_supplyPhase = 4;
            s_supplyPhaseStart = now;
            return;
        }

        int hpQty = cfg->buy_hp_qty.load();
        int mpQty = cfg->buy_mp_qty.load();
        int spQty = cfg->buy_sp_qty.load();
        int remaining = (hpQty > 0 ? 1 : 0) + (mpQty > 0 ? 1 : 0) + (spQty > 0 ? 1 : 0);

        if (remaining == 0) {
            Log("狀態機", "[Phase 3] 購買數量為 0 → Phase 4");
            SupplyOnExitPhase(3);
            s_supplyPhase = 4;
            s_supplyPhaseStart = now;
            return;
        }

        // SubPhase 0: 點擊「消耗品」分頁 (699, 253)
        if (s_buySubPhase == 0) {
            Logf("狀態機", "[Phase 3] SubPhase0: 點擊消耗品分頁");
            FOCUS_CLICK(699, 253);  // Consume tab coordinates
            Sleep(1000);
            s_buySubPhase = 1;
            return;
        }

        // SubPhase 1-3: 依序買 HP / MP / SP
        int buyIdx = s_buySubPhase - 1;  // 0=HP, 1=MP, 2=SP
        if (buyIdx >= 0 && buyIdx < 3) {
            int* qtyArr[3] = { &hpQty, &mpQty, &spQty };
            const char* itemNames[3] = { "HP", "MP", "SP" };
            const int itemXs[3] = { 320, 360, 400 };  // HP, MP, SP X coordinates
            const int itemYs[3] = { 280, 280, 280 };  // HP, MP, SP Y coordinates

            int qty = *qtyArr[buyIdx];
            if (qty > 0) {
                Logf("狀態機", "[Phase 3] SubPhase%d: 購買 %s x%d",
                    s_buySubPhase, itemNames[buyIdx], qty);

                // 1) 點擊商品
                FOCUS_CLICK(itemXs[buyIdx], itemYs[buyIdx]);
                Sleep(600);

                // 2) 點擊數量框
                FOCUS_CLICK(520, 420);  // Quantity input box coordinates
                Sleep(300);

                // 3) Ctrl+A 全選 → 輸入數量
                SendCtrlA(hWnd);
                Sleep(100);
                TypeNumber(hWnd, qty);
                Sleep(400);

                // 4) 點擊確認
                FOCUS_CLICK(500, 450);  // Buy confirm button coordinates
                Sleep(1500);

                Logf("狀態機", "[Phase 3] %s 購買完成", itemNames[buyIdx]);
            }

            s_buySubPhase++;
            if (s_buySubPhase > 3) {
                Log("狀態機", "[Phase 3] 全部購買完成 → Phase 4");
                SupplyOnExitPhase(3);
                s_supplyPhase = 4;
                s_supplyPhaseStart = now;
            }
        }
        return;
    }

    // ═══════════════════════════════════════════════════════════
    // Phase 4: 關閉商店 → 返回野外（前點卡）
    // ═══════════════════════════════════════════════════════════
    if (s_supplyPhase == 4) {
        if (s_lastSupplyPhase != 4) {
            SupplyOnEnterPhase(4);
            s_lastSupplyPhase = 4;
            Log("狀態機", "[Phase 4] 開始 - 關閉商店");
        }

        Logf("狀態機", "[Phase 4] 發送 ESC × 2 → 前點卡");
        SendKeyInputFocused(VK_ESCAPE, hWnd);
        SleepJitter(600);
        SendKeyInputFocused(VK_ESCAPE, hWnd);
        SleepJitter(600);

        // 重置並切回 BACK_TO_FIELD
        SupplyForceReset();
        TransitionState(BotState::BACK_TO_FIELD, "SupplyPhase4Complete", NULL);
        s_backToFieldCardSent = 0;
    }
}
