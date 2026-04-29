// ============================================================
// recovery_vision.h
// 增強版 Recovery 系統 - 結合視覺識別
// 當記憶體偏移失效時，進入 Recovery 狀態，使用視覺確認玩家狀態
// ============================================================

#pragma once
#ifndef _RECOVERY_VISION_H_
#define _RECOVERY_VISION_H_

#include "bot_logic.h"
#include <functional>

// ============================================================
// 視覺識別結果
// ============================================================
namespace VisionRecovery {

// 玩家安全狀態（從視覺識別推斷）
enum class SafeState {
    UNKNOWN = 0,      // 無法判斷
    IN_TOWN = 1,      // 在城鎮（看到城鎮 UI）
    IN_FIELD = 2,     // 在野外（看到怪物/草地等）
    IN_DUNGEON = 3,   // 在地城
    IN_COMBAT = 4,    // 戰鬥中
    LOADING = 5,      // 載入中
    STUCK = 99        // 卡住/無法識別
};

// 玩家狀態識別結果
struct VisionCheckResult {
    SafeState safe_state = SafeState::UNKNOWN;
    float confidence = 0.0f;
    bool town_detected = false;
    bool monster_detected = false;
    bool hp_bar_visible = false;
    bool mp_bar_visible = false;
    bool inventory_full = false;
    std::string description;
};

// 記憶體失效原因
enum class MemoryFailureReason {
    NONE = 0,
    MAP_ID_INVALID = 1,         // MapID = 0 或無效
    INVENTORY_BASE_INVALID = 2, // 背包基底無效
    CHARACTER_PTR_INVALID = 3,   // 角色指標無效
    POSITION_INVALID = 4,       // 座標無效
    HP_MP_INVALID = 5,          // HP/MP 無效
    ALL_ATTRIBUTES_INVALID = 6, // 所有屬性讀取失敗
    BASE_ADDR_ZERO = 7          // baseAddr = 0
};

// Recovery 配置
struct RecoveryConfig {
    int max_attempts = 5;           // 最大嘗試次數
    DWORD vision_timeout_ms = 15000; // 視覺識別超時
    DWORD memory_retry_ms = 3000;     // 記憶體重試間隔
    bool use_vision = false;         // 預設關閉視覺識別（避免截圖問題）
};

// ============================================================
// 視覺識別 API
// ============================================================

// 初始化視覺識別系統
bool InitVisionSystem(HWND hWnd);

// 本地 CAPTCHA 檢測（無截圖）
bool CheckCaptchaLocal();

// 截圖並分析當前畫面
VisionCheckResult AnalyzeCurrentScreen();

// 檢測是否在城鎮
bool IsInTown();

// 檢測是否有威脅（怪物）
bool HasNearbyThreat();

// 等待畫面穩定
bool WaitForStableScreen(DWORD timeoutMs);

// 獲取當前記憶體失敗原因
MemoryFailureReason GetLastMemoryFailure();

// 設置記憶體失敗原因（由 bot_logic 調用）
void SetMemoryFailureReason(MemoryFailureReason reason);

// 設置配置
void SetConfig(const RecoveryConfig& config);
RecoveryConfig GetConfig();

// 重置嘗試次數
void ResetAttempts();

// 獲取嘗試次數
int GetAttempts();

// 進入 Recovery 狀態
void EnterRecovery(const char* reason);

// Recovery Tick（由 BotTick 調用）
void RecoveryTick(HWND hWnd);

// 是否在 Recovery 狀態
bool IsInRecoveryState();

// 是否應該返回正常狀態
bool ShouldReturnToNormal();

// 離開 Recovery 狀態，返回 IDLE
void ExitRecovery(const char* reason);

} // namespace VisionRecovery

#endif // _RECOVERY_VISION_H_
