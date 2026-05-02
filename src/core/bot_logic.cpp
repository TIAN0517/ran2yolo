#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include <atomic>
#include <windows.h>

#include "bot_logic.h"
#include "game_process.h"
#include "input_sender.h"
#include "memory_reader.h"
#include "attack_packet.h"
#include "coord_calib.h"
#include "coords.h"
#include "dm_plugin.h"
#include "nethook_shmem.h"
#include "offline_license.h"
#include "offset_config.h"
#include "target_lock.h"
#include "visionentity.h"
#include "../common/utils.h"  // for LogStealth (stealth logging)
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
#endif

// ============================================================
// RAII 防重入保護（防止異常導致卡死）
// ============================================================
class ReentryGuard {
  bool &m_flag;

public:
  ReentryGuard(bool &flag) : m_flag(flag) { m_flag = true; }
  ~ReentryGuard() { m_flag = false; }

private:
  ReentryGuard(const ReentryGuard &) = delete;
  ReentryGuard &operator=(const ReentryGuard &) = delete;
};

// ============================================================
// 隨機延遲包裝（防外掛時序偵測）
// ============================================================
static void SleepJitter(int ms) {
  if (ms <= 0) {
    ms = 1;
  } // ✅ 杜絕 Sleep(0) 等於沒延遲
  int jitter = ms + (int)((rand() % 41 - 20) * ms / 100.0f);
  if (jitter < 1)
    jitter = 1;
  Sleep(jitter);
}

// ============================================================
// 純鍵盤戰鬥模式 - 完全不需要記憶體讀取
// 2026-05-02
// ============================================================

// 前向宣告
static void SwitchBar(HWND hWnd, BYTE barKey);

// 純戰鬥模式狀態
static bool s_pureCombatInit = false;        // 是否已初始化
static int s_pureSkillIndex = 0;            // 當前技能索引 (0-4)
static int s_purePointIndex = 0;            // 當前圓形點索引 (0-7)
static DWORD s_pureLastSkillTime = 0;       // 上次技能時間
static DWORD s_pureLastPointTime = 0;       // 上次圓形點時間
static DWORD s_pureLastPickupTime = 0;      // 上次撿物時間

// 初始化純戰鬥模式
static void PureCombatInit(HWND hWnd) {
    s_pureCombatInit = true;
    s_pureSkillIndex = 0;
    s_purePointIndex = 0;
    s_pureLastSkillTime = 0;
    s_pureLastPointTime = 0;
    s_pureLastPickupTime = 0;

    // 切換到 F1 技能欄（SwitchBar 在本文件 line 2600 定義）
    SwitchBar(hWnd, VK_F1);  // VK_F1 是 Windows 虛擬鍵碼 (0x70)

    Log("狀態機", "========================================");
    Log("狀態機", "[HUNTING] 進入純鍵盤戰鬥模式 (StationaryCircleMode)");
    Log("狀態機", "  └- 攻擊技能: 1~5 輪替");
    Log("狀態機", "  └- 圓形掃描: 8 點定點");
    Log("狀態機", "  └- 撿物: 空白鍵");
    Log("狀態機", "========================================");
}

// 純鍵盤戰鬥主循環
static void PureCombatTick(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) {
        return;
    }

    // 緊急停止檢查
    if (!g_Running) {
        s_pureCombatInit = false;
        return;
    }

    // 初始化
    if (!s_pureCombatInit) {
        PureCombatInit(hWnd);
    }

    DWORD now = GetTickCount();

    // 間隔設定
    DWORD skillInterval = 800;   // 技能間隔 800ms
    DWORD circleInterval = 100;  // 圓形點間隔 100ms
    DWORD pickupInterval = 1000; // 撿物間隔 1000ms

    // === 1. 攻擊技能輪替 ===
    if (now - s_pureLastSkillTime >= skillInterval) {
        s_pureLastSkillTime = now;

        // 技能索引 (1-5)
        BYTE skillKey = (BYTE)('1' + s_pureSkillIndex);
        Logf("戰鬥", "★★★ [技能%c] 點%d/%d ★★★", (char)skillKey, s_pureSkillIndex + 1, 5);

        extern void SendKeyInputFocused(BYTE vk, HWND hWnd);
        SendKeyInputFocused(skillKey, hWnd);

        // 推進索引
        s_pureSkillIndex = (s_pureSkillIndex + 1) % 5;
    }

    // === 2. 圓形掃描定點 ===
    if (now - s_pureLastPointTime >= circleInterval) {
        s_pureLastPointTime = now;

        extern void ClickAttackPoint(HWND hWnd, int pointIndex);
        extern int Coords_ATTACK_SCAN_COUNT;

        int pointIndex = s_purePointIndex % 8; // Coords::ATTACK_SCAN_COUNT
        ClickAttackPoint(hWnd, pointIndex);

        Logf("戰鬥", "[圓形] 點%d/8", pointIndex + 1);

        s_purePointIndex = (s_purePointIndex + 1) % 8;
    }

    // === 3. 自動撿物 ===
    if (g_cfg.auto_pickup.load() && now - s_pureLastPickupTime >= pickupInterval) {
        s_pureLastPickupTime = now;
        SendKeyInputFocused(VK_SPACE, hWnd);
        Log("戰鬥", "[撿物] 空白鍵");
    }

    Sleep(50);
}

// 前向宣告（Log/Logf 在後面定義）
void Log(const char *tag, const char *msg);
void Logf(const char *tag, const char *fmt, ...);
static void SupplyForceReset(void);
// ============================================================
// Global state
// ============================================================
volatile bool g_Running = true;
BotConfig g_cfg;
static std::atomic<int> g_State{(int)BotState::IDLE};
static PlayerState s_uiPlayerCache;
static CRITICAL_SECTION s_uiCacheCs;
static CRITICAL_SECTION s_stateTransitionCs; // BUG-C002: 狀態轉換需要鎖保護

// ✅ 離線卡密驗證狀態
std::atomic<bool> g_licenseValid{false};
bool IsLicenseValid() {
    bool valid = g_licenseValid.load();
    static DWORD s_lastDebugLog = 0;
    if (GetTickCount() - s_lastDebugLog > 5000) {
        Logf("認證", "[DEBUG] IsLicenseValid() = %d", valid ? 1 : 0);
        s_lastDebugLog = GetTickCount();
    }
    return valid;
}
void SetLicenseValid(bool valid) {
    Logf("認證", "[DEBUG] SetLicenseValid(%d)", valid ? 1 : 0);
    g_licenseValid.store(valid);
}
static volatile LONG s_uiCacheCsInit = 0;
static volatile LONG s_invCacheCsInited = 0;
static DWORD s_currentTargetId = 0;
static DWORD s_lastPickupTime = 0;
static DWORD s_lastDrinkCheck = 0;
static char s_curBar =
    (char)0xFF; // 目前技能列（0xFF=未初始化，確保第一次 SwitchBar 一定發送）
static DWORD s_lastStatusLog = 0;
static DWORD s_returnStartTime = 0;
static bool s_returnCardSent = false;
static DWORD s_backToFieldStartTime = 0;
static DWORD s_deathStartTime = 0;       // 死亡時間
static DWORD s_deadRecoverySeenTime = 0; // 偵測到 HP 恢復的穩定起點
static DWORD s_partialDeadSeenTime = 0;
// 復活狀態變量
static std::atomic<bool> s_reviveClicked{false};
static int s_reviveRetryCount = 0;
static bool s_stopInProgress = false; // STOP 防重入
static bool s_enteredHunting = false;
static bool s_enteredRelativeCombat = false;
static bool s_wasInHunting = false;
static bool s_enteredDeadState = false;
static bool s_loggedDead = false;
static bool s_loggedReturn = false;
static BotState s_pausedPreviousState =
    BotState::IDLE; // PAUSED 之前的狀態（用於正確恢復）
static DWORD s_lastReadFailLog = 0;
static DWORD s_lastNoHwndLog = 0;
static DWORD s_consecutiveReadFail = 0;
static DWORD s_lastInvalidStateLog = 0;
static DWORD s_lastInventoryDiagLog = 0;
static DWORD s_lastRelativeCombatLog = 0;
static DWORD s_lastValidPlayerStateTime = 0;
static bool s_hasLastValidPlayerState = false;
static PlayerState s_lastValidPlayerState = {};
static DWORD s_lastValidPlayerStatePid = 0;
static DWORD s_lastValidPlayerStateBase = 0;
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
static DWORD s_antiPkCooldownUntil = 0;
static DWORD s_lastAntiPkCooldownLog = 0;
// ═══════════════ 戰鬥意向狀態機 ═══════════════
enum class CombatIntent {
  SEEKING = 0,  // 尋找目標中
  ENGAGING = 1, // 已在攻擊範圍，施放技能中
  LOOTING = 2,  // 目標死亡，等待撿物品
};
static CombatIntent s_combatIntent = CombatIntent::SEEKING;
static DWORD s_killCount = 0;       // 擊殺計數器
static DWORD s_targetLostTime = 0;  // 目標丟失/死亡時間
static DWORD s_lootDelay = 800;     // 死後撿物品延遲(ms)
static DWORD s_engageStartTime = 0; // 開始攻擊的時間
static DWORD s_targetLockTime = 0;  // 目標鎖定時間
static DWORD s_lastTargetHp = 0;    // 上次記錄的目標 HP
static DWORD s_hpCheckTime = 0;     // 上次 HP 檢查時間
// 失敗目標追蹤：記住打不到的怪，避免重複鎖定
static const int MAX_FAILED_TARGETS = 10;
struct FailedTarget {
  DWORD id;
  DWORD failTime;
};
static FailedTarget s_failedTargets[MAX_FAILED_TARGETS] = {};
static int s_failedCount = 0;
static DWORD s_lastFailedPurge = 0; // 上次清理失敗列表的時間
// 超時設定
static const DWORD ENGAGE_HP_TIMEOUT = 12000;      // 12 秒 HP 沒下降 → 放棄
static const DWORD ENGAGE_HARD_TIMEOUT = 30000;    // 30 秒硬超時 → 強制放棄
static const DWORD FAILED_TARGET_COOLDOWN = 60000; // 60 秒內不再鎖定失敗目標
static bool IsFailedTarget(DWORD targetId);
// 每個技能的獨立冷卻時間（毫秒）
static DWORD s_skillLastTime[BotConfig::MAX_SKILLS] = {0};
// 防止同一 tick 內多次施放
static DWORD s_lastCombatTick = 0;
// 意向切換防抖：避免狀態機在高頻率下當掉（100ms 間隔）
static DWORD s_lastIntentChange = 0;
// 鎖定目標冷卻：防止高頻率點擊
static DWORD s_lastTargetClick = 0;
// ═══════════════ 攻擊圓圈範圍（1024x768 相對座標中心 +
// 真圓半徑）═══════════════
static const int ATTACK_CENTER_X = IsWin7Platform() ? 520 : 500;
static const int ATTACK_CENTER_Y = IsWin7Platform() ? 390 : 370;
static const int ATTACK_TARGET_RADIUS = 260;
static DWORD s_playerServerId = 0;
static DWORD s_playerServerIdTime = 0;
static bool SplitServerId(DWORD serverId, DWORD *outMid, DWORD *outSid) {
  if (outMid)
    *outMid = (serverId >> 8) & 0x3FF;
  if (outSid)
    *outSid = serverId & 0x3F;
  return serverId != 0 && serverId != 0xFFFFFFFF;
}
// ============================================================
// 防呆自動移動功能
// ============================================================
static DWORD s_antiStuckLastMove = 0;
static int s_antiStuckPhase = 0;
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
static int s_sellSubPhase = 0;  // Bug 1 Fix: 賣物分頁追蹤 (0=點分頁, 1=點物品, 2=點確認)
static DWORD s_lastSellAction = 0;
static DWORD s_backToFieldCardSent = 0; // 時間戳，當發送時設置，用於檢測
static bool s_phase4EscSent = false;

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
    {"起點傳送卡", 0},      // 傳送卡（建議放第4排後）
    {"前點傳送卡", 0},      // 傳送卡（建議放第4排後）
    {"聖財團D停車場卡", 0}, // 特殊卡片
    {"好友卡", 0},          // 社交道具
    {"公車卡", 0},          // 交通工具卡
    {"異界磨石", 0},        // 強化材料
    {"磨石", 0},            // 強化材料
    {"護貝劑", 0},          // 強化材料
    {"高級護貝劑", 0},      // 強化材料
    {"生釉", 0},            // 強化材料
    {"強化釉", 0},          // 強化材料
    {"HP恢復藥劑", 0},      // 珍貴消耗品
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
// IsGoodPtr is defined in memory_reader.h
static float Distance2D(float ax, float az, float bx, float bz) {
  float dx = ax - bx;
  float dz = az - bz;
  return sqrtf(dx * dx + dz * dz);
}
static bool HasUsableWorldPos(float x, float z) {
  if (!std::isfinite(x) || !std::isfinite(z))
    return false;
  if ((fabsf(x) <= 0.01f) && (fabsf(z) <= 0.01f))
    return false;
  if ((fabsf(x) <= 2.0f) && (fabsf(z) <= 2.0f))
    return false;
  if (fabsf(x) >= 100000.0f || fabsf(z) >= 100000.0f)
    return false;
  return true;
}

// ═══════════════ 二次檢查輔助函數 ═══════════════
// 在技能、撿物、右鍵攻擊前呼叫，確保狀態正確
static bool PreActionCheck(const char* actionName) {
  // 0. 檢查全域終止旗標（最優先）
  if (!g_Running) {
    static int s_warnCount = 0;
    if (s_warnCount < 5) {
      UIAddLog("[PreCheck] %s 失敗: g_Running=false", actionName);
      s_warnCount++;
    }
    return false;
  }
  // 1. 檢查 Bot 是否啟用
  if (!g_cfg.active.load()) {
    static int s_warnCount2 = 0;
    if (s_warnCount2 < 5) {
      UIAddLog("[PreCheck] %s 失敗: g_cfg.active=false", actionName);
      s_warnCount2++;
    }
    return false;
  }
  // 2. 檢查 Bot 狀態（必須是 HUNTING）
  BotState state = (BotState)g_State.load();
  if (state != BotState::HUNTING) {
    static int s_warnCount3 = 0;
    if (s_warnCount3 < 5) {
      UIAddLog("[PreCheck] %s 失敗: state=%d (需要 HUNTING=%d)", actionName, (int)state, (int)BotState::HUNTING);
      s_warnCount3++;
    }
    return false;
  }
  // 3. 檢查授權
  if (!IsLicenseValid()) {
    static int s_warnCount4 = 0;
    if (s_warnCount4 < 5) {
      UIAddLog("[PreCheck] %s 失敗: 授權無效", actionName);
      s_warnCount4++;
    }
    return false;
  }
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
    if (bytes[i] >= 0x20 && bytes[i] <= 0x7E)
      printable++;
  }
  return printable >= 3;
}
static bool IsLikelyValidInventorySlot(DWORD itemId, DWORD itemCount) {
  if (!itemId || itemId == 0xFFFFFFFF || itemId == 0xCCCCCCCC)
    return false;
  if (itemId >= 0xF0000000)
    return false;
  if ((itemId & 0xFF000000) != 0)
    return false;
  if (LooksLikeAsciiDword(itemId))
    return false;
  if (itemCount == 0 || itemCount > 9999)
    return false;
  return true;
}
static int ClampRelativeCoord(int value, int limit) {
  if (value < 0)
    return 0;
  if (value >= limit)
    return limit - 1;
  return value;
}
static void ClampRelativePoint(int *x, int *y) {
  if (!x || !y)
    return;
  *x = ClampRelativeCoord(*x, 1024);
  *y = ClampRelativeCoord(*y, 768);
}
static BYTE SkillKeyFromIndex(int index) {
  index %= 10;
  if (index < 0)
    index = 0;
  return (index == 9) ? (BYTE)'0' : (BYTE)('1' + index);
}
static bool HasTownMapConfig() {
  bool hasConfig = false;
  EnterCriticalSection(&g_cfg.cs_protected);
  hasConfig = !g_cfg.townMapIds.empty();
  LeaveCriticalSection(&g_cfg.cs_protected);
  return hasConfig;
}
static bool IsInventoryScanUsable(InventoryScanStatus status) {
  return status == InventoryScanStatus::OK ||
         status == InventoryScanStatus::EMPTY;
}
static const char *GetPlayerStateReadStatusName(PlayerStateReadStatus status) {
  switch (status) {
  case PlayerStateReadStatus::OK:
    return "OK";
  case PlayerStateReadStatus::READ_FAILED:
    return "READ_FAILED";
  case PlayerStateReadStatus::INVALID_DATA:
    return "INVALID_DATA";
  default:
    return "UNKNOWN";
  }
}
static const char *GetInventoryScanStatusName(InventoryScanStatus status) {
  switch (status) {
  case InventoryScanStatus::OK:
    return "OK";
  case InventoryScanStatus::EMPTY:
    return "EMPTY";
  case InventoryScanStatus::INVALID_HANDLE:
    return "INVALID_HANDLE";
  case InventoryScanStatus::INVALID_BASE:
    return "INVALID_BASE";
  default:
    return "UNKNOWN";
  }
}
static bool IsPlausibleMapId(int mapId) {
  // 允許 0 到 65535 (WORD 範圍) 或 -1 (未知)
  return (mapId >= 0 && mapId <= 65535) || mapId == -1;
}

// ═══════════════ 唯一指定的偏移讀取（無候選）═══════════════
// MapID 唯一偏移 (CE 2026-05-02 驗證)
static constexpr DWORD kMapIdOffset = 0x930DEC;
static constexpr DWORD kPosXOffset = 0x930DF8;  // ✅ CE 驗證
static constexpr DWORD kPosZOffset = 0x930DFC;  // ✅ CE 驗證 (+4)

static bool ReadMapId(GameHandle *gh, int *outMapId) {
  if (!gh || !gh->hProcess || !gh->baseAddr)
    return false;
  if (!outMapId)
    return true;

  int mapId = SafeRPM<int>(gh->hProcess, gh->baseAddr + kMapIdOffset, -1);
  if (IsPlausibleMapId(mapId)) {
    *outMapId = mapId;
    return true;
  }
  return false;
}

static bool ReadPlayerPos(GameHandle *gh, float *outX, float *outY,
                          float *outZ) {
  if (!gh || !gh->hProcess || !gh->baseAddr)
    return false;

  float x = SafeRPM<float>(gh->hProcess, gh->baseAddr + kPosXOffset, 0.0f);
  float z = SafeRPM<float>(gh->hProcess, gh->baseAddr + kPosZOffset, 0.0f);

  if (HasUsableWorldPos(x, z)) {
    if (outX)
      *outX = x;
    if (outZ)
      *outZ = z;
    float y = SafeRPM<float>(gh->hProcess, gh->baseAddr + 0x930E00, 0.0f);  // ✅ CE 驗證
    if (outY)
      *outY = y;
    return true;
  }

  // Fallback 1: 嘗試從 Python Packet Listener 讀取座標
  float px = 0.0f, py = 0.0f, pz = 0.0f;
  if (ReadCoordsFromPython(&px, &pz, &py)) {
    if (HasUsableWorldPos(px, pz)) {
      if (outX)
        *outX = px;
      if (outZ)
        *outZ = pz;
      if (outY)
        *outY = py;
      return true;
    }
  }

  // Fallback 2: 嘗試從 NetHook DLL 共享記憶體讀取座標
  px = py = pz = 0.0f;
  if (ReadPlayerCoordsFromNetHook(&px, &pz, &py)) {
    if (HasUsableWorldPos(px, pz)) {
      if (outX)
        *outX = px;
      if (outZ)
        *outZ = pz;
      if (outY)
        *outY = py;
      return true;
    }
  }

  return false;
}

// 玩家狀態修補：當主偏移讀取失敗時使用備用偏移
static void PatchPlayerState(GameHandle *gh, PlayerState *st) {
  if (!gh || !st)
    return;

  // MapID 修補
  if (!IsPlausibleMapId(st->mapId)) {
    ReadMapId(gh, &st->mapId);
  }

  // 座標修補
  if (!HasUsableWorldPos(st->x, st->z)) {
    float px = 0.0f, py = 0.0f, pz = 0.0f;
    if (ReadPlayerPos(gh, &px, &py, &pz)) {
      st->x = px;
      st->y = py;
      st->z = pz;
    }
  }
}
// ============================================================
// Logging
// ============================================================
static FILE *s_logFile = NULL;
static void OpenLogFile() {
  if (s_logFile)
    return;
  char path[MAX_PATH] = {0};
  GetModuleFileNameA(NULL, path, MAX_PATH);
  char *slash = strrchr(path, '\\');
  if (slash) {
    strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "RanBot_Trainer.log");
  } else {
    strcpy_s(path, "RanBot_Trainer.log");
  }
  fopen_s(&s_logFile, path, "a");
}
void Log(const char *tag, const char *msg) {
  char line[768];
  SYSTEMTIME st;
  GetLocalTime(&st);
  sprintf_s(line, "[%s][%02d:%02d:%02d] %s", tag ? tag : "LOG", st.wHour,
            st.wMinute, st.wSecond, msg ? msg : "");
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
void Logf(const char *tag, const char *fmt, ...) {
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
#if !defined(_M_IX86) || defined(_WIN64)
  return;
#else
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
    PEB_FULL *peb = (PEB_FULL *)__readfsdword(0x30);
    if (peb) {
      peb->BeingDebugged = FALSE;
      peb->NtGlobalFlag = 0;
      Logf("防偵測", "✓ PEB 反偵測設置完成 (BeingDebugged=0, NtGlobalFlag=0)");
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Logf("防偵測", "⚠️ PEB 修改失敗（異常）");
  }
#endif
}
static void InitAntiDebugHooks() {
#if !defined(_M_IX86) || defined(_WIN64)
  return;
#else
  auto ResolveExportTarget = [](DWORD_PTR p) -> DWORD_PTR {
    if (!p)
      return 0;
    __try {
      BYTE *b = (BYTE *)p;
      if (b[0] == 0xE9) {
        int rel = *(int *)(b + 1);
        return (DWORD_PTR)(b + 5 + rel);
      }
      if (b[0] == 0xFF && b[1] == 0x25) {
        DWORD addr = *(DWORD *)(b + 2);
        DWORD_PTR target = *(DWORD_PTR *)addr;
        return target;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return p;
  };

  Logf("防偵測", "正在安裝 Inline Patch Anti-Debug 保護...");
  HMODULE hK32 = GetModuleHandleA("kernel32.dll");
  if (!hK32) {
    Logf("防偵測", "⚠️ 無法取得 kernel32.dll 句柄");
    return;
  }
  HMODULE hKB = GetModuleHandleA("KernelBase.dll");
  DWORD_PTR pIsDbg =
      (DWORD_PTR)GetProcAddress(hKB ? hKB : hK32, "IsDebuggerPresent");
  if (pIsDbg) {
    pIsDbg = ResolveExportTarget(pIsDbg);
    DWORD oldProt = 0;
    if (VirtualProtect((LPVOID)pIsDbg, 3, PAGE_EXECUTE_READWRITE, &oldProt)) {
      BYTE patch[] = {0x31, 0xC0, 0xC3};
      memcpy((void *)pIsDbg, patch, sizeof(patch));
      VirtualProtect((LPVOID)pIsDbg, 3, oldProt, &oldProt);
      Logf("防偵測", "✓ IsDebuggerPresent Patch 成功");
    }
  }
  DWORD_PTR pDbgBrk = (DWORD_PTR)GetProcAddress(hKB ? hKB : hK32, "DebugBreak");
  if (pDbgBrk) {
    pDbgBrk = ResolveExportTarget(pDbgBrk);
    DWORD oldProt = 0;
    if (VirtualProtect((LPVOID)pDbgBrk, 1, PAGE_EXECUTE_READWRITE, &oldProt)) {
      BYTE patch[] = {0xC3};
      memcpy((void *)pDbgBrk, patch, sizeof(patch));
      VirtualProtect((LPVOID)pDbgBrk, 1, oldProt, &oldProt);
      Logf("防偵測", "✓ DebugBreak Patch 成功");
    }
  }
  Logf("防偵測", "Inline Patch Anti-Debug 保護安裝完成");
#endif
}
// ============================================================
// Cached player state
// ============================================================
static void UpdateUICache(const PlayerState &st) {
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
  return (st.maxHp > 1 || st.maxMp > 1 || st.maxSp > 1 || st.hp > 0 ||
          st.mp > 0 || st.sp > 0 || st.level > 0 || st.gold > 0 ||
          st.arrowCount > 0 || st.talismanCount > 0 ||
          st.combatPower > 0 || st.physAtkMin > 0 || st.sprAtkMin > 0 ||
          (st.targetId != 0 && st.targetId != 0xFFFFFFFF));
}
bool IsRelativeOnlyCombatMode() { return s_relativeOnlyCombatMode.load(); }
// ============================================================
// Memory helpers
// ============================================================
static DWORD GetLocalCharPtrExternal(GameHandle *gh) {
  if (!gh || !gh->hProcess || !gh->baseAddr)
    return 0;
  return gh->baseAddr + OffsetConfig::GLCharacterObj();
}

// ============================================================
// 偏移自動驗證系統
// ============================================================
struct OffsetCheck {
    const char* name;
    DWORD offset;
    DWORD expectedMin;
    DWORD expectedMax;
    DWORD value;
    bool valid;
};

void ValidatePlayerOffsets(GameHandle* gh) {
    if (!gh || !gh->hProcess || !gh->baseAddr) return;

    static bool s_done = false;
    if (s_done) return;
    s_done = true;

    printf("\n");
    printf("========================================\n");
    printf("[驗證] 開始偏移驗證...\n");
    printf("========================================\n");

    OffsetCheck checks[] = {
        {"HP", 0x930300, 1, 100000, 0, false},
        {"MaxHP", 0x930304, 1, 100000, 0, false},
        {"MP", 0x930308, 1, 100000, 0, false},
        {"MaxMP", 0x93030C, 1, 100000, 0, false},
        {"SP", 0x930310, 1, 100000, 0, false},
        {"MaxSP", 0x930314, 1, 100000, 0, false},
        {"Gold", 0x930250, 0, 999999999, 0, false},
        {"Level", 0x930248, 1, 300, 0, false},  // ❌ 待掃描驗證
        {"ArrowCount", 0x9308D8, 0, 99999, 0, false},
        {"STR", 0x932BF4, 1, 9999, 0, false},
        {"VIT", 0x932BF8, 1, 9999, 0, false},
        {"SPR", 0x932BFC, 1, 9999, 0, false},
        {"DEX", 0x932C00, 1, 9999, 0, false},
        {"PhysAtkMin", 0x933128, 1, 99999, 0, false},
        {"PhysAtkMax", 0x93312C, 1, 99999, 0, false},
        {"Defense", 0x933130, 0, 99999, 0, false},
        {"SprAtkMin", 0x933134, 1, 99999, 0, false},
        {"SprAtkMax", 0x933138, 1, 99999, 0, false},
        {"MapID", 0x930DEC, 0, 65535, 0, false},
    };

    int validCount = 0;
    int invalidCount = 0;

    for (auto& c : checks) {
        c.value = SafeRPM<DWORD>(gh->hProcess, gh->baseAddr + c.offset, 0xFFFFFFFF);
        c.valid = (c.value >= c.expectedMin && c.value <= c.expectedMax);

        if (c.valid) {
            validCount++;
            printf("  [OK]  %-15s = %-10d @ 0x%08X\n",
                   c.name, c.value, gh->baseAddr + c.offset);
        } else {
            invalidCount++;
            printf("  [!!]  %-15s = %-10d @ 0x%08X (期望: %d-%d)\n",
                   c.name, c.value, gh->baseAddr + c.offset,
                   c.expectedMin, c.expectedMax);
        }
    }

    printf("========================================\n");
    printf("[驗證] 結果: 有效=%d 無效=%d\n", validCount, invalidCount);
    printf("========================================\n");

    if (invalidCount > 0) {
        printf("\n[建議] 請將以下偏移更新到 offsets.h:\n\n");
        for (auto& c : checks) {
            if (!c.valid && c.value != 0xFFFFFFFF) {
                printf("  constexpr DWORD %-15s = 0x%X;  // 值=%d\n",
                       c.name, c.offset, c.value);
            }
        }
        printf("\n");
    }

    // 嘗試自動掃描找到正確偏移
    if (invalidCount > 0) {
        printf("\n[掃描] 嘗試自動掃描正確偏移...\n");

        // 讀取 HP 值用於匹配
        DWORD hpValue = 0;
        for (auto& c : checks) {
            if (strcmp(c.name, "HP") == 0) hpValue = c.value;
        }

        if (hpValue > 0) {
            printf("  已知 HP 值 = %d，掃描附近區域...\n", hpValue);

            // 掃描 0x930000 - 0x931000 區域
            for (DWORD off = 0x930000; off <= 0x931000; off += 4) {
                DWORD val = SafeRPM<DWORD>(gh->hProcess, gh->baseAddr + off, 0xFFFFFFFF);
                if (val == hpValue && off != 0x930300) {
                    printf("  [候選]  %-15s @ 0x%08X = %d\n",
                           "HP?", off, val);
                }
            }
        }
    }
}

// 前向宣告
static PlayerStateReadStatus ReadPlayerStateDetailedInternal(GameHandle *gh,
    PlayerState *out, char *reason, size_t reasonSize, bool allowRefresh);

static bool RefreshUiPlayerSnapshot(GameHandle *gh,
                                    PlayerState *outSnapshot = NULL) {
  if (!gh || !gh->hProcess || !gh->baseAddr)
    return false;

  PlayerState st = {};
  st.state = (BotState)g_State.load();

  // ✅ 統一調用 ReadPlayerStateDetailedInternal，Level 等所有屬性只讀取一次
  if (ReadPlayerStateDetailedInternal(gh, &st, NULL, 0, false) != PlayerStateReadStatus::OK) {
    return false;
  }

  if (outSnapshot)
    *outSnapshot = st;
  UpdateUICache(st);
  return true;
}
// ============================================================
// 實體池讀取（CROwList 鏈表）
// ============================================================
static bool s_entityPoolKnownBad = false;
static DWORD s_entityPoolBadTime = 0;
static DWORD GetCROWListHead(GameHandle *gh) {
  if (!gh || !gh->hProcess || !gh->baseAddr)
    return 0;
  if (s_entityPoolKnownBad) {
    DWORD now = GetTickCount();
    if (now - s_entityPoolBadTime < 2000) {
      // 冷卻期（2秒），返回 0 讓 EnumerateCrows 使用 Fallback
      return 0;
    }
    s_entityPoolKnownBad = false;
  }
  DWORD charAddr = GetLocalCharPtrExternal(gh);
  if (!charAddr)
    return 0;
  DWORD landMan = SafeRPM<DWORD>(
      gh->hProcess, charAddr + OffsetConfig::EntityLandManPtr(), 0);
  if (!IsGoodPtr(landMan)) {
    s_entityPoolKnownBad = true;
    s_entityPoolBadTime = GetTickCount();
    return 0;
  }
  DWORD head =
      SafeRPM<DWORD>(gh->hProcess, landMan + OffsetConfig::EntityCROWList(), 0);
  if (!IsGoodPtr(head))
    return 0;
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
// ═══════════════ 怪物掃描（唯一偏移定義）═══════════════
// 怪物結構偏移（基於 IDA+CE 驗證）
static constexpr DWORD kCrowHP = 0x7B0;
static constexpr DWORD kCrowServerID = 0x91C;
static constexpr DWORD kCrowMaxHP = 0x7B4;
static constexpr DWORD kCrowPosX = 0x878;
static constexpr DWORD kCrowPosY = 0x87C;
static constexpr DWORD kCrowPosZ = 0x880;
static constexpr DWORD kCrowDeadFlag = 0x9A0; // 死亡標記

// ═══════════════ 直接記憶體掃描 ════════════════
// 掃描範圍: gameBase + 0x1D0000 ~ gameBase + 0x2D0000
// ============================================================
static int EnumerateCrowsDirect(GameHandle *gh, CrowInfo *outCrows,
                                   int maxCrows) {
  if (!gh || !gh->hProcess || !gh->baseAddr || !outCrows)
    return 0;

  DWORD scanBase = gh->baseAddr + 0x1D0000;
  DWORD scanEnd = gh->baseAddr + 0x2D0000;
  const DWORD SCAN_STEP = 0x1000;
  const DWORD MAX_HP_REASONABLE = 10000000;

  int count = 0;
  for (DWORD addr = scanBase; addr < scanEnd && count < maxCrows;
       addr += SCAN_STEP) {
    MEMORY_BASIC_INFORMATION memInfo{};
    if (!VirtualQueryEx(gh->hProcess,
                        reinterpret_cast<LPCVOID>((DWORD_PTR)addr), &memInfo,
                        sizeof(memInfo)))
      continue;
    if (memInfo.State != MEM_COMMIT)
      continue;
    if (!(memInfo.Protect & (PAGE_READONLY | PAGE_READWRITE)))
      continue;

    // 在每個記憶體頁內檢查怪物
    for (DWORD offset = 0; offset < 0x1000 && count < maxCrows;
         offset += 0x100) {
      DWORD base = addr + offset;

      // 唯一偏移讀取
      DWORD hp = SafeRPM<DWORD>(gh->hProcess, base + kCrowHP, 0);
      if (hp == 0 || hp >= MAX_HP_REASONABLE)
        continue;

      DWORD serverId = SafeRPM<DWORD>(gh->hProcess, base + kCrowServerID, 0);
      DWORD mid = (serverId >> 8) & 0x3FF;
      DWORD sid = serverId & 0x3F;
      if (serverId == 0 || serverId == 0xFFFFFFFF)
        continue;
      if (mid >= 1024 || sid >= 64 || mid == 0)
        continue;

      // 檢查死亡標記
      BYTE deadFlag = SafeRPM<BYTE>(gh->hProcess, base + kCrowDeadFlag, 0);
      if (deadFlag != 0)
        continue;

      CrowInfo &ci = outCrows[count];
      ci.serverId = serverId;
      ci.crowDataPtr = base;
      ci.hp = hp;
      ci.maxHp = SafeRPM<WORD>(gh->hProcess, base + kCrowMaxHP, 0);
      if (ci.maxHp == 0)
        ci.maxHp = hp;
      ci.x = SafeRPM<float>(gh->hProcess, base + kCrowPosX, 0.0f);
      ci.y = SafeRPM<float>(gh->hProcess, base + kCrowPosY, 0.0f);
      ci.z = SafeRPM<float>(gh->hProcess, base + kCrowPosZ, 0.0f);

      if (count < 3) {
        Logf("掃描", "怪物[%d]: ID=0x%X HP=%d @ 0x%X", count, serverId, hp,
             base);
      }
      count++;
    }
  }

  if (count > 0) {
    Logf("掃描", "直接掃描找到 %d 隻怪物", count);
  }
  return count;
}

static int EnumerateCrows(GameHandle *gh, CrowInfo *outCrows, int maxCrows) {
  if (!gh || !gh->baseAddr || !outCrows)
    return 0;

  // ── 嘗試 NetHook 共享記憶體（若可用）──
  if (NetHookShmem_IsConnected()) {
    ShmemEntity entities[200];
    int count = NetHookShmem_EnumerateEntities(entities, 200);
    if (count > 0) {
      int outCount = 0;
      for (int i = 0; i < count && outCount < maxCrows; i++) {
        ShmemEntity &e = entities[i];
        if (e.id == 0 || e.id == 0xFFFFFFFF)
          continue;
        if (e.dead)
          continue;
        CrowInfo &ci = outCrows[outCount];
        ci.serverId = e.id;
        ci.crowDataPtr = 0;
        ci.hp = e.hp;
        ci.maxHp = e.maxHp;
        ci.x = e.x;
        ci.y = e.y;
        ci.z = e.z;
        outCount++;
      }
      if (outCount > 0)
        return outCount;
    }
  }

  // ── CROwList 直接讀取（需要 hProcess）──
  if (!gh->hProcess)
    return 0;
  DWORD headNode = GetCROWListHead(gh);
  if (headNode) {
    int count = 0;
    DWORD nodeAddr = headNode;
    DWORD visited[200];
    int visitCount = 0;
    while (nodeAddr && count < maxCrows &&
           visitCount < OffsetConfig::EntityMaxCrows()) {
      bool alreadyVisited = false;
      for (int i = 0; i < visitCount; i++) {
        if (visited[i] == nodeAddr) {
          alreadyVisited = true;
          break;
        }
      }
      if (alreadyVisited)
        break;
      visited[visitCount++] = nodeAddr;
      DWORD crowPtr = SafeRPM<DWORD>(
          gh->hProcess, nodeAddr + OffsetConfig::CrowNodeCrowPtr(), 0);
      DWORD nextNode = SafeRPM<DWORD>(
          gh->hProcess, nodeAddr + OffsetConfig::CrowNodeNext(), 0);
      if (crowPtr && IsGoodPtr(crowPtr)) {
        DWORD crowData = SafeRPM<DWORD>(
            gh->hProcess, crowPtr + OffsetConfig::CrowDataPtr(), 0);
        if (crowData && IsGoodPtr(crowData)) {
          CrowInfo &ci = outCrows[count];
          ci.serverId = SafeRPM<DWORD>(
              gh->hProcess, crowPtr + OffsetConfig::CrowServerID(), 0);
          ci.crowDataPtr = crowData;
          ci.hp =
              SafeRPM<DWORD>(gh->hProcess, crowPtr + OffsetConfig::CrowHP(), 0);
          WORD maxHp = SafeRPM<WORD>(gh->hProcess,
                                     crowPtr + OffsetConfig::CrowMaxHP(), 0);
          ci.maxHp = maxHp > 0 ? (DWORD)maxHp : ci.hp;
          ci.x = SafeRPM<float>(gh->hProcess,
                                crowPtr + OffsetConfig::CrowPosX(), 0.0f);
          ci.y = SafeRPM<float>(gh->hProcess,
                                crowPtr + OffsetConfig::CrowPosY(), 0.0f);
          ci.z = SafeRPM<float>(gh->hProcess,
                                crowPtr + OffsetConfig::CrowPosZ(), 0.0f);
          if (ci.serverId != 0 && ci.serverId != 0xFFFFFFFF && ci.hp > 0) {
            count++;
          }
        }
      }
      nodeAddr = nextNode;
    }
    if (count > 0)
      return count;
  }

  // ── Fallback：直接記憶體掃描 ──
  static DWORD s_lastDirectScan = 0;
  static int s_lastDirectCount = 0;
  static CrowInfo s_directCache[200] = {};
  DWORD now = GetTickCount();
  int cacheLimit = (int)(sizeof(s_directCache) / sizeof(s_directCache[0]));

  if (now - s_lastDirectScan <= 5000 && s_lastDirectCount > 0) {
    int copyCount = s_lastDirectCount;
    if (copyCount > maxCrows)
      copyCount = maxCrows;
    memcpy(outCrows, s_directCache, sizeof(CrowInfo) * copyCount);
    return copyCount;
  }

  int directCount = EnumerateCrowsDirect(gh, s_directCache, cacheLimit);
  if (directCount > 0) {
    s_lastDirectScan = now;
    s_lastDirectCount = directCount;
    int copyCount = directCount;
    if (copyCount > maxCrows)
      copyCount = maxCrows;
    memcpy(outCrows, s_directCache, sizeof(CrowInfo) * copyCount);
    return copyCount;
  }

  s_lastDirectScan = now;
  s_lastDirectCount = 0;
  return 0;
}

struct PCInfo {
  DWORD serverId;
  float x, y, z;
  DWORD hp, maxHp;
};

static int EnumeratePCs(GameHandle *gh, PCInfo *outPCs, int maxPCs) {
  if (!gh || !gh->hProcess || !gh->baseAddr || !outPCs)
    return 0;
  DWORD charAddr = GetLocalCharPtrExternal(gh);
  if (!charAddr)
    return 0;
  DWORD landMan = SafeRPM<DWORD>(
      gh->hProcess, charAddr + OffsetConfig::EntityLandManPtr(), 0);
  if (!landMan || !IsGoodPtr(landMan))
    return 0;
  DWORD headNode =
      SafeRPM<DWORD>(gh->hProcess, landMan + OffsetConfig::EntityPCList(), 0);
  if (!headNode || !IsGoodPtr(headNode))
    return 0;
  int count = 0;
  DWORD nodeAddr = headNode;
  DWORD visited[100];
  int visitCount = 0;
  while (nodeAddr && count < maxPCs && visitCount < 100) {
    bool alreadyVisited = false;
    for (int i = 0; i < visitCount; i++) {
      if (visited[i] == nodeAddr) {
        alreadyVisited = true;
        break;
      }
    }
    if (alreadyVisited)
      break;
    visited[visitCount++] = nodeAddr;
    DWORD pcPtr = SafeRPM<DWORD>(gh->hProcess,
                                 nodeAddr + OffsetConfig::CrowNodeCrowPtr(), 0);
    DWORD nextNode = SafeRPM<DWORD>(gh->hProcess,
                                    nodeAddr + OffsetConfig::CrowNodeNext(), 0);
    if (pcPtr && IsGoodPtr(pcPtr)) {
      PCInfo &pi = outPCs[count];
      pi.serverId =
          SafeRPM<DWORD>(gh->hProcess, pcPtr + OffsetConfig::PCServerID(), 0);
      pi.hp = SafeRPM<DWORD>(gh->hProcess, pcPtr + OffsetConfig::PCHP(), 0);
      pi.maxHp =
          SafeRPM<DWORD>(gh->hProcess, pcPtr + OffsetConfig::PCMaxHP(), 0);
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
static bool RefreshPlayerServerId(GameHandle *gh, const PlayerState &st,
                                  DWORD *outServerId) {
  if (outServerId)
    *outServerId = 0;
  if (!gh || !gh->hProcess || !gh->baseAddr)
    return false;
  if (!HasUsableWorldPos(st.x, st.z))
    return false;
  PCInfo pcs[32];
  int pcCount = EnumeratePCs(gh, pcs, 32);
  if (pcCount <= 0)
    return false;
  float bestDist = 99999.0f;
  DWORD bestId = 0;
  for (int i = 0; i < pcCount; i++) {
    const PCInfo &pi = pcs[i];
    if (pi.serverId == 0 || pi.serverId == 0xFFFFFFFF)
      continue;
    if (!HasUsableWorldPos(pi.x, pi.z))
      continue;
    if ((int)pi.hp != st.hp)
      continue;
    float d = Distance2D(st.x, st.z, pi.x, pi.z);
    if (d < bestDist) {
      bestDist = d;
      bestId = pi.serverId;
    }
  }
  if (bestId != 0 && bestDist < 10.0f) {
    if (outServerId)
      *outServerId = bestId;
    return true;
  }
  return false;
}
// ============================================================
// 背包快取
// ============================================================
static InvSlot s_invCache[78];
static int s_invCacheCount = 0;
static DWORD s_invCacheTime = 0;
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
  SetLicenseValid(false);
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
  s_lastValidPlayerStatePid = 0;
  s_lastValidPlayerStateBase = 0;
  s_consecutiveReadFail = 0;
  s_enteredHunting = false;
  s_wasInHunting = false;
  s_enteredDeadState = false;
  s_loggedDead = false;
  s_loggedReturn = false;
  if (g_cfg.protected_item_ids.empty()) {
    for (int i = 0; i < BotConfig::defaultProtectedCount; i++) {
      int id = BotConfig::defaultProtectedItems[i].id;
      if (id != 0) { // 只添加有效的ID
        g_cfg.protected_item_ids.push_back(id);
      }
    }
    if (!g_cfg.protected_item_ids.empty()) {
      Logf("保護", "已載入 %d 個預設保護物品",
           (int)g_cfg.protected_item_ids.size());
    } else {
      Logf("保護", "請在設定中手動添加保護物品");
    }
  }
  {
    char cachedToken[8192] = {0};
    OfflineLicenseInfo info = {};
    if (OfflineLicenseLoadCached(cachedToken, sizeof(cachedToken)) &&
        OfflineLicenseVerifySimple(cachedToken, &info)) {
      SetLicenseValid(true);
      Logf("認證", "卡密快取驗證成功，剩餘 %d 天", info.days_left);
    } else {
      SetLicenseValid(false);
      if (cachedToken[0]) {
        Logf("認證", "卡密快取驗證失敗：%s",
             info.message.empty() ? "未知原因" : info.message.c_str());
      } else {
        Logf("認證", "未找到已緩存卡密");
      }
    }
  }

  // ═══════════════════════════════════════════════════════════════
  // 隱形自動偏移恢復（已停用 - 代碼不完整）
  // ═══════════════════════════════════════════════════════════════
  // 此功能需要完整的 gh 上下文，InitBotLogic 中不可用

  // ✅ BUG-007 FIX: InitAttackSender 非阻塞（連線在背景線程完成）
  InitAttackSender("210.64.10.55", 6870);
  UIAddLog("[Bot] 攻擊封包發送器已初始化（非阻塞）");
  InitAntiDebugProtection();
  InitAntiDebugHooks();
  Logf("系統", "Bot 邏輯初始化完成");
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
bool IsEntityPoolWorking() { return !s_entityPoolKnownBad; }
static InventoryScanStatus ScanInventoryDetailed(GameHandle *gh,
                                                 std::vector<InvSlot> *out,
                                                 int *outCount) {
  if (out)
    out->clear();
  if (outCount)
    *outCount = 0;
  if (!gh || !gh->hProcess || !gh->baseAddr) {
    return InventoryScanStatus::INVALID_HANDLE;
  }

  if (NetHookShmem_IsConnected()) {
    ShmemInvItem shmemItems[78] = {};
    int shmemCount = NetHookShmem_EnumerateInventory(shmemItems, 78);
    if (shmemCount > 0) {
      int count = 0;
      for (int i = 0; i < shmemCount; ++i) {
        const ShmemInvItem &item = shmemItems[i];
        if (item.slot >= OffsetConfig::InvMaxSlots())
          continue;
        if (!IsLikelyValidInventorySlot(item.itemId, item.count))
          continue;
        InvSlot slot;
        slot.slotIdx = (int)item.slot;
        slot.itemId = item.itemId;
        slot.count = item.count;
        slot.valid = true;
        if (out)
          out->push_back(slot);
        count++;
      }
      if (outCount)
        *outCount = count;
      return count > 0 ? InventoryScanStatus::OK : InventoryScanStatus::EMPTY;
    }
  }

  DWORD glChar = GetLocalCharPtrExternal(gh);
  if (IsGoodPtr(glChar)) {
    DWORD inv =
        SafeRPM<DWORD>(gh->hProcess, glChar + OffsetConfig::InvItemPtr(), 0);
    if (IsGoodPtr(inv))
      s_invBase = inv;
  }

  if (!IsGoodPtr(s_invBase)) {
    DWORD now = GetTickCount();
    if (now - s_lastInventoryDiagLog > 3000) {
      Logf("背包", "❌ 背包基底無效，跳過背包掃描");
      s_lastInventoryDiagLog = now;
    }
    return InventoryScanStatus::INVALID_BASE;
  }

  DWORD testDword = SafeRPM<DWORD>(gh->hProcess, s_invBase, 0xCDCDCDCD);
  if (testDword == 0xCDCDCDCD || testDword == 0) {
    s_invBase = 0;
    if (IsGoodPtr(glChar)) {
      DWORD inv =
          SafeRPM<DWORD>(gh->hProcess, glChar + OffsetConfig::InvItemPtr(), 0);
      if (IsGoodPtr(inv))
        s_invBase = inv;
    }
    if (!IsGoodPtr(s_invBase)) {
      DWORD now = GetTickCount();
      if (now - s_lastInventoryDiagLog > 3000) {
        Logf("背包", "❌ 背包基底刷新失敗，無法辨識背包內容");
        s_lastInventoryDiagLog = now;
      }
      return InventoryScanStatus::INVALID_BASE;
    }
  }

  int count = 0;
  for (int i = 0; i < OffsetConfig::InvMaxSlots(); ++i) {
    DWORD slotAddr = s_invBase + (DWORD)i * OffsetConfig::InvSlotStride();
    DWORD itemId =
        SafeRPM<DWORD>(gh->hProcess, slotAddr + OffsetConfig::InvItemId(), 0);
    DWORD itemCount = SafeRPM<DWORD>(
        gh->hProcess, slotAddr + OffsetConfig::InvItemCount(), 0);
    if (!IsLikelyValidInventorySlot(itemId, itemCount))
      continue;
    InvSlot slot;
    slot.slotIdx = i;
    slot.itemId = itemId;
    slot.count = itemCount;
    slot.valid = true;
    if (out)
      out->push_back(slot);
    count++;
  }

  if (outCount)
    *outCount = count;
  return count > 0 ? InventoryScanStatus::OK : InventoryScanStatus::EMPTY;
}
static bool TryGetCachedInventoryCount(int *outCount, DWORD maxAgeMs) {
  if (!outCount || maxAgeMs == 0)
    return false;
  if (InterlockedCompareExchange(&s_invCacheCsInited, 0, 0) == 0)
    return false;
  DWORD now = GetTickCount();
  EnterCriticalSection(&s_invCacheCs);
  DWORD cacheTime = s_invCacheTime;
  int count = s_invCacheCount;
  LeaveCriticalSection(&s_invCacheCs);
  if (!cacheTime || now - cacheTime > maxAgeMs)
    return false;
  *outCount = count;
  return true;
}
static bool ValidatePlayerStateData(const PlayerState &st, DWORD charAddr,
                                    InventoryScanStatus invStatus, char *reason,
                                    size_t reasonSize) {
  if (!IsGoodPtr(charAddr)) {
    _snprintf_s(reason, reasonSize, _TRUNCATE, "GLCharacter 指標無效 (0x%08X)",
                charAddr);
    return false;
  }
  (void)invStatus;
  if (!IsPlausibleMapId(st.mapId)) {
    _snprintf_s(reason, reasonSize, _TRUNCATE, "地圖 ID 無效 (%d)", st.mapId);
    return false;
  }
  // ✅ 放寬座標驗證：如果 HP > 0 且 maxHp > 0，即使座標為零也接受
  // 這處理遊戲剛載入但座標尚未更新的情況
  if (!HasUsableWorldPos(st.x, st.z)) {
    if (st.hp > 0 && st.maxHp > 0 && st.maxHp <= 100000) {
      // HP 有效，座標可能尚未更新，接受資料
      static DWORD s_lastCoordWarning = 0;
      if (GetTickCount() - s_lastCoordWarning > 10000) {
        Logf("讀取", "⚠️ 座標為零但 HP 有效 (%d/%d)，接受資料", st.hp, st.maxHp);
        s_lastCoordWarning = GetTickCount();
      }
    } else {
      _snprintf_s(reason, reasonSize, _TRUNCATE, "座標無效 (x=%.1f z=%.1f)", st.x,
                  st.z);
      return false;
    }
  }
  if (st.maxHp <= 0 || st.maxMp <= 0 || st.maxSp <= 0) {
    _snprintf_s(reason, reasonSize, _TRUNCATE, "最大屬性無效 HP/MP/SP=%d/%d/%d",
                st.maxHp, st.maxMp, st.maxSp);
    return false;
  }
  if (st.hp <= 0 || st.hp > st.maxHp * 20 || st.mp < 0 ||
      st.mp > st.maxMp * 20 || st.sp < 0 || st.sp > st.maxSp * 20) {
    _snprintf_s(reason, reasonSize, _TRUNCATE, "屬性數值越界 HP/MP/SP=%d/%d/%d",
                st.hp, st.mp, st.sp);
    return false;
  }
  // ✅ Level 已修復為 2 Bytes 讀取，重新啟用驗證
  if (st.level < 1 || st.level > 300) {
    _snprintf_s(reason, reasonSize, _TRUNCATE, "等級數值異常 Lv=%d", st.level);
    return false;
  }
  return true;
}

// ═══════════════ PlayerState 資料可信度診斷工具 ═══════════════
// 只讀取不回寫，診斷哪個欄位異常，不做自動定位
struct PlayerStateDiagnostic {
  bool hp_abnormal = false;
  bool maxHp_abnormal = false;
  bool mp_abnormal = false;
  bool maxMp_abnormal = false;
  bool sp_abnormal = false;
  bool maxSp_abnormal = false;
  bool coord_abnormal = false;
  bool mapId_abnormal = false;
  bool level_abnormal = false;
  int abnormal_count = 0;
};

static void DiagnosePlayerStateFields(const PlayerState& st, PlayerStateDiagnostic* diag) {
  if (!diag) return;
  memset(diag, 0, sizeof(PlayerStateDiagnostic));
  if (!(st.hp > 0 && st.hp <= st.maxHp * 20)) { diag->hp_abnormal = true; diag->abnormal_count++; }
  if (!(st.maxHp > 0 && st.maxHp <= 1000000)) { diag->maxHp_abnormal = true; diag->abnormal_count++; }
  if (!(st.mp >= 0 && st.mp <= st.maxMp * 20)) { diag->mp_abnormal = true; diag->abnormal_count++; }
  if (!(st.maxMp > 0 && st.maxMp <= 1000000)) { diag->maxMp_abnormal = true; diag->abnormal_count++; }
  if (!(st.sp >= 0 && st.sp <= st.maxSp * 20)) { diag->sp_abnormal = true; diag->abnormal_count++; }
  if (!(st.maxSp > 0 && st.maxSp <= 1000000)) { diag->maxSp_abnormal = true; diag->abnormal_count++; }
  if (!HasUsableWorldPos(st.x, st.z)) { diag->coord_abnormal = true; diag->abnormal_count++; }
  if (!IsPlausibleMapId(st.mapId)) { diag->mapId_abnormal = true; diag->abnormal_count++; }
  if (st.level < 1 || st.level > 300) { diag->level_abnormal = true; diag->abnormal_count++; }
}

static void LogPlayerStateDiagnostic(const PlayerState& st, DWORD now) {
  static DWORD s_lastDiagLog = 0;
  if (now - s_lastDiagLog < 5000) return;
  s_lastDiagLog = now;
  PlayerStateDiagnostic diag;
  DiagnosePlayerStateFields(st, &diag);
  if (diag.abnormal_count == 0) return;
  Logf("診斷", "⚠️ PlayerState 異常 (%d 欄位):", diag.abnormal_count);
  if (diag.hp_abnormal) Logf("診斷", "  - HP=%d", st.hp);
  if (diag.maxHp_abnormal) Logf("診斷", "  - maxHp=%d", st.maxHp);
  if (diag.mp_abnormal) Logf("診斷", "  - MP=%d", st.mp);
  if (diag.maxMp_abnormal) Logf("診斷", "  - maxMp=%d", st.maxMp);
  if (diag.sp_abnormal) Logf("診斷", "  - SP=%d", st.sp);
  if (diag.maxSp_abnormal) Logf("診斷", "  - maxSp=%d", st.maxSp);
  if (diag.coord_abnormal) Logf("診斷", "  - 座標=(%.1f, %.1f)", st.x, st.z);
  if (diag.mapId_abnormal) Logf("診斷", "  - 地圖ID=%d", st.mapId);
  if (diag.level_abnormal) Logf("診斷", "  - 等級=%d", st.level);
}

static PlayerStateReadStatus
ReadPlayerStateDetailedInternal(GameHandle *gh, PlayerState *out, char *reason,
                                size_t reasonSize, bool allowRefresh) {
  if (!out)
    return PlayerStateReadStatus::READ_FAILED;

  *out = PlayerState{};
  out->maxHp = out->maxMp = out->maxSp = 1;
  out->state = (BotState)g_State.load();
  if (reason && reasonSize > 0)
    reason[0] = '\0';

  if (!gh || !gh->hProcess || !gh->baseAddr) {
    static DWORD s_lastDiag = 0;
    if (GetTickCount() - s_lastDiag > 3000) {
      Logf("讀取", "❌ ReadPlayerState: gh=%p hProcess=%p baseAddr=0x%08X", gh,
           gh ? gh->hProcess : 0, gh ? gh->baseAddr : 0);
      s_lastDiag = GetTickCount();
    }
    if (reason && reasonSize > 0) {
      _snprintf_s(reason, reasonSize, _TRUNCATE, "GameHandle 未就緒");
    }
    return PlayerStateReadStatus::READ_FAILED;
  }

  DWORD base = gh->baseAddr;
  DWORD charAddr = GetLocalCharPtrExternal(gh);
  if (charAddr && IsGoodPtr(charAddr)) {
    char nameBuf[22] = {0};
    SIZE_T bytesRead = 0;
    DWORD_PTR nameAddr = (DWORD_PTR)charAddr + 0x050;
    if (ReadProcessMemory(gh->hProcess, reinterpret_cast<LPCVOID>(nameAddr),
                          nameBuf, 21, &bytesRead) &&
        bytesRead > 0) {
      nameBuf[21] = '\0';
      memcpy(out->name, nameBuf, 22);
    } else {
      // BUG-H001 修復：strcpy_s(NULL) 會崩潰
      if (out && out->name)
        strcpy_s(out->name, sizeof(out->name), "???");
    }
  } else {
    // charAddr 無效時也要填預設值
    if (out && out->name)
      strcpy_s(out->name, sizeof(out->name), "???");
  }

  out->hp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerHP(), 0);
  out->maxHp =
      SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMaxHP(), 0);
  out->mp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMP(), 0);
  out->maxMp =
      SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMaxMP(), 0);
  out->sp = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerSP(), 0);
  out->maxSp =
      SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMaxSP(), 0);
  out->gold = SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerGold(), 0);
  // ✅ Level 是 2 Bytes (WORD)，需要用 WORD 讀取
  out->level = (int)SafeRPM<WORD>(gh->hProcess, base + OffsetConfig::PlayerLevel(), 0);
  // ✅ CE 驗證通過的讀取
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
  out->arrowCount =
      SafeRPM<int>(gh->hProcess, base + OffsetConfig::QuickSlotArrowCount(), 0);
  out->talismanCount = SafeRPM<int>(
      gh->hProcess, base + OffsetConfig::QuickSlotTalismanCount(), 0);
  out->mapId =
      SafeRPM<int>(gh->hProcess, base + OffsetConfig::PlayerMapID(), 0);

  bool posResolved = false;
  // 調試：打印座標讀取嘗試
  static DWORD s_lastPosDebugLog = 0;
  bool shouldLogPos = (GetTickCount() - s_lastPosDebugLog > 10000);

  if (charAddr && IsGoodPtr(charAddr)) {
    DWORD crowPosXAddr = charAddr + OffsetConfig::CrowPosX();
    DWORD crowPosZAddr = charAddr + OffsetConfig::CrowPosZ();
    float cx = SafeRPM<float>(gh->hProcess, crowPosXAddr, 0.0f);
    float cz = SafeRPM<float>(gh->hProcess, crowPosZAddr, 0.0f);
    if (shouldLogPos) {
      Logf("座標", "調試: GLChar=0x%08X CrowPosX@0x%08X=%.1f CrowPosZ@0x%08X=%.1f",
           charAddr, crowPosXAddr, cx, crowPosZAddr, cz);
    }
    if (HasUsableWorldPos(cx, cz)) {
      out->x = cx;
      out->z = cz;
      out->y = SafeRPM<float>(gh->hProcess, charAddr + OffsetConfig::CrowPosY(),
                              0.0f);
      posResolved = true;
      if (shouldLogPos) {
        Logf("座標", "✅ CrowPos 解析成功: (%.1f, %.1f)", out->x, out->z);
      }
    }
  }
  if (!posResolved) {
    DWORD playerPosXAddr = base + OffsetConfig::PlayerPosX();
    DWORD playerPosZAddr = base + OffsetConfig::PlayerPosZ();
    // ✅ PosX/PosZ 是 float (4 bytes)，需要用 float 讀取
    float wx = SafeRPM<float>(gh->hProcess, playerPosXAddr, 0.0f);
    float wz = SafeRPM<float>(gh->hProcess, playerPosZAddr, 0.0f);
    if (shouldLogPos) {
      Logf("座標", "調試: PlayerPosX@0x%08X=%.1f PlayerPosZ@0x%08X=%.1f",
           playerPosXAddr, wx, playerPosZAddr, wz);
    }
    if (HasUsableWorldPos(wx, wz)) {
      out->x = wx;
      out->z = wz;
      float wy = SafeRPM<float>(gh->hProcess, base + OffsetConfig::PlayerPosY(), 0.0f);
      out->y = wy;
      posResolved = true;
      if (shouldLogPos) {
        Logf("座標", "✅ PlayerPos 解析成功: (%.1f, %.1f)", out->x, out->z);
      }
    }
  }
  if (!posResolved && shouldLogPos) {
    s_lastPosDebugLog = GetTickCount();
  } else if (posResolved) {
    s_lastPosDebugLog = GetTickCount();
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

  // 目標讀取（唯一偏移）
  out->targetId =
      SafeRPM<DWORD>(gh->hProcess, base + OffsetConfig::TargetID(), 0xFFFFFFFF);
  out->hasTarget =
      SafeRPM<int>(gh->hProcess, base + OffsetConfig::TargetHasTarget(), 0);
  out->skillLockState =
      SafeRPM<int>(gh->hProcess, base + OffsetConfig::TargetLockedState(), 0);
  out->attackCount = SafeRPM<int>(gh->hProcess, base + 0x724, 0);
  out->attackRange = SafeRPM<int>(gh->hProcess, base + 0x728, 0);

  // 玩家狀態修補
  PatchPlayerState(gh, out);

  int inventoryCount = 0;
  InventoryScanStatus invStatus = InventoryScanStatus::INVALID_HANDLE;
  if (TryGetCachedInventoryCount(&inventoryCount, 1000)) {
    invStatus = inventoryCount > 0 ? InventoryScanStatus::OK
                                   : InventoryScanStatus::EMPTY;
  } else {
    invStatus = ScanInventoryDetailed(gh, NULL, &inventoryCount);
  }
  out->inventoryCount = (invStatus == InventoryScanStatus::INVALID_BASE ||
                         invStatus == InventoryScanStatus::INVALID_HANDLE)
                            ? -1
                            : inventoryCount;

  // ═══════════════ PlayerState 欄位可信度診斷 ═══════════════
  // 只 log 不回寫，幫助判斷哪個欄位異常
  DWORD nowDiag = GetTickCount();
  LogPlayerStateDiagnostic(*out, nowDiag);

  char localReason[160] = {0};
  if (!ValidatePlayerStateData(*out, charAddr, invStatus, localReason,
                               sizeof(localReason))) {
    DWORD refreshed = 0;
    if (allowRefresh) {
      refreshed = GetGameBaseAddress(gh);
    }
    if (allowRefresh && refreshed && refreshed != gh->baseAddr) {
      gh->baseAddr = refreshed;
      if (gh->attached) {
        SetGameHandle(gh);
      }
      return ReadPlayerStateDetailedInternal(gh, out, reason, reasonSize,
                                             false);
    }

    if (reason && reasonSize > 0) {
      strncpy_s(reason, reasonSize, localReason, _TRUNCATE);
    }
    DWORD now = GetTickCount();
    if (now - s_lastInvalidStateLog > 3000) {
      Logf("讀取",
           "⚠️ 玩家資料無效: %s [GLChar=0x%08X Map=%d x=%.1f z=%.1f HP=%d/%d "
           "Inv=%s Offset=%s MapRVA=0x%08X PosXRVA=0x%08X PosZRVA=0x%08X]",
           localReason, charAddr, out->mapId, out->x, out->z, out->hp,
           out->maxHp, GetInventoryScanStatusName(invStatus),
           OffsetConfig::GetLoadSource(), OffsetConfig::PlayerMapID(),
           OffsetConfig::PlayerPosX(), OffsetConfig::PlayerPosZ());
      s_lastInvalidStateLog = now;
    }
    return PlayerStateReadStatus::INVALID_DATA;
  }

  if (invStatus == InventoryScanStatus::INVALID_BASE ||
      invStatus == InventoryScanStatus::INVALID_HANDLE) {
    DWORD now = GetTickCount();
    if (now - s_lastInventoryDiagLog > 5000) {
      Logf("讀取", "⚠️ 背包狀態未知 (%s)，戰鬥可繼續，補給判定暫停",
           GetInventoryScanStatusName(invStatus));
      s_lastInventoryDiagLog = now;
    }
  }

  s_lastValidPlayerState = *out;
  s_lastValidPlayerStateTime = GetTickCount();
  s_lastValidPlayerStatePid = gh->pid;
  s_lastValidPlayerStateBase = gh->baseAddr;
  s_hasLastValidPlayerState = true;
  UpdateUICache(*out);
  return PlayerStateReadStatus::OK;
}
static PlayerStateReadStatus ReadPlayerStateDetailed(GameHandle *gh,
                                                     PlayerState *out,
                                                     char *reason,
                                                     size_t reasonSize) {
  return ReadPlayerStateDetailedInternal(gh, out, reason, reasonSize, true);
}
static bool TryGetCachedPlayerState(GameHandle *gh, PlayerState *out,
                                    DWORD maxAgeMs) {
  if (!gh || !out || !s_hasLastValidPlayerState || maxAgeMs == 0)
    return false;
  if (s_lastValidPlayerStatePid != gh->pid ||
      s_lastValidPlayerStateBase != gh->baseAddr)
    return false;
  DWORD now = GetTickCount();
  if (now - s_lastValidPlayerStateTime > maxAgeMs)
    return false;
  *out = s_lastValidPlayerState;
  out->state = (BotState)g_State.load();
  return true;
}
static PlayerStateReadStatus ReadPlayerStateCached(GameHandle *gh,
                                                   PlayerState *out,
                                                   char *reason,
                                                   size_t reasonSize,
                                                   DWORD maxAgeMs) {
  if (TryGetCachedPlayerState(gh, out, maxAgeMs)) {
    if (reason && reasonSize > 0)
      reason[0] = '\0';
    return PlayerStateReadStatus::OK;
  }
  return ReadPlayerStateDetailed(gh, out, reason, reasonSize);
}
bool ReadPlayerState(GameHandle *gh, PlayerState *out) {
  return ReadPlayerStateCached(gh, out, NULL, 0, 120) ==
         PlayerStateReadStatus::OK;
}
static int ReadEntitiesFromCROwList(GameHandle *gh,
                                    std::vector<Entity> *monsters,
                                    std::vector<Entity> *npcs,
                                    const PlayerState &ps) {
  if (!gh || !gh->hProcess || !gh->baseAddr)
    return 0;
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
    if (e.maxHp <= 0)
      e.maxHp = 1;
    e.isDead = e.hp <= 0;
    e.dist = Distance2D(ps.x, ps.z, e.x, e.z);
    if (monsters)
      monsters->push_back(e);
    total++;
  }
  return total;
}
int ScanEntities(GameHandle *gh, std::vector<Entity> *monsters,
                 std::vector<Entity> *npcs) {
  if (monsters)
    monsters->clear();
  if (npcs)
    npcs->clear();
  if (!IsLicenseValid())
    return 0;
  PlayerState ps;
  if (ReadPlayerStateCached(gh, &ps, NULL, 0, 120) !=
      PlayerStateReadStatus::OK)
    return 0;
  return ReadEntitiesFromCROwList(gh, monsters, npcs, ps);
}
int ScanInventory(GameHandle *gh, std::vector<InvSlot> *out) {
  int count = 0;
  InventoryScanStatus status = ScanInventoryDetailed(gh, out, &count);
  return (status == InventoryScanStatus::OK ||
          status == InventoryScanStatus::EMPTY)
             ? count
             : 0;
}
bool IsInventoryFull(GameHandle *gh) {
  int count = 0;
  InventoryScanStatus status = ScanInventoryDetailed(gh, NULL, &count);
  if (status != InventoryScanStatus::OK && status != InventoryScanStatus::EMPTY)
    return false;
  int pct = (count * 100) / OffsetConfig::InvMaxSlots();
  return pct >= g_cfg.inventory_full_pct.load();
}

bool IsPotionSlotsLow(GameHandle *gh) {
  if (!g_cfg.potion_check_enable.load())
    return false;

  std::vector<InvSlot> slots;
  int count = 0;
  InventoryScanStatus status = ScanInventoryDetailed(gh, &slots, &count);
  if (status != InventoryScanStatus::OK && status != InventoryScanStatus::EMPTY)
    return false;

  int start = g_cfg.potion_slot_start.load();
  int end = g_cfg.potion_slot_end.load();
  int minSlots = g_cfg.min_potion_slots.load();
  if (minSlots < 1)
    minSlots = 1;

  int occupiedCount = 0;
  for (const auto &slot : slots) {
    if (slot.slotIdx >= start && slot.slotIdx <= end && slot.itemId != 0) {
      occupiedCount++;
    }
  }
  return occupiedCount < minSlots;
}
void DumpInventoryItems(GameHandle *gh) {
  if (!gh || !gh->hProcess)
    return;
  std::vector<InvSlot> items;
  int count = ScanInventory(gh, &items);
  Logf("背包", "===== 背包物品 (共%d個) =====", count);
  for (const auto &item : items) {
    bool isProtected = false;
    EnterCriticalSection(&g_cfg.cs_protected);
    for (int pid : g_cfg.protected_item_ids) {
      if (pid == (int)item.itemId && pid != 0) {
        isProtected = true;
        break;
      }
    }
    LeaveCriticalSection(&g_cfg.cs_protected);
    Logf("背包", "格%d: ID=0x%X (%d個)%s", item.slotIdx, item.itemId,
         item.count, isProtected ? " [已保護]" : "");
  }
  Logf("背包", "===== 掃描完成 =====");
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
  if (n > 78)
    n = 78;
  for (int i = 0; i < n; i++) {
    s_guiInvCache[i] = items[i];
  }
  LeaveCriticalSection(&s_guiInvCs);
  return s_guiInvCount;
}
bool GetGuiInvSlot(int idx, int *outItemId, int *outCount) {
  if (idx < 0 || idx >= s_guiInvCount)
    return false;
  if (outItemId)
    *outItemId = (int)s_guiInvCache[idx].itemId;
  if (outCount)
    *outCount = (int)s_guiInvCache[idx].count;
  return true;
}
void AddProtectedItem(int itemId) {
  EnterCriticalSection(&g_cfg.cs_protected);
  for (int id : g_cfg.protected_item_ids) {
    if (id == itemId) {
      LeaveCriticalSection(&g_cfg.cs_protected);
      return;
    }
  }
  g_cfg.protected_item_ids.push_back(itemId);
  LeaveCriticalSection(&g_cfg.cs_protected);
  Logf("保護", "已添加物品ID=0x%X 到保護列表", itemId);
}
void RemoveProtectedItem(int itemId) {
  EnterCriticalSection(&g_cfg.cs_protected);
  for (auto it = g_cfg.protected_item_ids.begin();
       it != g_cfg.protected_item_ids.end();) {
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
  if (row < 0 || row >= BotConfig::MAX_INVENTORY_ROWS)
    return false;
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
    if (g_cfg.protected_rows[i])
      count++;
  }
  return count;
}
int GetCurrentSkillIndex() { return g_cfg.currentSkillIndex.load(); }
int GetHuntPointIndex() { return s_huntPointIndex; }
int GetHuntPointCount() {
  EnterCriticalSection(&g_cfg.cs_protected);
  int count = (int)g_cfg.huntWaypoints.size();
  LeaveCriticalSection(&g_cfg.cs_protected);
  return count > 0 ? count : 1;
}
int GetCombatIntentState() { return (int)s_combatIntent; }
// ═══════════════ GUI: 切換戰鬥意向 ═══════════════
void CycleCombatIntent() {
  DWORD now = GetTickCount();
  if (now - s_lastIntentChange < 200)
    return; // 防抖：200ms 內不重複
  s_lastIntentChange = now;

  int current = (int)s_combatIntent;
  int next = (current + 1) % 3;
  s_combatIntent = (CombatIntent)next;

  const char *names[] = {"SEEKING", "ENGAGING", "LOOTING"};
  const char *desc[] = {"尋找目標", "戰鬥中", "撿物中"};
  Logf("戰鬥", "[GUI] 戰鬥意向: %s (%s)", names[next], desc[next]);
  UIAddLog("[GUI] 戰鬥意向: %s", desc[next]);
}
// ═══════════════ GUI: 切換輔助技能 ═══════════════
static int s_supportSkillIndex = 0;
int GetCurrentSupportSkillIndex() { return s_supportSkillIndex; }
void CycleSupportSkill(int delta) {
  int maxSkills = g_cfg.auxSkillCount.load();
  if (maxSkills < 1)
    maxSkills = 1;
  if (maxSkills > BotConfig::MAX_AUX_SKILLS)
    maxSkills = BotConfig::MAX_AUX_SKILLS;

  s_supportSkillIndex += delta;
  if (s_supportSkillIndex < 0)
    s_supportSkillIndex = maxSkills - 1;
  if (s_supportSkillIndex >= maxSkills)
    s_supportSkillIndex = 0;

  Logf("技能", "[GUI] 選擇輔助技能: %d/%d", s_supportSkillIndex + 1, maxSkills);
  UIAddLog("[GUI] 輔助技能: %d/%d", s_supportSkillIndex + 1, maxSkills);
}
DWORD GetKillCount() { return s_killCount; }
void GetPlayerName(char *outName, int maxLen) {
  if (!outName || maxLen <= 0)
    return;
  outName[0] = '\0';
  GameHandle gh = GetGameHandle();
  if (!gh.attached || !gh.hProcess)
    return;
  DWORD charAddr = GetLocalCharPtrExternal(&gh);
  if (!charAddr)
    return;
  SIZE_T bytesRead = 0;
  int readLen = (maxLen < 21) ? maxLen : 21;
  ReadProcessMemory(gh.hProcess,
                    (LPCVOID)(charAddr + OffsetConfig::PlayerName()), outName,
                    readLen, &bytesRead);
  outName[maxLen - 1] = '\0';
}
void CacheInventory(GameHandle *gh) {
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
bool GetCachedInvSlot(int idx, int *outItemId, int *outCount) {
  EnterCriticalSection(&s_invCacheCs);
  bool ok = false;
  if (idx >= 0 && idx < s_invCacheCount && idx < 78) {
    if (outItemId)
      *outItemId = (int)s_invCache[idx].itemId;
    if (outCount)
      *outCount = (int)s_invCache[idx].count;
    ok = s_invCache[idx].valid;
  }
  LeaveCriticalSection(&s_invCacheCs);
  return ok;
}
DWORD GetCachedInvTime() { return s_invCacheTime; }
bool FindNearestMonster(GameHandle *gh, Entity *out) {
  if (!gh || !out)
    return false;
  int range = g_cfg.attack_range.load();
  std::vector<Entity> monsters;
  ScanEntities(gh, &monsters, NULL);
  if (monsters.empty())
    return false;
  Entity *best = nullptr;
  float bestScore = -1.0f;
  for (auto &e : monsters) {
    if (e.isDead || e.hp <= 0)
      continue;
    if (IsFailedTarget(e.id))
      continue;
    if (e.dist > (float)range)
      continue;
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
    Logf("戰鬥", "目標: ID=0x%X, dist=%.1f, HP=%d/%d (score=%.1f)", out->id,
         out->dist, out->hp, out->maxHp, bestScore);
    return true;
  }
  return false;
}
bool FindNearestNPC(GameHandle *gh, Entity *out) {
  std::vector<Entity> npcs;
  ScanEntities(gh, NULL, &npcs);
  if (npcs.empty())
    return false;
  auto it = std::min_element(
      npcs.begin(), npcs.end(),
      [](const Entity &a, const Entity &b) { return a.dist < b.dist; });
  if (it == npcs.end())
    return false;
  if (out)
    *out = *it;
  return true;
}
bool GetMonsterById(GameHandle *gh, DWORD id, Entity *out) {
  std::vector<Entity> monsters;
  ScanEntities(gh, &monsters, NULL);
  for (const Entity &e : monsters) {
    if (e.id == id) {
      if (out)
        *out = e;
      return true;
    }
  }
  return false;
}
bool IsTargetDying(GameHandle *gh) {
  PlayerState st;
  if (!ReadPlayerState(gh, &st))
    return false;
  if (!st.hasTarget || st.targetId == 0xFFFFFFFF || st.targetId == 0)
    return false;
  Entity e;
  if (!GetMonsterById(gh, st.targetId, &e))
    return false;
  return e.isDead || (e.maxHp > 0 && e.hp <= 0);
}
BotState GetBotState() { return (BotState)g_State.load(); }
static const char *GetStateName(BotState s) {
  switch (s) {
  case BotState::IDLE:
    return "IDLE";
  case BotState::HUNTING:
    return "HUNTING";
  case BotState::DEAD:
    return "DEAD";
  case BotState::RETURNING:
    return "RETURNING";
  case BotState::TOWN_SUPPLY:
    return "TOWN_SUPPLY";
  case BotState::BACK_TO_FIELD:
    return "BACK_TO_FIELD";
  case BotState::PAUSED:
    return "PAUSED";
  case BotState::RECOVERY:
    return "RECOVERY";
  default:
    return "UNKNOWN";
  }
}
static void BuildStateSnapshot(const PlayerState *st, char *buf,
                               size_t bufSize) {
  if (!buf || bufSize == 0)
    return;
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
  s_enteredRelativeCombat = false;
  g_cfg.currentSkillIndex.store(0);
  g_cfg.lastRightClickTime.store(0);
  g_cfg.lastSkillTime.store(0);
  // 重置失敗目標追蹤（避免舊資料殘留）
  memset(s_failedTargets, 0, sizeof(s_failedTargets));
  s_failedCount = 0;
}
static bool CanUseRelativeOnlyCombat(GameHandle *gh) {
  return gh && gh->attached && gh->hWnd && IsWindow(gh->hWnd);
}
static void SetRelativeOnlyCombatMode(bool enabled, const char *reason) {
  bool previous = s_relativeOnlyCombatMode.exchange(enabled);
  if (previous == enabled)
    return;

  if (enabled) {
    s_enteredRelativeCombat = false;
    Logf("戰鬥", "↪️ 啟用純相對座標戰鬥模式: %s",
         reason && reason[0] ? reason : "玩家資料不可用");
    UIAddLog("[Bot] 已切換為純相對座標戰鬥模式 (%s)",
             reason && reason[0] ? reason : "玩家資料不可用");
  } else {
    s_enteredRelativeCombat = false;
    Logf("戰鬥", "↩️ 關閉純相對座標戰鬥模式: %s",
         reason && reason[0] ? reason : "玩家資料已恢復");
    UIAddLog("[Bot] 已恢復一般戰鬥模式 (%s)",
             reason && reason[0] ? reason : "玩家資料已恢復");
  }
}
static void TransitionState(BotState nextState, const char *reason,
                            const PlayerState *st) {
  EnsureUICacheReady(); // 確保鎖已初始化
  EnterCriticalSection(&s_stateTransitionCs);

  // ═══════════════ 狀態轉換防護驗證 ═══════════════
  // 防護1: 防止同狀態重複轉換
  BotState oldState = (BotState)g_State.load();
  if (oldState == nextState) {
    LeaveCriticalSection(&s_stateTransitionCs);
    return;
  }

  // 防護2: 緊急停止狀態不可被其他狀態覆蓋（除非是 IDLE）
  if (oldState == BotState::EMERGENCY_STOP && nextState != BotState::IDLE) {
    static DWORD s_lastEmergLog = 0;
    if (GetTickCount() - s_lastEmergLog > 2000) {
      Logf("狀態機", "⚠️ 拒絕轉換: 處於 EMERGENCY_STOP 狀態，只能轉換到 IDLE");
      s_lastEmergLog = GetTickCount();
    }
    LeaveCriticalSection(&s_stateTransitionCs);
    return;
  }

  // 防護3: 防止狀態轉換頻率過高（500ms 內不允許重複轉換）
  // 注意：轉移到 IDLE 永遠允許，STOP 必須能夠立即重置狀態
  static BotState s_lastTransitionState = BotState::IDLE;
  static DWORD s_lastTransitionTime = 0;
  DWORD nowTransition = GetTickCount();
  if (nextState != BotState::IDLE &&
      s_lastTransitionState == oldState &&  // ✅ 修復：比較舊狀態
      (nowTransition - s_lastTransitionTime) < 500) {
    LeaveCriticalSection(&s_stateTransitionCs);
    return;
  }
  s_lastTransitionState = oldState;  // ✅ 記錄舊狀態
  s_lastTransitionTime = nowTransition;

  // 防護4: 從 DEAD 轉換時，如果 HP 仍為 0，警告並延遲
  if (oldState == BotState::DEAD && nextState != BotState::DEAD) {
    if (st && st->hp <= 0 && st->maxHp > 0) {
      Logf("狀態機", "⚠️ 警告: HP 仍為 0，但即將從 DEAD 轉換到 %s",
           GetStateName(nextState));
    }
  }

  const PlayerState *snapshot =
      st ? st : (s_hasLastValidPlayerState ? &s_lastValidPlayerState : NULL);
  char snapBuf[128];
  BuildStateSnapshot(snapshot, snapBuf, sizeof(snapBuf));

  DWORD now = GetTickCount();
  DWORD lastValidAge =
      s_lastValidPlayerStateTime ? (now - s_lastValidPlayerStateTime) : 0;

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
  s_partialDeadSeenTime = 0;

  g_State.store((int)nextState);

  Logf("狀態轉換", "[狀態機] %s -> %s [原因: %s]", GetStateName(oldState),
       GetStateName(nextState), reason ? reason : "n/a");
  Logf("狀態轉換", "  └- 資料: %s | 最近有效讀值: %s | 偏移來源: %s", snapBuf,
       s_lastValidPlayerStateTime ? "有" : "無", OffsetConfig::GetLoadSource());
  if (s_lastValidPlayerStateTime) {
    Logf("狀態轉換", "  └- 最近有效讀值距今: %lu ms",
         (unsigned long)lastValidAge);
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
    Logf("狀態機", "  └- RETURNING: 重置回城計時器和卡片發送標記");
    break;
  case BotState::TOWN_SUPPLY:
    SupplyForceReset();
    // Bug 4 修復：如果來自 DEAD，先確認位置
    if (oldState == BotState::DEAD) {
      s_returnCardSent = false;
      s_returnStartTime = 0;
      Logf("狀態機", "  └- TOWN_SUPPLY: 來自 DEAD，重置回城卡片狀態");
    }
    Logf("狀態機", "  └- TOWN_SUPPLY: 重置補給 FSM 狀態");
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
    Logf("狀態機", "  └- BACK_TO_FIELD: 開始返回野外計時");
    break;
  case BotState::HUNTING:
    ResetCombatRuntimeState();
    // ✅ 修復：初始化 s_targetLockTime 為當前時間，讓意圖狀態機正確計算延遲
    s_targetLockTime = now;
    s_enteredHunting = false;
    s_wasInHunting = false;
    s_supplyPhase = 0; // 防止非正常退出 TOWN_SUPPLY 時的狀態污染
    Logf("狀態機", "  └- HUNTING: 重置戰鬥意向與技能輪替");
    break;
  case BotState::DEAD:
    s_deathStartTime = now; // ✅ R2 FIX: 用 TransitionState 裡已存在的
                            // now（而非延後到 TickDead）
    s_deadRecoverySeenTime = 0;
    s_reviveClicked = false;
    s_reviveRetryCount = 0;
    s_enteredDeadState = false;
    s_loggedDead = false;
    Logf("狀態機", "  └- DEAD: 記錄死亡時間，重置復活狀態");
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
    s_antiPkCooldownUntil = 0;
    s_supplyPhase = 0; // 確保非正常退出時狀態乾淨
    Logf("狀態機", "  └- IDLE: 清空戰鬥/補給殘留狀態");
    break;
  case BotState::PAUSED:
    s_pausedPreviousState = oldState;
    Logf("狀態機", "  └- PAUSED: 已暫停（之前: %s）",
         GetStateName(s_pausedPreviousState));
    break;
  default:
    break;
  }
  LeaveCriticalSection(&s_stateTransitionCs);
}
void SetBotState(BotState s) { TransitionState(s, "SetBotState", NULL); }
BotConfig *GetBotConfig() { return &g_cfg; }
void StopBot() {
  if (s_stopInProgress)
    return;
  BotState prevState = (BotState)g_State.load();
  if (!g_cfg.active.load() && prevState == BotState::IDLE)
    return;
  s_stopInProgress = true;

  // 調試：輸出精確時間
  SYSTEMTIME st;
  GetLocalTime(&st);
  Logf("認證", "[DEBUG] StopBot() 被調用！時間=%02d:%02d:%02d.%03d",
       st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
  Logf("認證", "[DEBUG] StopBot: active=%d state=%s",
       g_cfg.active.load(), GetStateName(prevState));

  Logf("狀態機", "========================================");
  Logf("狀態機", "[STOP] Bot 停止！");
  Logf("狀態機", "  └- 之前狀態: %s -> IDLE", GetStateName(prevState));
  Logf("狀態機", "  └- active=%d g_Running=%d",
       g_cfg.active.load(), g_Running ? 1 : 0);
  Logf("狀態機", "========================================");
  g_cfg.active.store(false);
  // g_Running 保持不變！只軟停止 Bot，不要殺 BotThread

  // ═══════════════ 停止所有戰鬥意向 ═══════════════
  // 重置戰鬥意向狀態機
  s_combatIntent = CombatIntent::SEEKING;
  s_targetLostTime = 0;
  s_lootDelay = 800;
  s_lastPickupTime = 0;
  s_targetLockTime = 0;
  s_lastTargetHp = 0;
  s_engageStartTime = 0;

  // 重置技能冷卻
  for (int i = 0; i < BotConfig::MAX_SKILLS; i++) {
    s_skillLastTime[i] = 0;
  }
  g_cfg.currentAttackSkillIndex.store(0);
  g_cfg.currentAuxSkillIndex.store(0);
  g_cfg.currentSkillIndex.store(0);

  // 重置失敗目標列表
  s_failedCount = 0;
  memset(s_failedTargets, 0, sizeof(s_failedTargets));

  // 重置防呆移動
  s_antiStuckLastMove = 0;
  s_antiStuckPhase = 0;

  // 重置循環掃描索引
  s_huntPointIndex = 0;
  s_relativeScanIndex = 0;
  s_relativeSkillIndex = 0;

  TransitionState(BotState::IDLE, "StopBot", NULL);
  s_stopInProgress = false;
}
void ForceStopBot() { StopBot(); }
void ToggleBotActive() {
  static DWORD s_lastToggleMs = 0;
  DWORD nowMs = GetTickCount();
  if (nowMs - s_lastToggleMs < 300) return;
  s_lastToggleMs = nowMs;

  bool wasActive = g_cfg.active.load();

  // ════ 停止邏輯 ════
  if (wasActive) {
    // Bot 正在運行，呼叫 StopBot 軟停止
    Logf("Bot", "[F11] 軟停止 Bot (active: 1->0)");
    StopBot();
    return;
  }

  // ════ 啟動邏輯 ════
  // 認證檢查
  if (!IsLicenseValid()) {
    Logf("Bot", "❌ 卡密未驗證，拒絕啟動");
    UIAddLog("[Bot] 卡密未驗證，無法啟動");
    return;
  }

  // 檢查遊戲視窗
  GameHandle gh = GetGameHandle();
  if (!gh.hWnd || !IsWindow(gh.hWnd)) {
    Logf("Bot", "⚠️ 遊戲視窗無效，等待中...");
    UIAddLog("[Bot] 等待遊戲視窗...");
    // 仍允許啟動，BotTick 會等待視窗
  }

  Logf("Bot", "========================================");
  Logf("Bot", "[START] 原點圓形掃描模式啟動！");
  Logf("Bot", "  gh.hWnd=%p base=0x%08X", gh.hWnd, gh.baseAddr);
  Logf("Bot", "========================================");

  // 初始化技能計時
  DWORD initNow = GetTickCount();
  g_cfg.lastSkillTime.store(initNow - 800);  // 讓技能立即可用
  g_cfg.lastAuxSkillTime.store(initNow);
  g_cfg.currentAttackSkillIndex.store(0);
  g_cfg.currentAuxSkillIndex.store(0);
  g_cfg.currentSkillIndex.store(0);

  // 重置圓形掃描索引
  s_relativeScanIndex = 0;

  // 重置技能欄
  s_curBar = (char)0xFF;

  // 啟動
  g_cfg.active.store(true);
  TransitionState(BotState::HUNTING, "ToggleBotActive", NULL);

  Logf("Bot", "[START] Bot 啟動完成");
  UIAddLog("[Bot] ✅ 已啟動原點圓形掃描模式");
}
void ResetBotTarget() { ResetCombatRuntimeState(); }
int GetMonsterCount(GameHandle *gh) {
  if (!IsLicenseValid())
    return 0;
  std::vector<Entity> monsters;
  return ScanEntities(gh, &monsters, NULL) >= 0 ? (int)monsters.size() : 0;
}
int GetInventoryCount(GameHandle *gh) {
  if (!IsLicenseValid())
    return 0;
  return ScanInventory(gh, NULL);
}
int GetSkillLockState(GameHandle *gh) {
  if (!gh || !gh->hProcess || !gh->baseAddr)
    return 0;
  return SafeRPM<int>(gh->hProcess,
                      gh->baseAddr + OffsetConfig::TargetLockedState(), 0);
}
int GetAttackCount(GameHandle *gh) {
  if (!IsLicenseValid())
    return 0;
  if (!gh || !gh->hProcess || !gh->baseAddr)
    return 0;
  return SafeRPM<int>(gh->hProcess, gh->baseAddr + 0x724, 0);
}
int GetAttackRange(GameHandle *gh) {
  if (!gh || !gh->hProcess || !gh->baseAddr)
    return 0;
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

// 前向宣告
static bool DoSupportSkill(HWND hWnd);
static bool HasNearbyPlayer(GameHandle *gh, const PlayerState &st, float range);

// ============================================================
// 切換技能欄位
// ============================================================
static void SwitchBar(HWND hWnd, BYTE barKey) {
  // ✅ 二次檢查：確保狀態正確
  if (!PreActionCheck("SwitchBar"))
    return;

  if (!hWnd)
    return;
  if (s_curBar == barKey)
    return;
  SendKeyInputFocused(barKey, hWnd);
  SleepJitter(200);  // ✅ 增加延遲，確保技能欄切換完成
  s_curBar = (char)barKey;
  if (barKey == VK_F1) {
    Logf("技能", "[欄位] 切換 F1 欄（1~5攻擊 / 6~0輔助）");
  } else {
    Logf("技能", "[欄位] 切換欄位 VK=0x%02X", (unsigned)barKey);
  }
}
// ============================================================
// 喝水邏輯
// ============================================================
static void DoPotionTick(HWND hWnd, const PlayerState &st) {
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
      Logf("喝水", "❌ HWND 為 NULL，無法發送按鍵！");
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
    Logf("喝水",
         "狀態: HP=%d/%d=%d%%(閾值<%d%%) MP=%d/%d=%d%%(閾值<%d%%) "
         "SP=%d/%d=%d%%(閾值<%d%%)",
         st.hp, st.maxHp, hpPct, hpTh, st.mp, st.maxMp, mpPct, mpTh, st.sp,
         st.maxSp, spPct, spTh);
    lastDiagLog = now;
  }
  if (hpPct < hpTh) {
    if (hpKey != 0) {
      SendKeyInputFocused(hpKey, hWnd);
      Logf("喝水", "HP %d%% < %d%%，按 %c", hpPct, hpTh, (char)hpKey);
    }
  }
  if (mpPct < mpTh) {
    if (mpKey != 0) {
      SendKeyInputFocused(mpKey, hWnd);
      Logf("喝水", "MP %d%% < %d%%，按 %c", mpPct, mpTh, (char)mpKey);
    }
  }
  if (spPct < spTh) {
    if (spKey != 0) {
      SendKeyInputFocused(spKey, hWnd);
      Logf("喝水", "SP %d%% < %d%%，按 %c", spPct, spTh, (char)spKey);
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
// 遊戲時間自動傳送
// ═══════════════════════════════════════════════════════════════════
static int s_lastReturnTriggerDay = -1;
static int s_lastFieldTriggerDay = -1;

static int GetDateSerial() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  return st.wYear * 10000 + st.wMonth * 100 + st.wDay;
}

bool CheckGameTimeReturn(GameHandle *gh) {
  if (!gh || !gh->hProcess)
    return false;
  if (!g_cfg.auto_game_time.load())
    return false;

  DWORD gameHour = SafeRPM<DWORD>(
      gh->hProcess, gh->baseAddr + OffsetConfig::GameTimeHour(), 0xFF);
  DWORD gameMinute = SafeRPM<DWORD>(
      gh->hProcess, gh->baseAddr + OffsetConfig::GameTimeMinute(), 0xFF);

  int targetHour = g_cfg.game_time_hour.load();
  int targetMin = g_cfg.game_time_min.load();
  int today = GetDateSerial();

  if (today < s_lastReturnTriggerDay) {
    s_lastReturnTriggerDay = today;
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

bool CheckGameTimeBackToField(GameHandle *gh) {
  if (!gh || !gh->hProcess)
    return false;
  if (!g_cfg.auto_game_time.load())
    return false;
  if (!g_cfg.auto_return_to_field.load())
    return false;

  DWORD gameHour = SafeRPM<DWORD>(
      gh->hProcess, gh->baseAddr + OffsetConfig::GameTimeHour(), 0xFF);
  DWORD gameMinute = SafeRPM<DWORD>(
      gh->hProcess, gh->baseAddr + OffsetConfig::GameTimeMinute(), 0xFF);

  int targetHour = g_cfg.game_time_return_hour.load();
  int targetMin = g_cfg.game_time_return_min.load();
  int today = GetDateSerial();

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
      if (s_failedTargets[i].failTime < s_failedTargets[oldest].failTime)
        oldest = i;
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
        for (int j = i; j < s_failedCount - 1; j++)
          s_failedTargets[j] = s_failedTargets[j + 1];
        s_failedCount--;
      }
    }
    s_lastFailedPurge = now;
  }
  for (int i = 0; i < s_failedCount; i++) {
    if (s_failedTargets[i].id == targetId)
      return true;
  }
  return false;
}
static void ClearFailedTarget(DWORD targetId) {
  for (int i = 0; i < s_failedCount; i++) {
    if (s_failedTargets[i].id == targetId) {
      for (int j = i; j < s_failedCount - 1; j++)
        s_failedTargets[j] = s_failedTargets[j + 1];
      s_failedCount--;
      return;
    }
  }
}
// ============================================================
// 放棄目標
// ============================================================
static void GiveUpTarget(GameHandle *gh, const char *reason) {
  DWORD now = GetTickCount();
  Logf("戰鬥", "放棄目標 ID=0x%08X 原因: %s (擊殺:%d)", s_currentTargetId,
       reason, s_killCount);
  AddFailedTarget(s_currentTargetId);
  s_currentTargetId = 0;
  if (now - s_lastIntentChange > 100) {
    s_combatIntent = CombatIntent::SEEKING;
    s_lastIntentChange = now;
  }
  s_engageStartTime = 0;
  if (gh && gh->hProcess && gh->baseAddr) {
    SafeNtWPM(gh->hProcess, gh->baseAddr + OffsetConfig::TargetID(),
              (DWORD)0xFFFFFFFF);
    SafeNtWPM(gh->hProcess, gh->baseAddr + OffsetConfig::TargetHasTarget(),
              (int)0);
  }
  // ── 清除遊戲內鎖定框（按 ESC 取消目標瞄準）──
  if (gh && gh->hWnd) {
    SendKeyDirect(gh->hWnd, VK_ESCAPE);
    Sleep(30);
  }
}
static bool RunPacketCombatIntentTick(GameHandle *gh, const PlayerState &st) {
  if (!gh || !gh->hProcess || !gh->baseAddr)
    return false;
  DWORD now = GetTickCount();
  if (!IsAttackSenderConnected()) {
    static DWORD s_lastConnectTry = 0;
    if (now - s_lastConnectTry > 5000) {
      TryAttackSenderConnect(50);
      s_lastConnectTry = now;
    }
    if (!IsAttackSenderConnected())
      return false;
  }
  if (now - s_playerServerIdTime > 10000 || s_playerServerId == 0) {
    DWORD sid = 0;
    if (RefreshPlayerServerId(gh, st, &sid)) {
      s_playerServerId = sid;
      s_playerServerIdTime = now;
      DWORD mid = 0, low = 0;
      if (SplitServerId(s_playerServerId, &mid, &low)) {
        SetPlayerID(mid, low);
      }
    }
  }
  if (s_playerServerId == 0)
    return false;

  if (s_combatIntent == CombatIntent::LOOTING) {
    if (s_targetLostTime == 0) {
      s_targetLostTime = now;
      return true;
    }
    if (now - s_targetLostTime >= s_lootDelay) {
      s_targetLostTime = 0;
      s_currentTargetId = 0;
      if (now - s_lastIntentChange > 100) {
        s_combatIntent = CombatIntent::SEEKING;
        s_lastIntentChange = now;
      }
    }
    return true;
  }

  if (s_combatIntent == CombatIntent::SEEKING) {
    Entity e;
    if (!FindNearestMonster(gh, &e))
      return false;
    if (e.id == 0 || e.id == 0xFFFFFFFF)
      return false;
    if (IsFailedTarget(e.id))
      return false;
    s_currentTargetId = e.id;
    s_engageStartTime = now;
    s_hpCheckTime = now;
    s_lastTargetHp = (DWORD)e.hp;
    s_targetLockTime = now;
    if (gh->hProcess && gh->baseAddr) {
      SafeNtWPM(gh->hProcess, gh->baseAddr + OffsetConfig::TargetID(),
                (DWORD)e.id);
      SafeNtWPM(gh->hProcess, gh->baseAddr + OffsetConfig::TargetHasTarget(),
                (int)1);
    }
    if (now - s_lastIntentChange > 100) {
      s_combatIntent = CombatIntent::ENGAGING;
      s_lastIntentChange = now;
    }
    return true;
  }

  if (s_combatIntent != CombatIntent::ENGAGING)
    return true;
  if (s_currentTargetId == 0) {
    if (now - s_lastIntentChange > 100) {
      s_combatIntent = CombatIntent::SEEKING;
      s_lastIntentChange = now;
    }
    return false;
  }

  Entity e;
  if (!GetMonsterById(gh, s_currentTargetId, &e)) {
    GiveUpTarget(gh, "TargetNotFound");
    return false;
  }
  if (e.isDead || e.hp <= 0) {
    s_killCount++;
    ClearFailedTarget(s_currentTargetId);
    s_targetLostTime = now;
    if (now - s_lastIntentChange > 100) {
      s_combatIntent = CombatIntent::LOOTING;
      s_lastIntentChange = now;
    }
    return true;
  }

  DWORD engageElapsed = s_engageStartTime ? (now - s_engageStartTime) : 0;
  if (engageElapsed > ENGAGE_HARD_TIMEOUT) {
    GiveUpTarget(gh, "EngageHardTimeout");
    return true;
  }

  if ((DWORD)e.hp < s_lastTargetHp) {
    s_lastTargetHp = (DWORD)e.hp;
    s_hpCheckTime = now;
  } else {
    DWORD noDropElapsed = s_hpCheckTime ? (now - s_hpCheckTime) : 0;
    if (noDropElapsed > ENGAGE_HP_TIMEOUT) {
      GiveUpTarget(gh, "TargetHpNotDropping");
      return true;
    }
  }

  DWORD interval = (DWORD)g_cfg.attackSkillInterval.load();
  if (interval < 10)
    interval = 10;
  if (now - s_lastCombatTick < interval)
    return true;
  s_lastCombatTick = now;

  DWORD attackerMid = 0, attackerSid = 0;
  DWORD targetMid = 0, targetSid = 0;
  if (!SplitServerId(s_playerServerId, &attackerMid, &attackerSid))
    return true;
  if (!SplitServerId(s_currentTargetId, &targetMid, &targetSid))
    return true;

  bool ok = SendAttackPacket(attackerMid, attackerSid, targetMid, targetSid);
  return ok;
}
// ============================================================
// 輔助技能（6~0 鍵自動施放）
// ============================================================
static bool DoSupportSkill(HWND hWnd) {
  // ✅ 二次檢查：確保狀態正確
  if (!PreActionCheck("輔助技能"))
    return false;

  if (!hWnd) {
    static DWORD s_logTime = 0;
    if (GetTickCount() - s_logTime > 5000) {
      Logf("Debug", "[輔助技能] hWnd 無效");
      s_logTime = GetTickCount();
    }
    return false;
  }

  if (!g_cfg.auxEnabled.load()) {
    static DWORD s_logTime = 0;
    if (GetTickCount() - s_logTime > 5000) {
      Logf("Debug", "[輔助技能] auxEnabled=0 (請在GUI開啟)");
      s_logTime = GetTickCount();
    }
    return false;
  }

  DWORD now = GetTickCount();
  int intervalSec = g_cfg.auxCastIntervalSec.load();
  if (intervalSec <= 0)
    intervalSec = 30;
  DWORD intervalMs = (DWORD)intervalSec * 1000UL;

  // 檢查冷卻時間（毫秒）
  DWORD lastAuxTime = g_cfg.lastAuxSkillTime.load();
  if (lastAuxTime != 0 && now - lastAuxTime < intervalMs) {
    static DWORD s_logTime = 0;
    if (GetTickCount() - s_logTime > 5000) {
      Logf("Debug", "[輔助技能] 冷卻中: elapsed=%lums < interval=%lums",
           now - lastAuxTime, intervalMs);
      s_logTime = GetTickCount();
    }
    return false;
  }

  int auxCount = g_cfg.auxSkillCount.load();
  if (auxCount < 1)
    auxCount = 1;
  if (auxCount > BotConfig::MAX_AUX_SKILLS)
    auxCount = BotConfig::MAX_AUX_SKILLS;

  // 輪替輔助技能 6~0
  int auxIdx = g_cfg.currentAuxSkillIndex.load() % auxCount;
  // 輔助技能鍵 6, 7, 8, 9, 0 (共5個)
  BYTE numKey = (auxIdx == 4) ? (BYTE)'0' : (BYTE)('6' + auxIdx);

  // 施放輔助技能
  // ✅ 調試日誌：確認輔助技能是否被發送
  Logf("技能", "★★★ [輔助技能%c] idx=%d/%d interval=%ds ★★★",
       (char)numKey, auxIdx + 1, auxCount, intervalSec);
  SwitchBar(hWnd, g_cfg.attackBarKey.load());
  SleepJitter(80);
  SendKeyInputFocused(numKey, hWnd);
  SleepJitter(120);

  // 更新狀態
  DWORD castTime = GetTickCount();
  g_cfg.lastAuxSkillTime.store(castTime);
  int nextIdx = (auxIdx + 1) % auxCount;
  g_cfg.currentAuxSkillIndex.store(nextIdx);

  return true;
}
// ============================================================
// 戰鬥意向狀態機（純相對座標版 - 自動流轉）
// SEEKING → ENGAGING → LOOTING → SEEKING
// ============================================================
static void UpdateCombatIntent(HWND hWnd) {
  if (!hWnd)
    return;

  DWORD now = GetTickCount();
  CombatIntent current = s_combatIntent;

  // ✅ 調試日誌：顯示意圖狀態
  static DWORD s_lastIntentDebugLog = 0;
  if (now - s_lastIntentDebugLog > 5000) {
    DWORD targetElapsed = now - s_targetLockTime;
    DWORD engageElapsed = current == CombatIntent::ENGAGING ? (now - s_engageStartTime) : 0;
    Logf("Debug", "[意圖] 當前=%d targetElapsed=%lums engageElapsed=%lums lastChange=%lu",
         (int)current, (unsigned long)targetElapsed, (unsigned long)engageElapsed, s_lastIntentChange);
    s_lastIntentDebugLog = now;
  }

  switch (current) {
  case CombatIntent::SEEKING: {
    // 第一次進入 SEEKING，立即輸出日誌
    static bool s_wasNotSeeking = false;
    if (!s_wasNotSeeking) {
      Logf("戰鬥", "[意向] → SEEKING（尋找怪物）");
      s_wasNotSeeking = true;
    }
    // 直接進入戰鬥（圓形掃描不需要視覺鎖怪）
    if (now - s_targetLockTime > 200) { // 200ms 後進入戰鬥
      if (now - s_lastIntentChange > 100) {
        s_combatIntent = CombatIntent::ENGAGING;
        s_engageStartTime = now;
        s_targetLockTime = now;
        s_lastIntentChange = now;
        s_wasNotSeeking = false;  // 重置標誌
        Logf("戰鬥", "[意向] SEEKING → ENGAGING");
      }
    }
    break;
  }

  case CombatIntent::ENGAGING: {
    // 第一次進入 ENGAGING，立即輸出日誌
    static bool s_wasNotEngaging = false;
    if (!s_wasNotEngaging) {
      Logf("戰鬥", "[意向] → ENGAGING（戰鬥中）");
      s_wasNotEngaging = true;
    }
    // 攻擊持續一段時間後，進入撿物
    DWORD engageElapsed = now - s_engageStartTime;
    DWORD lootTimeout =
        (DWORD)g_cfg.attackSkillInterval.load() * 10; // 預設 8秒
    if (engageElapsed > lootTimeout && lootTimeout > 0) {
      if (now - s_lastIntentChange > 100) {
        s_combatIntent = CombatIntent::LOOTING;
        s_targetLostTime = now;
        s_lastIntentChange = now;
        s_wasNotEngaging = false;  // 重置標誌
        Logf("戰鬥", "[意向] ENGAGING → LOOTING（攻擊%ds）",
             engageElapsed / 1000);
      }
    } else {
      // 每 5 秒顯示一次戰鬥正在進行的日誌（避免無輸出讓人以為卡死）
      static DWORD s_lastEngageLog = 0;
      if (now - s_lastEngageLog > 5000) {
        Logf("戰鬥", "[意向] ENGAGING 中（已攻擊 %ds，等待轉換...）",
             engageElapsed / 1000);
        s_lastEngageLog = now;
      }
    }
    break;
  }

  case CombatIntent::LOOTING: {
    // 第一次進入 LOOTING，立即輸出日誌
    static bool s_wasNotLooting = false;
    if (!s_wasNotLooting) {
      Logf("戰鬥", "[意向] → LOOTING（撿物中）");
      s_wasNotLooting = true;
    }
    // 撿物持續一段時間後，回到尋怪
    if (now - s_targetLostTime > 500) { // 0.5秒後回到尋怪
      if (now - s_lastIntentChange > 100) {
        s_combatIntent = CombatIntent::SEEKING;
        s_targetLockTime = now;
        s_currentTargetId = 0;
        s_lastIntentChange = now;
        s_wasNotLooting = false;  // 重置標誌
        Log("戰鬥", "[意向] LOOTING → SEEKING");
      }
    }
    break;
  }
  }
}

// ═══════════════════════════════════════════════════════════════════
// RunRelativeOnlyCombatTick - 純定點圓形掃描模式（無記憶體依賴）
// ═══════════════════════════════════════════════════════════════════
static void RunRelativeOnlyCombatTick(GameHandle *gh, DWORD now) {
  // ═══════════════ g_Running 緊急停止檢查 ═══════════════
  if (!g_Running) {
    Logf("狀態機", "[STOP] g_Running=false，緊急停止圓形戰鬥");
    TransitionState(BotState::IDLE, "EmergencyStop", NULL);
    g_cfg.active.store(false);
    s_combatIntent = CombatIntent::SEEKING;
    Sleep(100);
    return;
  }
  if (!gh || !gh->hWnd) {
    static DWORD s_hwndLogTime = 0;
    if (now - s_hwndLogTime > 3000) {
      Logf("Debug", "[Combat] gh=%p hWnd=%p", gh, gh ? gh->hWnd : NULL);
      s_hwndLogTime = now;
    }
    return;
  }

  HWND hWnd = gh->hWnd;

  // ═══════════════ 計算時間間隔 ═══════════════
  DWORD skillInterval = (DWORD)g_cfg.attackSkillInterval.load();
  if (skillInterval < 100) skillInterval = 200;
  if (skillInterval > 5000) skillInterval = 5000;

  DWORD circleInterval = (DWORD)g_cfg.circleRotationIntervalMs.load();
  if (circleInterval < 60) circleInterval = 75;  // 8點 × 75ms = 600ms
  if (circleInterval > 1000) circleInterval = 1000;

  // ═══════════════ 進入戰鬥初始化 ═══════════════
  if (!s_enteredRelativeCombat) {
    Log("狀態機", "========================================");
    Log("狀態機", "[StationaryCircleMode] 定點圓形掃描模式已啟動");
    Logf("狀態機", "  └- 中心點: (%d,%d) | 點數: %d",
         g_cfg.attackCenterX.load(), g_cfg.attackCenterZ.load(),
         Coords::ATTACK_SCAN_COUNT);
    Logf("狀態機", "  └- 圓形掃描: %lums/點，約 %.1fs 一圈",
         (unsigned long)circleInterval,
         (circleInterval * Coords::ATTACK_SCAN_COUNT) / 1000.0f);
    Logf("狀態機", "  └- 攻擊技能: 1~%d | 輪替間隔: %lums",
         g_cfg.attackSkillCount.load(), (unsigned long)skillInterval);
    Logf("狀態機", "  └- 輔助技能: 6~0 | 施放間隔: %d 秒",
         g_cfg.auxCastIntervalSec.load());
    Logf("狀態機", "  └- 自動撿物: %s | 間隔: %dms",
         g_cfg.auto_pickup.load() ? "開" : "關",
         g_cfg.pickup_interval_ms.load());
    Log("狀態機", "========================================");

    // 初始化狀態
    s_combatIntent = CombatIntent::SEEKING;
    s_currentTargetId = 0;
    s_curBar = -1;
    s_relativeScanIndex = 0;
    s_relativeSkillIndex = 0;
    g_cfg.currentAuxSkillIndex.store(0);
    g_cfg.lastAuxSkillTime.store(0);
    g_cfg.lastSkillTime.store(0);
    g_cfg.currentAttackSkillIndex.store(0);
    g_cfg.currentSkillIndex.store(0);
    g_cfg.lastCirclePointTime.store(0);
    g_cfg.lastRightClickTime.store(0);
    s_lastPickupTime = 0;

    // 切換到 F1 技能欄
    SwitchBar(hWnd, VK_F1);
    s_enteredRelativeCombat = true;
  }

  // ═══════════════ 輔助技能（時間驅動） ═══════════════
  DoSupportSkill(hWnd);

  // ═══════════════ 撿物（時間驅動，無論意圖狀態） ═══════════════
  if (g_cfg.auto_pickup.load()) {
    DWORD pickupDelay = (DWORD)g_cfg.pickup_interval_ms.load();
    if (pickupDelay < 200) pickupDelay = 200;
    if (now - s_lastPickupTime > pickupDelay) {
      s_lastPickupTime = now;
      SendKeyInputFocused(g_cfg.key_pickup, hWnd);
    }
  }

  // ═══════════════ 技能欄切換（只執行一次） ═══════════════
  SwitchBar(hWnd, VK_F1);

  // ═══════════════ 攻擊技能輪替（時間驅動） ═══════════════
  DWORD elapsed = now - g_cfg.lastSkillTime.load();
  if (elapsed >= skillInterval) {
    g_cfg.lastSkillTime.store(now);

    int skillCount = g_cfg.attackSkillCount.load();
    if (skillCount < 1) skillCount = 5;
    if (skillCount > BotConfig::MAX_ATTACK_SKILLS)
      skillCount = BotConfig::MAX_ATTACK_SKILLS;

    int skillIndex = g_cfg.currentAttackSkillIndex.load() % skillCount;
    BYTE skillKey = (BYTE)('1' + skillIndex);

    Logf("戰鬥", "[技能%c] idx=%d/%d elapsed=%lums",
         (char)skillKey, skillIndex + 1, skillCount, (unsigned long)elapsed);
    SendKeyInputFocused(skillKey, hWnd);
    Sleep(50);

    int nextSkillIndex = (skillIndex + 1) % skillCount;
    g_cfg.currentAttackSkillIndex.store(nextSkillIndex);
    g_cfg.currentSkillIndex.store(nextSkillIndex);
    s_relativeSkillIndex = nextSkillIndex;
  }

  // ═══════════════ 圓形掃描（時間驅動，無記憶體依賴） ═══════════════
  if (now - g_cfg.lastCirclePointTime.load() >= circleInterval) {
    g_cfg.lastCirclePointTime.store(now);
    g_cfg.lastRightClickTime.store(now);

    int pointIndex = s_relativeScanIndex % Coords::ATTACK_SCAN_COUNT;
    ClickAttackPoint(hWnd, pointIndex);

    s_relativeScanIndex = (s_relativeScanIndex + 1) % Coords::ATTACK_SCAN_COUNT;

    Coords::ScanPoint pt = Coords::GetAttackPoint(pointIndex, g_cfg.attackCenter());
    Logf("戰鬥", "[圓形] 點%d/%d(%d,%d)",
         pointIndex + 1, Coords::ATTACK_SCAN_COUNT, pt.x, pt.z);
  }

  s_wasInHunting = true;
}
static int CountPotionSlotsInRange(const std::vector<InvSlot> &slots, int start,
                                   int end) {
  int occupiedCount = 0;
  for (const auto &slot : slots) {
    if (slot.slotIdx >= start && slot.slotIdx <= end && slot.itemId != 0) {
      occupiedCount++;
    }
  }
  return occupiedCount;
}
static DWORD GetTransitionConfirmTimeoutMs(DWORD minDelayMs, DWORD floorMs) {
  if (minDelayMs < floorMs)
    minDelayMs = floorMs;
  DWORD timeout = minDelayMs * 4;
  if (timeout < floorMs)
    timeout = floorMs;
  if (timeout > 30000)
    timeout = 30000;
  return timeout;
}
static bool ConfirmReturnArrival(const PlayerState &st, char *reason,
                                 size_t reasonSize) {
  bool validPos = HasUsableWorldPos(st.x, st.z);
  bool mapChanged = !s_returnSourceValid || st.mapId != s_returnSourceMapId;
  bool inTown = IsTownMap(st.mapId);
  bool townConfigured = HasTownMapConfig();

  if (inTown) {
    _snprintf_s(reason, reasonSize, _TRUNCATE, "命中城鎮 MapId=%d", st.mapId);
    return true;
  }
  if (validPos && mapChanged) {
    _snprintf_s(reason, reasonSize, _TRUNCATE,
                townConfigured
                    ? "Map 已變更且座標有效 (Map=%d)"
                    : "未配置 townMapIds，改用 map 變更確認 (Map=%d)",
                st.mapId);
    return true;
  }
  _snprintf_s(reason, reasonSize, _TRUNCATE,
              "尚未確認到城 (Map=%d, mapChanged=%d, inTown=%d)", st.mapId,
              mapChanged ? 1 : 0, inTown ? 1 : 0);
  return false;
}
static bool ConfirmBackToFieldArrival(const PlayerState &st, char *reason,
                                      size_t reasonSize) {
  bool validPos = HasUsableWorldPos(st.x, st.z);
  bool mapChanged =
      !s_backToFieldSourceValid || st.mapId != s_backToFieldSourceMapId;
  bool inTown = IsTownMap(st.mapId);

  if (s_backToFieldSourceWasTown) {
    if (validPos && !inTown && mapChanged) {
      _snprintf_s(reason, reasonSize, _TRUNCATE,
                  "已離開城鎮且切換地圖 (Map=%d)", st.mapId);
      return true;
    }
    _snprintf_s(reason, reasonSize, _TRUNCATE,
                "仍未確認離開城鎮 (Map=%d, mapChanged=%d, inTown=%d)", st.mapId,
                mapChanged ? 1 : 0, inTown ? 1 : 0);
    return false;
  }

  if (validPos && !inTown) {
    _snprintf_s(reason, reasonSize, _TRUNCATE,
                mapChanged ? "野外確認完成且地圖已變更 (Map=%d)"
                           : "野外確認完成，使用有效座標恢復戰鬥 (Map=%d)",
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
static bool s_approachThreatTriggered = false; // 防止短時間重複觸發

// 檢查是否有 entity 高速接近玩家，是則觸發回城
static bool CheckApproachingThreats(GameHandle *gh, const PlayerState &st,
                                    DWORD now) {
  if (!g_cfg.enemy_approach_detect.load())
    return false;

  DWORD interval = 500; // 每 500ms 檢查一次
  if (now - s_lastApproachCheck < interval)
    return false;
  s_lastApproachCheck = now;

  float speedThresh = g_cfg.enemy_approach_speed.load();
  float distThresh = g_cfg.enemy_approach_dist.load();

  std::vector<Entity> monsters;
  ScanEntities(gh, &monsters, NULL);
  if (monsters.empty())
    return false;

  bool threatFound = false;
  for (const auto &m : monsters) {
    if (m.dist > distThresh)
      continue; // 超過偵測距離

    // 在歷史中找這個 entity
    EntityPosHistory *prev = nullptr;
    for (int i = 0; i < 64; i++) {
      if (s_entityHistory[i].id == m.id) {
        prev = &s_entityHistory[i];
        break;
      }
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
    if (dt < 0.1f)
      continue; // 間隔太短不計算

    // 計算位移和速度
    float dx = m.x - prev->x;
    float dz = m.z - prev->z;
    float moveSpeed = std::sqrt(dx * dx + dz * dz) / dt;

    // 計算朝向玩家的徑向速度（負值=接近，正值=遠離）
    float prevDist = std::sqrt((prev->x - st.x) * (prev->x - st.x) +
                               (prev->z - st.z) * (prev->z - st.z));
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
    if (s_entityHistory[i].id != 0 &&
        now - s_entityHistory[i].lastSeen > 5000) {
      s_entityHistory[i].id = 0;
    }
  }

  // 威脅消失後解除鎖定（讓下次可以再觸發）
  if (!threatFound)
    s_approachThreatTriggered = false;

  if (threatFound && !s_approachThreatTriggered) {
    // 有威脅但已被處理過（本輪已觸發），不再重複觸發
    return true;
  }

  return threatFound;
}

// 前向宣告（HasNearbyPlayer 在後面定義）
static bool HasNearbyPlayer(GameHandle *gh, const PlayerState &st, float maxDist);

static bool HasNearbyPlayer(GameHandle *gh, const PlayerState &st,
                            float maxDist) {
  if (!gh || !HasUsableWorldPos(st.x, st.z))
    return false;
  PCInfo pcs[32];
  int pcCount = EnumeratePCs(gh, pcs, 32);
  for (int i = 0; i < pcCount; ++i) {
    const PCInfo &pc = pcs[i];
    if (pc.serverId == 0 || pc.serverId == 0xFFFFFFFF)
      continue;
    if (!HasUsableWorldPos(pc.x, pc.z))
      continue;
    float dist = Distance2D(st.x, st.z, pc.x, pc.z);
    if (dist < 2.0f)
      continue;
    if (dist <= maxDist)
      return true;
  }
  return false;
}

static bool DetectAntiPkThreat(GameHandle *gh, const PlayerState &st, DWORD now,
                               char *reason, size_t reasonSize) {
  if (reason && reasonSize > 0)
    reason[0] = '\0';
  if (!g_cfg.anti_pk_enable.load())
    return false;
  if (!gh || !gh->hWnd)
    return false;

  static DWORD s_lastAntiPkCheck = 0;
  if (now - s_lastAntiPkCheck < 700)
    return false;
  s_lastAntiPkCheck = now;

  static int s_lastAntiPkHp = 0;
  static DWORD s_lastAntiPkHpTime = 0;
  if (st.hp <= 0 || st.maxHp <= 0) {
    s_lastAntiPkHp = 0;
    s_lastAntiPkHpTime = now;
    return false;
  }
  if (s_lastAntiPkHp <= 0 || now - s_lastAntiPkHpTime > 10000) {
    s_lastAntiPkHp = st.hp;
    s_lastAntiPkHpTime = now;
    return false;
  }

  int hpDrop = s_lastAntiPkHp - st.hp;
  int dropThreshold = st.maxHp / 12;
  if (dropThreshold < 60)
    dropThreshold = 60;
  bool playerNearby =
      HasNearbyPlayer(gh, st, g_cfg.enemy_approach_dist.load() + 20.0f);
  s_lastAntiPkHp = st.hp;
  s_lastAntiPkHpTime = now;

  if (playerNearby && hpDrop >= dropThreshold) {
    _snprintf_s(reason, reasonSize, _TRUNCATE, "附近玩家且 HP 下降 %d", hpDrop);
    return true;
  }
  return false;
}

static bool TriggerReturnOrPause(GameHandle *gh, const PlayerState &st,
                                 DWORD now, const char *tag, const char *reason,
                                 bool pauseOnly) {
  if (!gh || !gh->hWnd)
    return false;
  if (pauseOnly) {
    Logf(tag, "★★★ %s，已暫停 Bot ★★★", reason ? reason : "觸發安全保護");
    TransitionState(BotState::PAUSED, reason ? reason : "SafetyPause", &st);
    Sleep(100);
    return true;
  }

  Logf(tag, "★★★ %s，按起點卡回城 ★★★", reason ? reason : "觸發安全回城");
  SendKeyInputFocused(g_cfg.key_waypoint_start, gh->hWnd);
  s_returnStartTime = now;
  s_returnCardSent = true;
  TransitionState(BotState::RETURNING, reason ? reason : "SafetyReturn", &st);
  Sleep(100);
  return true;
}

static bool HandleAntiPk(GameHandle *gh, const PlayerState &st, DWORD now) {
  static DWORD s_lastAntiPkTrigger = 0;
  if (now - s_lastAntiPkTrigger < 5000)
    return false;

  char reason[160] = {};
  if (!DetectAntiPkThreat(gh, st, now, reason, sizeof(reason)))
    return false;

  int cooldownSec = g_cfg.anti_pk_cooldown_sec.load();
  if (cooldownSec < 0)
    cooldownSec = 0;
  if (cooldownSec > 0) {
    s_antiPkCooldownUntil = now + (DWORD)cooldownSec * 1000UL;
  }
  s_lastAntiPkTrigger = now;
  return TriggerReturnOrPause(gh, st, now, "反PK",
                              reason[0] ? reason : "偵測到玩家威脅", false);
}

// ═══════════════ 狀態機防護系統 ════════════════
// 統一防護：所有狀態進入前都必須通過這些檢查

// 玩家狀態有效性檢查
static bool IsPlayerStateValidForGuard(const PlayerState &st) {
  if (st.maxHp <= 0 || st.maxHp > 100000000)
    return false;
  if (st.hp < 0 || st.hp > st.maxHp)
    return false;
  return true;
}

// 通用狀態防護檢查（每個狀態 tick 開頭調用）
// 返回 true = 通過，可以繼續執行；返回 false = 被阻斷，需要跳過本次執行
static bool GuardStateTick(GameHandle *gh, const PlayerState &st,
                           BotState state, DWORD now) {
  // ── 防護 1: HWND 無效 ──
  if (!gh || !gh->hWnd || !IsWindow(gh->hWnd)) {
    static DWORD s_logTime = 0;
    if (now - s_logTime > 5000) {
      Logf("防護", "❌ [%s] HWND 無效，跳過", GetStateName(state));
      s_logTime = now;
    }
    return false;
  }

  // ── 防護 2: 進程未連接 ──
  if (!gh->attached || !gh->hProcess || !gh->baseAddr) {
    static DWORD s_logTime = 0;
    if (now - s_logTime > 5000) {
      Logf("防護", "❌ [%s] 進程未連接", GetStateName(state));
      s_logTime = now;
    }
    return false;
  }

  // ── 防護 3: 玩家狀態無效（嘗試 UI 快取 fallback）──
  if (!IsPlayerStateValidForGuard(st)) {
    PlayerState cached = {};
    if (RefreshUiPlayerSnapshot(gh, &cached) &&
        IsPlayerStateValidForGuard(cached)) {
      static DWORD s_logTime = 0;
      if (now - s_logTime > 10000) {
        Logf("防護", "⚠️ [%s] 記憶體無效，使用 UI 快取: HP=%d/%d",
             GetStateName(state), cached.hp, cached.maxHp);
        s_logTime = now;
      }
      // 繼續執行（使用快取）
    } else {
      static DWORD s_logTime = 0;
      if (now - s_logTime > 5000) {
        Logf("防護", "❌ [%s] 狀態無效且無 fallback: maxHp=%d hp=%d",
             GetStateName(state), st.maxHp, st.hp);
        s_logTime = now;
      }
      return false;
    }
  }

  // ── 防護 4: HP=0 死亡檢測（所有狀態都檢測）──
  if (state != BotState::DEAD && st.hp <= 0 && st.maxHp > 0) {
    Logf("防護", "⚠️ [%s -> DEAD] HP=0 檢測到死亡", GetStateName(state));
    TransitionState(BotState::DEAD, "GuardDetectedHpZero", &st);
    return false;
  }

  // ── 防護 5: 連續讀取失敗超限 ──
  if (s_consecutiveReadFail >= 10) {
    Logf("防護", "❌ [%s] 連續讀取失敗 10 次，進入 IDLE", GetStateName(state));
    TransitionState(BotState::IDLE, "GuardReadFail10", NULL);
    s_consecutiveReadFail = 0;
    g_cfg.active.store(false);
    return false;
  }

  return true;
}

// HUNTING 專用防護（進入前檢查）
static bool GuardHuntingPreCheck(const PlayerState &st) {
  // HP 過低不能進入
  if (st.maxHp > 0) {
    int hpPct = (st.hp * 100) / st.maxHp;
    if (hpPct < g_cfg.hp_return_pct.load()) {
      Logf("防護", "❌ [HUNTING] HP 過低(%d%% < %d%%)，拒絕進入", hpPct,
           g_cfg.hp_return_pct.load());
      return false;
    }
  }

  // 已在城鎮不能進入
  if (IsTownMap(st.mapId)) {
    Logf("防護", "⚠️ [HUNTING] 已在城鎮(Map=%d)，應進入 TOWN_SUPPLY", st.mapId);
    return false;
  }

  return true;
}

void BotTick(GameHandle *gh) {
    static DWORD s_lastTickLog = 0;
    DWORD now = GetTickCount();

    // ═══════════════ g_Running 全域緊急停止 ═══════════════
    if (!g_Running) {
        if (now - s_lastTickLog > 5000) {
            Logf("Bot", "[STOP] g_Running=false，BotThread 停止運行");
            s_lastTickLog = now;
        }
        Sleep(100);
        return;
    }

    static bool s_platformLogged = false;
    if (!s_platformLogged) {
        InitPlatformDetect();
        Logf("系統", "=== StationaryCircleMode 已啟用 ===");
        s_platformLogged = true;
    }

    // ── 卡密驗證 ──
    if (!IsLicenseValid()) {
        static DWORD s_lastKamiLog = 0;
        if (now - s_lastKamiLog > 5000) {
            Logf("認證", "❌ 卡密未驗證");
            s_lastKamiLog = now;
        }
        Sleep(1000);
        return;
    }

    // ── Bot 不活躍檢查 ──
    if (!g_cfg.active.load()) {
        static DWORD s_lastInactiveLog = 0;
        if (now - s_lastInactiveLog > 3000) {
            Logf("Bot", "⚠️ Bot 不活躍");
            s_lastInactiveLog = now;
        }
        Sleep(100);
        return;
    }

    // ── HWND 驗證 ──
    HWND hWnd = gh ? gh->hWnd : NULL;
    if (!hWnd || !IsWindow(hWnd)) {
        static DWORD s_lastHwndLog = 0;
        if (now - s_lastHwndLog > 5000) {
            Logf("Bot", "❌ HWND 無效");
            s_lastHwndLog = now;
        }
        Sleep(100);
        return;
    }

    // ── StationaryCircleMode 純鍵盤戰鬥 ──
    static int s_logCounter = 0;
    if (++s_logCounter >= 500) {
        s_logCounter = 0;
        Logf("Bot", "[StationaryCircle] 運行中...");
    }

    PureCombatTick(hWnd);
    Sleep(50);
}
// ============================================================
// SupplyTick 子函數（SupplyForceReset 已於 line 68 前向宣告）
// ============================================================
static void ForceResetToBackToField(GameHandle *gh);
static void SupplyCheckGlobalTimeout(GameHandle *gh);
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
  s_phase4EscSent = false;
  Logf("狀態機", "[SupplyForceReset] 所有靜態變數已重置");
}
// DWORD 繞環安全比較：now 是否已超過 deadline（處理 GetTickCount 49.7天繞環）
static inline bool IsTickExpired(DWORD now, DWORD deadline) {
  return (now - deadline) < 0x80000000;
}
static void SupplyCheckGlobalTimeout(GameHandle *gh) {
  (void)gh;
  DWORD now = GetTickCount();
  if (s_globalTimeout > 0 && IsTickExpired(now, s_globalTimeout)) {
    Logf("狀態機", "★★★ [TOWN_SUPPLY] 全域超時 45 秒！強制返回野外 ★★★");
    SupplyForceReset();
    TransitionState(BotState::BACK_TO_FIELD, "SupplyGlobalTimeout45s", NULL);
  }
}
static void SupplyOnEnterPhase(int phase) {
  switch (phase) {
  case 0:
    Logf("狀態機", "[Phase 0] 走向 NPC 並開啟對話");
    break;
  case 1:
    Logf("狀態機", "[Phase 1] 發送 SPACE 開商店");
    break;
  case 2:
    Logf("狀態機", "[Phase 2] 自動賣物");
    break;
  case 3:
    Logf("狀態機", "[Phase 3] 自動買藥");
    s_buySubPhase = 0;
    break;
  case 4:
    Logf("狀態機", "[Phase 4] 關閉商店 → 返回野外");
    break;
  }
}
static void SupplyOnExitPhase(int phase) { (void)phase; }
static void ForceResetToBackToField(GameHandle *gh) {
  if (g_State.load() == (int)BotState::BACK_TO_FIELD && s_backToFieldCardSent) {
    s_backToFieldStartTime = GetTickCount();
    return;
  }
  TransitionState(BotState::BACK_TO_FIELD, "ForceResetToBackToField", NULL);
  s_backToFieldCardSent = 0;
  Logf("狀態機", "→ 已強制切換到 BACK_TO_FIELD");
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
void SupplyTick(GameHandle *gh) {
  if (!gh || !gh->attached || !gh->hWnd) {
    static DWORD s_lastNoGhLog = 0;
    DWORD now = GetTickCount();
    if (now - s_lastNoGhLog > 3000) {
      Logf("狀態機", "⚠️ [SupplyTick] gh 未就緒，跳過");
      s_lastNoGhLog = now;
    }
    return;
  }

  if (s_reentryGuard) {
    static DWORD s_lastReentryLog = 0;
    DWORD now = GetTickCount();
    if (now - s_lastReentryLog > 3000) {
      Logf("狀態機", "⚠️ [SupplyTick] 偵測到重入，略過本次 tick");
      s_lastReentryLog = now;
    }
    return;
  }

  ReentryGuard guard(s_reentryGuard); // RAII 自動防重入

  HWND hWnd = gh->hWnd;
  DWORD now = GetTickCount();

  // ── 前景焦點包裝巨集──────────────────────────────
#define FOCUS_CLICK(x, y)                                                      \
  do {                                                                         \
    FocusGameWindow(hWnd);                                                     \
    ClickAtDirect(hWnd, x, y);                                                 \
  } while (0)
#define FOCUS_RCLICK(x, y)                                                     \
  do {                                                                         \
    FocusGameWindow(hWnd);                                                     \
    RClickAtDirect(hWnd, x, y);                                                \
  } while (0)

  // ── BACK_TO_FIELD 分支─────────────────────────────
  if (g_State.load() == (int)BotState::BACK_TO_FIELD) {
    if (!s_backToFieldCardSent) {
      FocusGameWindow(hWnd);
      SendKeyInputFocused(g_cfg.key_waypoint_end, hWnd);
      s_backToFieldCardSent = now;
      s_backToFieldStartTime = now;
      Logf("狀態機", "[BACK_TO_FIELD] 發送前點卡");
    }
    DWORD minDelay = (DWORD)g_cfg.teleport_delay_ms.load();
    if (minDelay < 8000)
      minDelay = 8000;
    DWORD elapsed = now - s_backToFieldStartTime;

    // Bug 1 修復：15秒超時保護，防止 NPC 對話或傳送延遲卡死
    DWORD hardTimeout = 15000;
    if (elapsed > hardTimeout) {
      Logf("狀態機", "[BACK_TO_FIELD] 超時（%dms > %dms），強制回到 HUNTING",
           elapsed, hardTimeout);
      SupplyForceReset();
      TransitionState(BotState::HUNTING, "BackToFieldTimeout", NULL);
      return;
    }

    if (elapsed >= minDelay) {
      PlayerState fieldState;
      char confirmReason[160] = {0};
      if (ReadPlayerStateDetailed(gh, &fieldState, confirmReason,
                                  sizeof(confirmReason)) ==
              PlayerStateReadStatus::OK &&
          ConfirmBackToFieldArrival(fieldState, confirmReason,
                                    sizeof(confirmReason))) {
        Logf("狀態機", "[BACK_TO_FIELD → HUNTING] 傳送確認成功（%dms）: %s",
             elapsed, confirmReason);
        SupplyForceReset();
        TransitionState(BotState::HUNTING, confirmReason, &fieldState);
        return;
      }

      DWORD confirmTimeout = GetTransitionConfirmTimeoutMs(minDelay, 16000);
      if (elapsed >= confirmTimeout) {
        Logf("狀態機", "❌ [BACK_TO_FIELD] 傳送確認超時（%dms）: %s", elapsed,
             confirmReason[0] ? confirmReason : "讀值仍未有效");
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
    Logf("狀態機", "★★★ [TOWN_SUPPLY] 全域超時 90 秒！強制返回野外 ★★★");
    SupplyForceReset();
    TransitionState(BotState::BACK_TO_FIELD, "SupplyGlobalTimeout90s", NULL);
    s_backToFieldCardSent = 0;
    return;
  }

  if (!s_supplyEntered) {
    PlayerState supplyState;
    char supplyReason[160] = {0};
    PlayerStateReadStatus supplyRead = ReadPlayerStateDetailed(
        gh, &supplyState, supplyReason, sizeof(supplyReason));
    if (supplyRead != PlayerStateReadStatus::OK) {
      Logf("狀態機", "❌ [TOWN_SUPPLY] 進入前讀值無效: %s", supplyReason);
      TransitionState(BotState::IDLE, "SupplyEntryReadInvalid", NULL);
      g_cfg.active.store(false);
      return;
    }
    if (!IsTownMap(supplyState.mapId)) {
      char confirmReason[160] = {0};
      if (!ConfirmReturnArrival(supplyState, confirmReason,
                                sizeof(confirmReason))) {
        Logf("狀態機", "❌ [TOWN_SUPPLY] 尚未確認到城，拒絕進入補給: %s",
             confirmReason);
        TransitionState(BotState::IDLE, "SupplyEntryNotConfirmed",
                        &supplyState);
        g_cfg.active.store(false);
        return;
      }
    }
    Logf("狀態機", "=== [TOWN_SUPPLY] 進入聖門補給狀態，全域重置 ===");
    SupplyForceReset();
    s_supplyEntered = true;
    s_globalTimeout = now + 90000; // 90秒超時
    s_lastSupplyPhase = -1;
    s_phase0RClickTime = 0;
  }

  DWORD phaseElapsed =
      (s_supplyPhaseStart == 0) ? 0 : (now - s_supplyPhaseStart);

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
      FOCUS_CLICK(577, 140); // NPC coordinates
      Logf("狀態機", "[Phase 0] Step0: 左鍵點擊 NPC");
      s_phase0SubStep = 1;
      s_phase0RClickTime = now;
      Sleep(1200);
      return;
    }

    // Step 1: 右鍵點擊 NPC（等待 4.5 秒後）
    if (s_phase0SubStep == 1) {
      if (now - s_phase0RClickTime > 4500) {
        FOCUS_RCLICK(577, 140); // NPC coordinates
        Logf("狀態機", "[Phase 0] Step1: 右鍵點擊 NPC @ %dms",
             now - s_phase0RClickTime);
        s_phase0SubStep = 2;
        s_phase0RClickTime = now;
        Sleep(800);
      }
      return;
    }

    // Step 2: 點擊「購買物品」
    if (s_phase0SubStep == 2) {
      if (now - s_phase0RClickTime > 3500) {
        FOCUS_CLICK(440, 390); // Buy button coordinates
        Logf("狀態機", "[Phase 0] Step2: 點擊購買物品 @ %dms",
             now - s_phase0RClickTime);
        s_phase0SubStep = 3;
        s_phase0RClickTime = now;
        Sleep(1000);
      }
      return;
    }

    // Step 3: 等待商店開啟
    if (s_phase0SubStep == 3) {
      if (now - s_phase0RClickTime > 6000) {
        Logf("狀態機", "[Phase 0 → Phase 1] 商店應已開啟 @ %dms",
             now - s_phase0RClickTime);
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
      Logf("狀態機", "[Phase 1] 開始 - 發送 SPACE");
    }

    FocusGameWindow(hWnd);
    SendKeyInputFocused(VK_SPACE, hWnd);
    Logf("狀態機", "[Phase 1] 已發送 SPACE (已耗時 %dms)", phaseElapsed);

    if (phaseElapsed > 6500) {
      Logf("狀態機", "[Phase 1 → Phase 2] 商店已就緒");
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
      s_sellSubPhase = 0;  // Bug 1 Fix: 重置 SubStep
      Logf("狀態機", "[Phase 2] 開始 - 賣物");
    }

    // Bug 1 Fix: SubStep 流程
    // SubStep 0: 點擊賣物分頁
    // SubStep 1: 掃描背包，點擊賣物按鈕
    // SubStep 2: 點擊確認按鈕

    if (s_sellSubPhase == 0) {
      // 點擊賣物分頁（使用 SupplyCoords 設定）
      SupplyCoords* coords = GetSupplyCoords();
      if (coords->sellTabX > 0 && coords->sellTabY > 0) {
        ClickAtDirect(gh->hWnd, coords->sellTabX, coords->sellTabY);
        Logf("狀態機", "[Phase 2] 點擊賣物分頁 (%d,%d)", coords->sellTabX, coords->sellTabY);
      }
      Sleep(500);
      s_sellSubPhase = 1;
    }

    if (s_sellSubPhase == 1 && !s_sellSessionActive) {
      // 掃描背包，過濾保護物品
      std::vector<InvSlot> slots;
      if (ScanInventory(gh, &slots) > 0) {
        BotConfig *cfg = GetBotConfig();
        EnterCriticalSection(&cfg->cs_protected);
        for (auto it = slots.begin(); it != slots.end();) {
          bool isProtected = false;
          if (it->itemId == 0 || it->itemId == 0xFFFFFFFF) {
            isProtected = true;
          } else {
            for (size_t i = 0; i < cfg->protected_item_ids.size(); i++) {
              DWORD protId = (DWORD)cfg->protected_item_ids[i];
              if (protId != 0 && it->itemId == protId) {
                isProtected = true;
                break;
              }
            }
            if (!isProtected) {
              int row = it->slotIdx / 6;
              if (row >= 0 && row < BotConfig::MAX_INVENTORY_ROWS) {
                if (cfg->protected_rows[row])
                  isProtected = true;
              }
            }
          }
          if (isProtected)
            it = slots.erase(it);
          else
            ++it;
        }
        LeaveCriticalSection(&cfg->cs_protected);

        if (!slots.empty()) {
          Logf("賣物", "[Phase 2] 找到 %d 個可賣物品，點擊賣物資",
               (int)slots.size());
          ClickAtDirect(gh->hWnd, Coords::GetNPCSellItemPos().x,
                        Coords::GetNPCSellItemPos().z);
          Sleep(800);
          s_sellSessionActive = true;
          s_lastSellAction = now;
          s_sellSubPhase = 2;  // Bug 1 Fix: 進入確認 SubStep
        } else {
          Logf("狀態機", "[Phase 2] 沒有可賣物品 → Phase 3");
          s_sellSessionActive = false;
          SupplyOnExitPhase(2);
          s_supplyPhase = 3;
          s_supplyPhaseStart = now;
          return;
        }
      } else {
        // ScanInventory 返回 0（背包讀取失敗或背包空的）→ 直接進 Phase 3
        Logf("賣物", "[Phase 2] 背包掃描返回 0 → 跳過賣物，直接 Phase 3");
        s_supplyPhase = 3;
        s_supplyPhaseStart = now;
        return;
      }
    } else if (s_sellSubPhase == 2) {
      // Bug 1 Fix: SubStep 2 - 點擊確認按鈕
      DWORD sellElapsed = now - s_lastSellAction;
      if (sellElapsed > 5000) {
        Logf("狀態機", "[Phase 2] 賣物超時 → Phase 3");
        s_sellSessionActive = false;
        s_sellSubPhase = 0;
        SupplyOnExitPhase(2);
        s_supplyPhase = 3;
        s_supplyPhaseStart = now;
        return;
      }
      if (sellElapsed > 1200) {
        Logf("賣物", "[Phase 2] SubStep 2 - 點擊確認");
        ClickAtDirect(gh->hWnd, Coords::GetNPCSellConfirmPos().x,
                      Coords::GetNPCSellConfirmPos().z);
        s_lastSellAction = now;
        Sleep(500);
        // 完成確認，進入下一階段
        s_sellSessionActive = false;
        s_sellSubPhase = 0;
        SupplyOnExitPhase(2);
        s_supplyPhase = 3;
        s_supplyPhaseStart = now;
        return;
      }
    }

    if (phaseElapsed > 25000) {
      Logf("狀態機", "[Phase 2] 超時 25s → Phase 3");
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
      Logf("狀態機", "[Phase 3] 開始 - 買藥");
    }

    BotConfig *cfg = GetBotConfig();
    if (!cfg->auto_buy.load()) {
      Logf("狀態機", "[Phase 3] auto_buy=OFF → Phase 4");
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
    int remaining =
        (hpQty > 0 ? 1 : 0) + (mpQty > 0 ? 1 : 0) + (spQty > 0 ? 1 : 0);

    if (remaining == 0) {
      Logf("狀態機", "[Phase 3] 購買數量為 0 → Phase 4");
      SupplyOnExitPhase(3);
      s_supplyPhase = 4;
      s_supplyPhaseStart = now;
      return;
    }

    // SubPhase 0: 點擊「消耗品」分頁 (699, 253)
    if (s_buySubPhase == 0) {
      Logf("狀態機", "[Phase 3] SubPhase0: 點擊消耗品分頁");
      FOCUS_CLICK(699, 253); // Consume tab coordinates
      Sleep(1000);
      s_buySubPhase = 1;
      return;
    }

    // SubPhase 1-3: 依序買 HP / MP / SP
    int buyIdx = s_buySubPhase - 1; // 0=HP, 1=MP, 2=SP
    if (buyIdx >= 0 && buyIdx < 3) {
      int *qtyArr[3] = {&hpQty, &mpQty, &spQty};
      const char *itemNames[3] = {"HP", "MP", "SP"};
      const int itemXs[3] = {320, 360, 400}; // HP, MP, SP X coordinates
      const int itemYs[3] = {280, 280, 280}; // HP, MP, SP Y coordinates

      int qty = *qtyArr[buyIdx];
      if (qty > 0) {
        Logf("狀態機", "[Phase 3] SubPhase%d: 購買 %s x%d", s_buySubPhase,
             itemNames[buyIdx], qty);

        // 1) 點擊商品
        FOCUS_CLICK(itemXs[buyIdx], itemYs[buyIdx]);
        Sleep(600);

        // 2) 點擊數量框
        FOCUS_CLICK(520, 420); // Quantity input box coordinates
        Sleep(300);

        // 3) Ctrl+A 全選 → 輸入數量
        SendCtrlA(hWnd);
        Sleep(100);
        TypeNumber(hWnd, qty);
        Sleep(400);

        // 4) 點擊確認
        FOCUS_CLICK(500, 450); // Buy confirm button coordinates
        Sleep(1500);

        Logf("狀態機", "[Phase 3] %s 購買完成", itemNames[buyIdx]);
      }

      s_buySubPhase++;
      if (s_buySubPhase > 3) {
        Logf("狀態機", "[Phase 3] 全部購買完成 → Phase 4");
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
      Logf("狀態機", "[Phase 4] 開始 - 關閉商店");
    }

    if (!s_phase4EscSent) {
      Logf("狀態機", "[Phase 4] 發送 ESC × 2 → 前點卡");
      SendKeyInputFocused(VK_ESCAPE, hWnd);
      SleepJitter(600);
      SendKeyInputFocused(VK_ESCAPE, hWnd);
      SleepJitter(600);
      s_phase4EscSent = true;
    }

    if (s_antiPkCooldownUntil != 0) {
      if (!IsTickExpired(now, s_antiPkCooldownUntil)) {
        if (now - s_lastAntiPkCooldownLog > 5000) {
          DWORD remain = (s_antiPkCooldownUntil - now + 999) / 1000;
          Logf("反PK", "[冷卻] 暫留城鎮 %lu 秒後再返回野外",
               (unsigned long)remain);
          s_lastAntiPkCooldownLog = now;
        }
        return;
      }
      Logf("反PK", "[冷卻結束] 返回野外");
      s_antiPkCooldownUntil = 0;
    }

    // 重置並切回 BACK_TO_FIELD
    SupplyForceReset();
    TransitionState(BotState::BACK_TO_FIELD, "SupplyPhase4Complete", NULL);
    s_backToFieldCardSent = 0;
  }
}

// ============================================================
// Recovery / shared state helpers
// ============================================================
void RequestRecovery(const char *reason) {
  Logf("Recovery", reason);
  // Trigger recovery state if not already in it
  if (GetBotState() != BotState::RECOVERY) {
    SetBotState(BotState::RECOVERY);
  }
}

bool IsTownMap(int mapId) {
  BotConfig *cfg = GetBotConfig();
  for (int id : cfg->townMapIds) {
    if (mapId == id)
      return true;
  }
  return false;
}

// ═══════════════ 物資座標設定（持久化）═══════════════
static SupplyCoords s_supplyCoords = {};

SupplyCoords* GetSupplyCoords() {
  return &s_supplyCoords;
}

static const char* INI_PATH = ".\\offsets.ini";

bool SaveCoordSettings(const SupplyCoords* cfg) {
  if (!cfg) return false;
  char section[] = "SupplyCoords";
  WritePrivateProfileStringA(section, "npcLeftX",
      std::to_string(cfg->npcLeftX).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "npcLeftY",
      std::to_string(cfg->npcLeftY).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "buyBtnX",
      std::to_string(cfg->buyBtnX).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "buyBtnY",
      std::to_string(cfg->buyBtnY).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "sellBtnX",
      std::to_string(cfg->sellBtnX).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "sellBtnY",
      std::to_string(cfg->sellBtnY).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "sellTabX",
      std::to_string(cfg->sellTabX).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "sellTabY",
      std::to_string(cfg->sellTabY).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "hpItemX",
      std::to_string(cfg->hpItemX).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "hpItemY",
      std::to_string(cfg->hpItemY).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "mpItemX",
      std::to_string(cfg->mpItemX).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "mpItemY",
      std::to_string(cfg->mpItemY).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "qtyInputX",
      std::to_string(cfg->qtyInputX).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "qtyInputY",
      std::to_string(cfg->qtyInputY).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "buyConfirmX",
      std::to_string(cfg->buyConfirmX).c_str(), INI_PATH);
  WritePrivateProfileStringA(section, "buyConfirmY",
      std::to_string(cfg->buyConfirmY).c_str(), INI_PATH);
  return true;
}

bool LoadCoordSettings(SupplyCoords* cfg) {
  if (!cfg) return false;
  char section[] = "SupplyCoords";
  *cfg = SupplyCoords{}; // reset to defaults first
  cfg->npcLeftX = GetPrivateProfileIntA(section, "npcLeftX", 577, INI_PATH);
  cfg->npcLeftY = GetPrivateProfileIntA(section, "npcLeftY", 140, INI_PATH);
  cfg->buyBtnX = GetPrivateProfileIntA(section, "buyBtnX", 440, INI_PATH);
  cfg->buyBtnY = GetPrivateProfileIntA(section, "buyBtnY", 390, INI_PATH);
  cfg->sellBtnX = GetPrivateProfileIntA(section, "sellBtnX", 780, INI_PATH);
  cfg->sellBtnY = GetPrivateProfileIntA(section, "sellBtnY", 300, INI_PATH);
  cfg->sellTabX = GetPrivateProfileIntA(section, "sellTabX", 0, INI_PATH);
  cfg->sellTabY = GetPrivateProfileIntA(section, "sellTabY", 0, INI_PATH);
  cfg->hpItemX = GetPrivateProfileIntA(section, "hpItemX", 320, INI_PATH);
  cfg->hpItemY = GetPrivateProfileIntA(section, "hpItemY", 280, INI_PATH);
  cfg->mpItemX = GetPrivateProfileIntA(section, "mpItemX", 360, INI_PATH);
  cfg->mpItemY = GetPrivateProfileIntA(section, "mpItemY", 280, INI_PATH);
  cfg->qtyInputX = GetPrivateProfileIntA(section, "qtyInputX", 520, INI_PATH);
  cfg->qtyInputY = GetPrivateProfileIntA(section, "qtyInputY", 420, INI_PATH);
  cfg->buyConfirmX = GetPrivateProfileIntA(section, "buyConfirmX", 500, INI_PATH);
  cfg->buyConfirmY = GetPrivateProfileIntA(section, "buyConfirmY", 450, INI_PATH);
  return true;
}
