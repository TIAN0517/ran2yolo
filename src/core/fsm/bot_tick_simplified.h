// ============================================================
// Simplified BotTick using State Handler Pattern
// ============================================================

#pragma once
#ifndef FSM_BOT_TICK_SIMPLIFIED_H
#define FSM_BOT_TICK_SIMPLIFIED_H

#include "state_handler.h"
#include "../game/game_process.h"

// External declarations from bot_logic.cpp
extern std::atomic<bool> g_licenseValid;
extern bool IsLicenseValid();
extern BotConfig g_cfg;
extern std::atomic<int> g_State;
extern BotState GetBotState();
extern void SetBotState(BotState s);
extern bool FindGameProcess(GameHandle* gh);
extern GameHandle GetGameHandle();
extern void SetGameHandle(GameHandle* gh);
extern void TransitionState(BotState nextState, const char* reason, const PlayerState* st);
extern bool ReadPlayerState(GameHandle* gh, PlayerState* out);
extern void Log(const char* tag, const char* msg);

// ============================================================
// 統一的安全碼檢查（使用 bot_logic.cpp 中的 CheckCaptchaWindow）
// ============================================================
static bool CheckCaptchaWindowOnce() {
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

// ============================================================
// 全局狀態機看門狗（防止卡死）
// ============================================================
static DWORD s_lastStateProgressTick = 0;
static BotState s_lastWatchdogState = BotState::IDLE;
static DWORD s_lastWatchdogLog = 0;
static const DWORD GLOBAL_WATCHDOG_TIMEOUT_MS = 90000;     // 90 秒（普通狀態）
static const DWORD RECOVERY_WATCHDOG_TIMEOUT_MS = 180000;  // 180 秒（RECOVERY 專用）

static void CheckGlobalWatchdog(BotState currentState, DWORD now) {
    // 狀態變化時重置計時器
    if (currentState != s_lastWatchdogState) {
        s_lastWatchdogState = currentState;
        s_lastStateProgressTick = now;
        return;
    }

    // 根據狀態調整超時：RECOVERY 需要更長的超時避免誤殺
    DWORD timeout = (currentState == BotState::RECOVERY)
        ? RECOVERY_WATCHDOG_TIMEOUT_MS
        : GLOBAL_WATCHDOG_TIMEOUT_MS;

    // 檢查超時
    DWORD elapsed = now - s_lastStateProgressTick;
    if (elapsed > timeout) {
        // Rate limit: 每 10 秒最多 log 一次，避免日誌刷屏
        if (now - s_lastWatchdogLog > 10000) {
            Log("WATCHDOG", "★★★ 狀態機卡死！（已暫時忽略強制停止）★★★");
            Logf("WATCHDOG", "  └- 狀態: %d | 停留: %u ms | 閾值: %u ms",
                (int)currentState, elapsed, timeout);
            s_lastWatchdogLog = now;
        }
        return;
    }
}

// ============================================================
// 簡化版 BotTick（使用 State Handler）
// 整合 DM 模式和視覺模式的戰鬥邏輯
// ============================================================
static void BotTickSimplified(GameHandle* gh) {
    DWORD now = GetTickCount();
    BotState currentState = GetBotState();

    // Watchdog 只負責報警，不阻斷 handler tick。
    CheckGlobalWatchdog(currentState, now);

    // ── 卡密驗證 ──
    if (!IsLicenseValid()) {
        static DWORD s_lastKamiLog = 0;
        if (now - s_lastKamiLog > 5000) {
            Log("認證", "❌ 卡密未驗證，功能已鎖定");
            s_lastKamiLog = now;
        }
        Sleep(200);
        return;
    }

    // ── Bot 未啟動：停止時快速退出 ──
    if (!g_cfg.active.load()) {
        return;
    }

    // ── 安全碼檢查 ──
    if (CheckCaptchaWindowOnce()) {
        Sleep(100);
        return;
    }

    // ── PAUSED 狀態：只睡眠不處理 ──
    if (currentState == BotState::PAUSED) {
        Sleep(100);
        return;
    }

    // ── 遊戲就緒檢測 ──
    GameHandle curGh = GetGameHandle();
    if (!curGh.hProcess || !curGh.attached || !curGh.baseAddr) {
        if (FindGameProcess(&curGh)) {
            SetGameHandle(&curGh);
            Logf("Bot", "✅ [BotTick] 遊戲附加: pid=%u baseAddr=0x%08X",
                curGh.pid, curGh.baseAddr);
        } else {
            static DWORD s_lastGameNotReady = 0;
            if (now - s_lastGameNotReady > 3000) {
                Log("Bot", "⚠️ [BotTick] 等待遊戲就緒...");
                s_lastGameNotReady = now;
            }
            Sleep(200);
            return;
        }
    }

    // Pattern Scan 恢復 baseAddr（邊界情況）
    if (curGh.baseAddr == 0 && curGh.hProcess) {
        curGh.baseAddr = GetGameBaseAddress(&curGh);
        if (curGh.baseAddr) SetGameHandle(&curGh);
    }

    // 同步 gh
    if (curGh.hProcess) {
        static GameHandle s_ghFallback;
        s_ghFallback = curGh;
        gh = &s_ghFallback;
    }

    // 觸發 Pattern Scan（如需要）
    TriggerPatternScanIfNeeded(gh);

    // ── IDLE → HUNTING 冷卻：防止快速震盪 ──
    static DWORD s_lastIdleToHuntingTime = 0;
    static bool s_idleJustTransitioned = false;

    // ── 延遲啟動：遊戲就緒時從 IDLE 推進到 HUNTING ──
    if (currentState == BotState::IDLE) {
        // 防止快速重複轉換：至少間隔 1 秒
        if (s_idleJustTransitioned && (now - s_lastIdleToHuntingTime) < 1000) {
            Sleep(100);
            return;
        }

        TransitionState(BotState::HUNTING, "DelayedStartGameReady", NULL);
        currentState = GetBotState();

        // 標記剛轉換，等待冷卻
        s_idleJustTransitioned = true;
        s_lastIdleToHuntingTime = now;
    } else {
        // 非 IDLE 狀態：重置標記
        s_idleJustTransitioned = false;
    }

    // ── 單一路徑：使用 State Handler 處理目前狀態 ──
    IStateHandler* handler = StateHandlerRegistry::Instance().Get(currentState);

    if (!handler) {
        static DWORD s_lastNoHandler = 0;
        if (now - s_lastNoHandler > 5000) {
            Logf("Bot", "⚠️ [BotTick] 無 Handler 處理狀態 %d", (int)currentState);
            s_lastNoHandler = now;
        }
        Sleep(100);
        return;
    }

    // ── 讀取玩家狀態 ──
    PlayerState st;
    bool readOK = ReadPlayerState(gh, &st);

    if (readOK) {
        // ── 讀取成功：交給 Handler 處理 ──
        int nextState = handler->Tick(gh, &st, now);

        // 狀態轉換
        if (nextState >= 0 && nextState != (int)currentState) {
            TransitionState((BotState)nextState, handler->Name(), &st);
        }
    } else {
        // ── 讀取失敗：交給 RecoveryHandler ──
        // 使用 IStateHandler* 而非 static_cast（避免需要完整類型定義）
        IStateHandler* recovery = StateHandlerRegistry::Instance().Get(BotState::RECOVERY);
        if (recovery) {
            int recoveryNext = recovery->Tick(gh, NULL, now);
            if (recoveryNext >= 0 && recoveryNext != (int)currentState) {
                TransitionState((BotState)recoveryNext, recovery->Name(), NULL);
            }
        }
    }

    Sleep(50);
}

#endif // FSM_BOT_TICK_SIMPLIFIED_H
