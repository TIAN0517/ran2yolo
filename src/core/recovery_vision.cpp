// ============================================================
// recovery_vision.cpp
// 增強版 Recovery 系統 - 結合視覺識別
// ============================================================

#include "recovery_vision.h"
#include "../game/game_process.h"
#include "bot_logic.h"
#include "../vision/screenshot_universal.h"
#include "../embed/dm_wrapper.h"
#include "../input/input_sender.h"
#include "../gui/gui_ranbot.h"
#include <cstdarg>
#include <algorithm>
#include <cmath>
#include <ctime>

// ============================================================
// 內部狀態
// ============================================================
namespace VisionRecovery {

// Recovery 階段
enum class Phase {
    INIT = 0,              // 初始化，進入 Recovery
    MEMORY_RETRY = 1,      // 嘗試重讀記憶體
    VISION_CHECK = 2,      // 使用視覺識別確認狀態
    SAFE_WAIT = 3,          // 安全等待（記憶體失效但視覺正常）
    DONE = 4,              // 完成，返回正常狀態
    ABORT = 99             // 放棄，停機
};

// 內部狀態
static Phase s_phase = Phase::INIT;
static int s_attempts = 0;
static const int MAX_RECOVERY_ATTEMPTS = 5;

static MemoryFailureReason s_lastFailureReason = MemoryFailureReason::NONE;
static RecoveryConfig s_config;
static DWORD s_phaseStartTime = 0;
static DWORD s_lastLogTime = 0;

static bool s_visionInitialized = false;
static HWND s_hWnd = NULL;

// 視覺識別結果緩存
static VisionCheckResult s_lastVisionResult;
static DWORD s_lastVisionCheckTime = 0;
static const DWORD VISION_CHECK_INTERVAL_MS = 2000;

// 隨機點擊位置（脫困用）
static const int RANDOM_CLICK_COUNT = 8;
static struct Point { int rx, ry; } s_randomPoints[RANDOM_CLICK_COUNT] = {
    {45, 55}, {55, 55}, {50, 50}, {50, 60},
    {45, 50}, {55, 50}, {50, 45}, {50, 65}
};
static int s_clickIndex = 0;

// ============================================================
// CAPTCHA 安全碼檢測（本地窗口檢測，無截圖）
// ============================================================
static bool s_captchaNotified = false;

bool CheckCaptchaLocal() {
    // Fast window check - 無需截圖
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
        BotState oldState = (BotState)GetBotState();
        if (oldState != BotState::PAUSED) {
            SetBotState(BotState::PAUSED);
            printf("[Captcha] ★★★ 偵測到 CAPTCHA 安全碼視窗！已強制暫停機器人 ★★★\n");
        }
        if (!s_captchaNotified) {
            s_captchaNotified = true;
            MessageBoxW(NULL,
                L"偵測到 CAPTCHA 安全碼視窗！\n\n"
                L"機器人已自動暫停。\n"
                L"請手動輸入安全碼後，按 F11 恢復運行。",
                L"JyTrainer - 安全碼警告",
                MB_OK | MB_ICONWARNING | MB_TOPMOST);
        }
        return true;
    }
    s_captchaNotified = false;
    return false;
}

// ============================================================
// 輔助函式
// ============================================================
inline DWORD GetNowMs() { return GetTickCount(); }

void LogRV(const char* fmt, ...) {
    DWORD now = GetNowMs();
    if (now - s_lastLogTime < 500) return;  // 防刷屏
    s_lastLogTime = now;

    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    printf("[VisionRecovery] %s\n", buf);
    UIAddLog("[VisionRecovery] %s", buf);
}

// 盲按 ESC
void BlindPressESC() {
    INPUT esc_down = {};
    esc_down.type = INPUT_KEYBOARD;
    esc_down.ki.wVk = VK_ESCAPE;
    esc_down.ki.dwFlags = 0;

    INPUT esc_up = {};
    esc_up.type = INPUT_KEYBOARD;
    esc_up.ki.wVk = VK_ESCAPE;
    esc_up.ki.dwFlags = KEYEVENTF_KEYUP;

    INPUT inputs[2] = { esc_down, esc_up };
    SendInput(2, inputs, sizeof(INPUT));

    LogRV("盲按 ESC");
}

// 隨機點擊（避免重複）
void RandomClick() {
    int idx = s_clickIndex % RANDOM_CLICK_COUNT;
    int rx = s_randomPoints[idx].rx + (rand() % 11 - 5);
    int ry = s_randomPoints[idx].ry + (rand() % 11 - 5);

    s_clickIndex++;

    if (s_hWnd) {
        LPARAM lp = MAKELPARAM(rx, ry);
        SendMessage(s_hWnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
        Sleep(50);
        SendMessage(s_hWnd, WM_LBUTTONUP, 0, lp);
        LogRV("隨機點擊 (%d%%, %d%%)", rx, ry);
    }
}

// ============================================================
// 視覺識別實現
// ============================================================
bool InitVisionSystem(HWND hWnd) {
    s_hWnd = hWnd;
    // 視覺識別已預設關閉，不再初始化截圖系統
    s_visionInitialized = false;
    LogRV("⚠️ 視覺識別已關閉（使用本地窗口檢測）");
    return true;
}

VisionCheckResult AnalyzeCurrentScreen() {
    VisionCheckResult result;

    if (!s_visionInitialized || !s_hWnd) {
        result.description = "視覺系統未初始化";
        return result;
    }

    // 截圖
    auto cap = ScreenshotUniversal::Capture(s_hWnd);
    if (!cap.success) {
        result.description = "截圖失敗";
        return result;
    }

    // ========================================
    // 簡單的顏色/區域檢測
    // ========================================

    // 檢測 HP/MP 條（紅色/藍色豎條區域）
    int hpBarCount = 0, mpBarCount = 0;
    int greenCount = 0, brownCount = 0;

    // 掃描底部狀態欄區域（通常是 UI 最下方）
    int scanY = cap.height - 50;
    int scanHeight = 40;

    for (int y = scanY; y < scanY + scanHeight && y < cap.height; y += 2) {
        for (int x = cap.width / 4; x < cap.width * 3 / 4; x += 4) {
            int idx = (y * cap.width + x) * 4;
            if (idx + 2 >= (int)cap.pixels.size()) continue;

            uint8_t b = cap.pixels[idx];
            uint8_t g = cap.pixels[idx + 1];
            uint8_t r = cap.pixels[idx + 2];

            // HP 條：紅色系 (R > 150, R > G*2, R > B*2)
            if (r > 150 && r > g * 1.5 && r > b * 1.5) {
                hpBarCount++;
            }
            // MP 條：藍色系 (B > 150, B > R*2, B > G*2)
            if (b > 150 && b > r * 1.5 && b > g * 1.5) {
                mpBarCount++;
            }
            // 草地/野外：綠色系
            if (g > r * 1.2 && g > b * 1.2 && g > 80) {
                greenCount++;
            }
            // 土地/岩石：棕色系
            if (r > 80 && r > b * 1.5 && g > 40 && g < 120) {
                brownCount++;
            }
        }
    }

    // 判斷結果
    int totalPixels = (scanHeight / 2) * (cap.width / 2) * (cap.width / 4);
    float hpRatio = (float)hpBarCount / totalPixels;
    float mpRatio = (float)mpBarCount / totalPixels;
    float greenRatio = (float)greenCount / totalPixels;
    float brownRatio = (float)brownCount / totalPixels;

    // HP/MP 條檢測閾值
    const float HP_THRESHOLD = 0.01f;
    const float MP_THRESHOLD = 0.01f;
    const float GREEN_THRESHOLD = 0.1f;
    const float BROWN_THRESHOLD = 0.15f;

    result.hp_bar_visible = (hpRatio > HP_THRESHOLD);
    result.mp_bar_visible = (mpRatio > MP_THRESHOLD);
    result.monster_detected = false;  // 需要更複雜的檢測

    if (result.hp_bar_visible && result.mp_bar_visible) {
        result.safe_state = SafeState::IN_FIELD;
        result.confidence = 0.7f;
        result.description = "檢測到 HP/MP 條，可能在野外";
    } else if (result.hp_bar_visible) {
        result.safe_state = SafeState::IN_TOWN;
        result.confidence = 0.6f;
        result.description = "檢測到 HP 條，可能在城鎮";
    } else if (greenRatio > GREEN_THRESHOLD) {
        result.safe_state = SafeState::IN_FIELD;
        result.confidence = 0.5f;
        result.description = "檢測到綠地區域，在野外";
    } else if (brownRatio > BROWN_THRESHOLD) {
        result.safe_state = SafeState::IN_FIELD;
        result.confidence = 0.5f;
        result.description = "檢測到土地區域，在野外";
    } else {
        result.safe_state = SafeState::UNKNOWN;
        result.confidence = 0.3f;
        result.description = "無法確定當前狀態";
    }

    // 檢測背包滿（如果背包 UI 可見）
    // 這個需要根據實際截圖特徵調整

    s_lastVisionResult = result;
    s_lastVisionCheckTime = GetNowMs();

    LogRV("視覺分析: %s (conf=%.2f, hp=%d, mp=%d, green=%d)",
          result.description.c_str(), result.confidence,
          hpBarCount, mpBarCount, greenCount);

    return result;
}

bool IsInTown() {
    if (GetNowMs() - s_lastVisionCheckTime > VISION_CHECK_INTERVAL_MS) {
        AnalyzeCurrentScreen();
    }
    return s_lastVisionResult.safe_state == SafeState::IN_TOWN;
}

bool HasNearbyThreat() {
    if (GetNowMs() - s_lastVisionCheckTime > VISION_CHECK_INTERVAL_MS) {
        AnalyzeCurrentScreen();
    }
    return s_lastVisionResult.monster_detected;
}

bool WaitForStableScreen(DWORD timeoutMs) {
    DWORD start = GetNowMs();
    VisionCheckResult prev;

    while (GetNowMs() - start < timeoutMs) {
        VisionCheckResult curr = AnalyzeCurrentScreen();

        // 檢測畫面是否穩定（連續 3 次結果相同）
        if (prev.safe_state == curr.safe_state &&
            prev.safe_state != SafeState::UNKNOWN &&
            GetNowMs() - start > 1000) {
            return true;
        }
        prev = curr;
        Sleep(500);
    }
    return false;
}

// ============================================================
// 核心邏輯
// ============================================================
void SetMemoryFailureReason(MemoryFailureReason reason) {
    s_lastFailureReason = reason;

    const char* reasonStr = "未知";
    switch (reason) {
        case MemoryFailureReason::MAP_ID_INVALID: reasonStr = "MapID 無效"; break;
        case MemoryFailureReason::INVENTORY_BASE_INVALID: reasonStr = "背包基底無效"; break;
        case MemoryFailureReason::CHARACTER_PTR_INVALID: reasonStr = "角色指標無效"; break;
        case MemoryFailureReason::POSITION_INVALID: reasonStr = "座標無效"; break;
        case MemoryFailureReason::HP_MP_INVALID: reasonStr = "HP/MP 無效"; break;
        case MemoryFailureReason::ALL_ATTRIBUTES_INVALID: reasonStr = "所有屬性無效"; break;
        case MemoryFailureReason::BASE_ADDR_ZERO: reasonStr = "BaseAddr=0"; break;
        default: break;
    }

    LogRV("記憶體失效: %s", reasonStr);
}

void EnterRecovery(const char* reason) {
    LogRV("========================================");
    LogRV("⚠️ 進入 Recovery 模式: %s", reason);
    LogRV("  嘗試次數: %d/%d", s_attempts + 1, MAX_RECOVERY_ATTEMPTS);
    LogRV("========================================");

    s_phase = Phase::MEMORY_RETRY;
    s_phaseStartTime = GetNowMs();
}

void RecoveryTick(HWND hWnd) {
    if (!s_hWnd) s_hWnd = hWnd;

    DWORD now = GetNowMs();

    // 第一次進入
    if (s_phase == Phase::INIT) {
        LogRV("=== Recovery 初始化 ===");
        LogRV("  記憶體失效原因: %d", (int)s_lastFailureReason);
        LogRV("  嘗試 #%d/%d", s_attempts + 1, MAX_RECOVERY_ATTEMPTS);

        // 停止攻擊（處於安全狀態）
        LogRV("  動作: 暫停攻擊，等待記憶體恢復");

        s_phaseStartTime = now;
        s_phase = Phase::MEMORY_RETRY;
        return;
    }

    switch (s_phase) {
    case Phase::MEMORY_RETRY: {
        // 階段 1: 嘗試重讀記憶體
        DWORD elapsed = now - s_phaseStartTime;

        if (elapsed < s_config.memory_retry_ms) {
            // 還沒到重試時間
            break;
        }

        // 嘗試刷新 baseAddr
        GameHandle gh = GetGameHandle();
        if (gh.baseAddr != 0) {
            DWORD refreshed = RefreshGameBaseAddress(&gh);
            if (refreshed) {
                gh.baseAddr = refreshed;
                SetGameHandle(&gh);
                LogRV("✅ baseAddr 刷新成功: 0x%08X", refreshed);

                // 設置狀態為完成
                s_phase = Phase::DONE;
                s_phaseStartTime = now;
                break;
            }
        }

        // 記憶體仍未恢復，切換到視覺識別
        LogRV("⚠️ 記憶體未恢復，進入視覺識別");
        s_phase = Phase::VISION_CHECK;
        s_phaseStartTime = now;
        break;
    }

    case Phase::VISION_CHECK: {
        // 階段 2: 使用視覺識別確認狀態
        if (!s_config.use_vision) {
            LogRV("⚠️ 視覺識別已禁用，直接進入安全等待");
            s_phase = Phase::SAFE_WAIT;
            s_phaseStartTime = now;
            break;
        }

        // 執行視覺識別
        VisionCheckResult result = AnalyzeCurrentScreen();

        if (result.confidence >= 0.6f) {
            // 視覺識別成功
            if (result.safe_state == SafeState::IN_TOWN) {
                LogRV("✅ 視覺識別: 在城鎮 (安全狀態)");
                s_phase = Phase::SAFE_WAIT;
                s_phaseStartTime = now;
            } else if (result.safe_state == SafeState::IN_FIELD) {
                LogRV("⚠️ 視覺識別: 在野外，需要確認安全");
                s_phase = Phase::SAFE_WAIT;
                s_phaseStartTime = now;
            } else {
                LogRV("⚠️ 視覺識別: 狀態不明確");
                // 仍然進入安全等待
                s_phase = Phase::SAFE_WAIT;
                s_phaseStartTime = now;
            }
        } else {
            // 視覺識別失敗
            LogRV("⚠️ 視覺識別置信度低 (%.2f)，等待穩定", result.confidence);

            if (now - s_phaseStartTime > s_config.vision_timeout_ms) {
                LogRV("⚠️ 視覺識別超時，進入安全等待");
                s_phase = Phase::SAFE_WAIT;
                s_phaseStartTime = now;
            }
        }
        break;
    }

    case Phase::SAFE_WAIT: {
        // 階段 3: 安全等待
        DWORD elapsed = now - s_phaseStartTime;

        // 安全等待 5 秒
        if (elapsed < 5000) {
            // 持續監控
            if (elapsed > 2500 && (elapsed - 2500) % 2000 < 100) {
                // 每 2 秒檢查一次視覺
                if (s_config.use_vision) {
                    VisionCheckResult result = AnalyzeCurrentScreen();
                    LogRV("  監控: %s (conf=%.2f)", result.description.c_str(), result.confidence);
                }
            }
            break;
        }

        // 安全等待結束，增加嘗試次數
        s_attempts++;

        if (s_attempts >= MAX_RECOVERY_ATTEMPTS) {
            LogRV("❌ 超過最大嘗試次數 (%d/%d)", s_attempts, MAX_RECOVERY_ATTEMPTS);
            s_phase = Phase::ABORT;
        } else {
            LogRV("⚠️ 嘗試 #%d 完成，繼續監控", s_attempts);
            LogRV("  動作: 再次嘗試記憶體讀取");
            s_phase = Phase::MEMORY_RETRY;
            s_phaseStartTime = now;
        }
        break;
    }

    case Phase::DONE: {
        // 階段 4: 完成
        LogRV("========================================");
        LogRV("✅ Recovery 完成，返回 IDLE");
        LogRV("  總嘗試次數: %d", s_attempts);
        LogRV("========================================");

        // 重置並返回 IDLE
        s_attempts = 0;
        s_phase = Phase::INIT;
        s_lastFailureReason = MemoryFailureReason::NONE;

        // 恢復完成，等待 BotTick 的下一次讀取
        break;
    }

    case Phase::ABORT: {
        // 階段 99: 放棄，停機
        LogRV("========================================");
        LogRV("❌❌❌ RECOVERY 放棄 - 緊急停機");
        LogRV("  最後失敗原因: %d", (int)s_lastFailureReason);
        LogRV("  建議手動檢查遊戲狀態");
        LogRV("========================================");

        // Recovery 系統已嘗試多次仍失敗，進入 Recovery 狀態
        // 不直接停止，改為讓 Recovery 系統處理清理和後續
        RequestRecovery("Vision Recovery 放棄：系統已嘗試所有復原策略");

        // 重置狀態
        s_attempts = 0;
        s_phase = Phase::INIT;

        LogRV("進入 Recovery 狀態，請手動干預後 F11 恢復");
        break;
    }

    default:
        s_phase = Phase::INIT;
        break;
    }
}

bool ShouldReturnToNormal() {
    return s_phase == Phase::DONE;
}

void ExitRecovery(const char* reason) {
    LogRV("退出 Recovery: %s", reason);
    s_attempts = 0;
    s_phase = Phase::INIT;
}

// ============================================================
// 配置
// ============================================================
void SetConfig(const RecoveryConfig& config) {
    s_config = config;
}

RecoveryConfig GetConfig() {
    return s_config;
}

void ResetAttempts() {
    s_attempts = 0;
    s_phase = Phase::INIT;
}

int GetAttempts() {
    return s_attempts;
}

bool IsInRecoveryState() {
    return GetBotState() == BotState::RECOVERY;
}

MemoryFailureReason GetLastMemoryFailure() {
    return s_lastFailureReason;
}

const char* GetMemoryFailureReasonString(MemoryFailureReason reason) {
    switch (reason) {
        case MemoryFailureReason::NONE: return "NONE";
        case MemoryFailureReason::MAP_ID_INVALID: return "MAP_ID_INVALID";
        case MemoryFailureReason::INVENTORY_BASE_INVALID: return "INVENTORY_BASE_INVALID";
        case MemoryFailureReason::CHARACTER_PTR_INVALID: return "CHARACTER_PTR_INVALID";
        case MemoryFailureReason::POSITION_INVALID: return "POSITION_INVALID";
        case MemoryFailureReason::HP_MP_INVALID: return "HP_MP_INVALID";
        case MemoryFailureReason::ALL_ATTRIBUTES_INVALID: return "ALL_ATTRIBUTES_INVALID";
        case MemoryFailureReason::BASE_ADDR_ZERO: return "BASE_ADDR_ZERO";
        default: return "UNKNOWN";
    }
}

// ============================================================
// 初始化
// ============================================================
struct InitOnce {
    InitOnce() {
        s_config = RecoveryConfig{};
        s_attempts = 0;
        s_phase = Phase::INIT;
        s_lastFailureReason = MemoryFailureReason::NONE;
    }
};
static InitOnce s_init;

} // namespace VisionRecovery
