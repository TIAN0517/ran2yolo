// ============================================================
// recovery.cpp
// VLM-Driven Recovery System for Bot FSM
// (Blind mode: No screenshot, no VLM - pure ESC + random click)
// ============================================================

#include "bot_logic.h"
#include "../game/game_process.h"
#include "../input/input_sender.h"
#include <cstdarg>
#include <ctime>

// ============================================================
// VLM Blocking Issue & Action Enums (定義於此，與 recovery_vision.cpp 共用)
// ============================================================
namespace RecoverySystem {
    enum class VLMBlockingIssue {
        Unknown = 0,
        Popup,
        Terrain,
        Dead,
        InventoryFull,
        NpcDialog,
        ShopDialog,
        Loading
    };

    enum class VLMSuggestedAction {
        NoAction = 0,
        PressESCx3MoveRandom,
        ReturnToTown,
        WaitForLoading,
        AcceptDialog,
        CloseShop,
        Resurrect,
        ClearInventory
    };
}

// ============================================================
// 內部狀態
// ============================================================
namespace RecoverySystem {
    // Recovery 階段
    enum class Phase {
        INIT = 0,           // 截圖 + VLM 分析
        EXECUTING,          // 執行恢復動作
        VERIFYING,          // 確認畫面乾淨
        DONE,               // 恢復完成，返回 IDLE
        ABORT               // 恢復失敗，進入緊急停機
    };

    static Phase s_phase = Phase::INIT;
    static int s_attempts = 0;
    static const int MAX_RECOVERY_ATTEMPTS = 5;

    static VLMBlockingIssue s_lastIssue = VLMBlockingIssue::Unknown;
    static VLMSuggestedAction s_lastAction = VLMSuggestedAction::NoAction;
    static DWORD s_phaseStartTime = 0;
    static DWORD s_lastVLMLog = 0;
    static bool s_initialized = false;

    // 隨機地板點擊位置（避免重複點同一位置）
    static const int RANDOM_CLICK_POINTS = 8;
    static const struct Point { int rx, ry; } s_randomFloorPoints[RANDOM_CLICK_POINTS] = {
        {45, 60}, {55, 60}, {50, 55}, {48, 65},  // 中心偏上
        {45, 55}, {55, 55}, {50, 50}, {50, 70}   // 擴散範圍
    };
    static int s_floorClickIndex = 0;

    // ESC 按鍵計數（盲按 3 次）
    static int s_escPressCount = 0;
    static const int ESC_PRESS_TOTAL = 3;

    // 延遲控制
    static const DWORD ESC_INTERVAL_MS = 300;
    static const DWORD ACTION_WAIT_MS = 800;
    static const DWORD VERIFY_WAIT_MS = 1500;
    static const DWORD VLM_TIMEOUT_MS = 12000;  // VLM 回應超時

    // ============================================================
    // 輔助函式
    // ============================================================
    inline DWORD GetNowMs() { return GetTickCount(); }

    void LogRecovery(const char* fmt, ...) {
        DWORD now = GetNowMs();
        if (now - s_lastVLMLog < 500) return;  // 防刷屏
        s_lastVLMLog = now;

        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        printf("[Recovery] %s\n", buf);
        UIAddLog("[Recovery] %s", buf);
    }

    // 盲按 ESC（使用 SendInput 後台）
    void BlindPressESC() {
        if (s_escPressCount >= ESC_PRESS_TOTAL) return;

        INPUT esc_down = {};
        esc_down.type = INPUT_KEYBOARD;
        esc_down.ki.wVk = VK_ESCAPE;
        esc_down.ki.dwFlags = 0;

        INPUT esc_up = {};
        esc_up.type = INPUT_KEYBOARD;
        esc_up.ki.wVk = VK_ESCAPE;
        esc_up.ki.dwFlags = KEYEVENTF_KEYUP;

        INPUT inputs[2] = { esc_down, esc_up };
        UINT sent = SendInput(2, inputs, sizeof(INPUT));

        s_escPressCount++;
        LogRecovery("盲按 ESC #%d/%d (sent=%u)", s_escPressCount, ESC_PRESS_TOTAL, sent);

        Sleep(ESC_INTERVAL_MS + (rand() % 100));
    }

    // 隨機點擊地板（脫困用）
    void RandomFloorClick(HWND hWnd) {
        int idx = s_floorClickIndex % RANDOM_CLICK_POINTS;
        int rx = s_randomFloorPoints[idx].rx + (rand() % 11 - 5);  // ±5 隨機偏移
        int ry = s_randomFloorPoints[idx].ry + (rand() % 11 - 5);

        s_floorClickIndex++;

        // 使用 SendMessage 點擊（後台模式）
        LPARAM lp = MAKELPARAM(rx, ry);
        SendMessage(hWnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
        Sleep(50 + rand() % 50);
        SendMessage(hWnd, WM_LBUTTONUP, 0, lp);

        LogRecovery("隨機點擊地板 (%d%%, %d%%) [idx=%d]", rx, ry, idx);
    }

    // 執行回城腳本
    void ExecuteReturnToTown(HWND hWnd) {
        BotConfig* cfg = GetBotConfig();
        BYTE waypointKey = cfg->key_waypoint_start.load();

        LogRecovery("執行回城腳本 (key=0x%02X)", waypointKey);

        // 按下起點卡
        keybd_event(waypointKey, 0, 0, 0);
        Sleep(100 + rand() % 100);
        keybd_event(waypointKey, 0, KEYEVENTF_KEYUP, 0);

        Sleep(ACTION_WAIT_MS);
    }

    // 等待載入完成
    void WaitForLoading() {
        LogRecovery("等待載入完成...");
        Sleep(5000 + rand() % 2000);  // 5-7 秒
    }

    // 接受對話框
    void AcceptDialog(HWND hWnd) {
        LogRecovery("接受對話框");

        // 按下 Enter
        keybd_event(VK_RETURN, 0, 0, 0);
        Sleep(50);
        keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);

        Sleep(ACTION_WAIT_MS);
    }

    // 關閉商店
    void CloseShop(HWND hWnd) {
        LogRecovery("關閉商店");
        BlindPressESC();  // ESC 通常可以關閉商店
        Sleep(ACTION_WAIT_MS);
    }

    // 復活
    void ExecuteResurrect(HWND hWnd) {
        LogRecovery("執行復活");
        // 點擊畫面中央（復活按鈕通常在中央）
        LPARAM center = MAKELPARAM(50, 50);  // 50%, 50%
        SendMessage(hWnd, WM_LBUTTONDOWN, MK_LBUTTON, center);
        Sleep(100);
        SendMessage(hWnd, WM_LBUTTONUP, 0, center);
        Sleep(ACTION_WAIT_MS);
    }

    // 解析 VLM 回應
    VLMBlockingIssue ParseBlockingIssue(const char* str) {
        if (!str || !str[0]) return VLMBlockingIssue::Unknown;
        if (strcmp(str, "Popup") == 0) return VLMBlockingIssue::Popup;
        if (strcmp(str, "Terrain") == 0) return VLMBlockingIssue::Terrain;
        if (strcmp(str, "Dead") == 0) return VLMBlockingIssue::Dead;
        if (strcmp(str, "InventoryFull") == 0) return VLMBlockingIssue::InventoryFull;
        if (strcmp(str, "NpcDialog") == 0) return VLMBlockingIssue::NpcDialog;
        if (strcmp(str, "ShopDialog") == 0) return VLMBlockingIssue::ShopDialog;
        if (strcmp(str, "Loading") == 0) return VLMBlockingIssue::Loading;
        return VLMBlockingIssue::Unknown;
    }

    VLMSuggestedAction ParseSuggestedAction(const char* str) {
        if (!str || !str[0]) return VLMSuggestedAction::NoAction;
        if (strcmp(str, "Press_ESC_x3_MoveRandom") == 0) return VLMSuggestedAction::PressESCx3MoveRandom;
        if (strcmp(str, "ReturnToTown") == 0) return VLMSuggestedAction::ReturnToTown;
        if (strcmp(str, "WaitForLoading") == 0) return VLMSuggestedAction::WaitForLoading;
        if (strcmp(str, "AcceptDialog") == 0) return VLMSuggestedAction::AcceptDialog;
        if (strcmp(str, "CloseShop") == 0) return VLMSuggestedAction::CloseShop;
        if (strcmp(str, "Resurrect") == 0) return VLMSuggestedAction::Resurrect;
        if (strcmp(str, "ClearInventory") == 0) return VLMSuggestedAction::ClearInventory;
        return VLMSuggestedAction::NoAction;
    }

    // 根據 issue 和 action 執行對應的恢復動作
    void ExecuteRecoveryAction(GameHandle* gh, VLMBlockingIssue issue, VLMSuggestedAction action) {
        if (!gh || !gh->hWnd) return;

        s_lastIssue = issue;
        s_lastAction = action;

        switch (action) {
        case VLMSuggestedAction::PressESCx3MoveRandom:
            // 盲按 ESC 三次 + 隨機點擊地板
            if (s_escPressCount < ESC_PRESS_TOTAL) {
                BlindPressESC();
            } else {
                RandomFloorClick(gh->hWnd);
                s_escPressCount = 0;  // 重置計數
                s_phase = Phase::VERIFYING;
                s_phaseStartTime = GetNowMs();
            }
            break;

        case VLMSuggestedAction::ReturnToTown:
            ExecuteReturnToTown(gh->hWnd);
            s_phase = Phase::VERIFYING;
            s_phaseStartTime = GetNowMs();
            break;

        case VLMSuggestedAction::WaitForLoading:
            WaitForLoading();
            s_phase = Phase::VERIFYING;
            s_phaseStartTime = GetNowMs();
            break;

        case VLMSuggestedAction::AcceptDialog:
            AcceptDialog(gh->hWnd);
            s_phase = Phase::VERIFYING;
            s_phaseStartTime = GetNowMs();
            break;

        case VLMSuggestedAction::CloseShop:
            CloseShop(gh->hWnd);
            s_phase = Phase::VERIFYING;
            s_phaseStartTime = GetNowMs();
            break;

        case VLMSuggestedAction::Resurrect:
            ExecuteResurrect(gh->hWnd);
            s_phase = Phase::VERIFYING;
            s_phaseStartTime = GetNowMs();
            break;

        case VLMSuggestedAction::ClearInventory:
            // 等同於回城
            ExecuteReturnToTown(gh->hWnd);
            s_phase = Phase::VERIFYING;
            s_phaseStartTime = GetNowMs();
            break;

        case VLMSuggestedAction::NoAction:
        default:
            // NoAction 或 Unknown → 直接驗證
            s_phase = Phase::VERIFYING;
            s_phaseStartTime = GetNowMs();
            break;
        }
    }
}  // end namespace RecoverySystem

// ============================================================
// 公開 API 實作
// ============================================================

void InitRecoverySystem() {
    RecoverySystem::s_phase = RecoverySystem::Phase::INIT;
    RecoverySystem::s_attempts = 0;
    RecoverySystem::s_escPressCount = 0;
    RecoverySystem::s_lastIssue = RecoverySystem::VLMBlockingIssue::Unknown;
    RecoverySystem::s_lastAction = RecoverySystem::VLMSuggestedAction::NoAction;
    RecoverySystem::s_floorClickIndex = 0;
    RecoverySystem::s_initialized = true;

    RecoverySystem::LogRecovery("Recovery 系統已初始化 (盲操作模式)");
}

bool IsInRecoveryState() {
    return GetBotState() == BotState::RECOVERY;
}

int GetRecoveryAttempts() {
    return RecoverySystem::s_attempts;
}

void ResetRecoveryAttempts() {
    RecoverySystem::s_attempts = 0;
    RecoverySystem::s_phase = RecoverySystem::Phase::INIT;
}

void RecoveryTick(GameHandle* gh) {
    using namespace RecoverySystem;

    if (!gh || !gh->hWnd) {
        LogRecovery("ERROR: GameHandle 無效");
        s_phase = Phase::ABORT;
        return;
    }

    // 第一次進入 Recovery，重置狀態
    if (s_phase == Phase::INIT) {
        s_phaseStartTime = GetNowMs();
        s_escPressCount = 0;
        s_floorClickIndex = 0;
    }

    switch (s_phase) {
    case Phase::INIT: {
        // Phase 0: 直接執行盲操作脫困（關閉截圖以節省效能）
        LogRecovery("=== Recovery 嘗試 #%d/%d (盲操作模式) ===", s_attempts + 1, MAX_RECOVERY_ATTEMPTS);

        // 預設盲操作：按 ESC 3 次 + 隨機點擊地板
        VLMBlockingIssue issue = VLMBlockingIssue::Terrain;
        VLMSuggestedAction action = VLMSuggestedAction::PressESCx3MoveRandom;

        LogRecovery("盲操作: 按 ESC 3次 + 隨機點擊地板");
        ExecuteRecoveryAction(gh, issue, action);
        break;
    }

    case Phase::EXECUTING: {
        // Phase 1: 執行恢復動作
        DWORD elapsed = GetNowMs() - s_phaseStartTime;
        if (elapsed < 500) break;  // 防抖

        // 根據上次記錄的 action 繼續執行
        if (s_lastAction == VLMSuggestedAction::PressESCx3MoveRandom) {
            ExecuteRecoveryAction(gh, s_lastIssue, s_lastAction);
        } else {
            // 其他 action 在 INIT 階段已完成
            s_phase = Phase::VERIFYING;
            s_phaseStartTime = GetNowMs();
        }
        break;
    }

    case Phase::VERIFYING: {
        // Phase 2: 等待動作生效後確認（關閉截圖以節省效能）
        DWORD elapsed = GetNowMs() - s_phaseStartTime;
        if (elapsed < VERIFY_WAIT_MS) break;

        // 直接視為康復成功（盲操作模式）
        LogRecovery("盲操作完成，等待後返回 IDLE");

        s_attempts++;
        if (s_attempts >= MAX_RECOVERY_ATTEMPTS) {
            LogRecovery("❌ 超過最大嘗試次數，進入緊急停機");
            s_phase = Phase::ABORT;
        } else {
            // 多次嘗試後才返回 IDLE
            if (s_attempts >= 2) {
                LogRecovery("✅ Recovery 完成 (嘗試 %d 次)，返回 IDLE", s_attempts);
                s_phase = Phase::DONE;
            } else {
                // 再嘗試一次
                s_phase = Phase::INIT;
                s_phaseStartTime = GetNowMs();
            }
        }
        break;
    }

    case Phase::DONE: {
        // Phase 3: 恢復完成，返回 IDLE
        LogRecovery("=== Recovery 完成，重置回 IDLE ===");
        SetBotState(BotState::IDLE);
        s_attempts = 0;
        s_phase = Phase::INIT;
        s_initialized = false;
        break;
    }

    case Phase::ABORT: {
        // Phase 4: 緊急停機
        LogRecovery("!!! RECOVERY ABORT - 緊急停機 !!!");
        LogRecovery("建議手動檢查遊戲狀態後，按 F11 恢復");
        SetBotState(BotState::EMERGENCY_STOP);
        RequestRecovery("Recovery ABORT 緊急停機");  // 進入 Recovery，不直接停止
        s_phase = Phase::INIT;
        break;
    }

    default:
        s_phase = Phase::INIT;
        break;
    }
}