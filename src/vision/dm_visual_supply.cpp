// ============================================================
// dm_visual_supply.cpp
// 視覺補給模組實現（使用 CoordCalibrator 座標）
// ============================================================

#include "dm_visual_supply.h"
#include "../embed/dm_wrapper.h"
#include "../input/input_sender.h"
#include "../game/game_process.h"
#include "../core/bot_logic.h"
#include "../platform/coord_calib.h"
#include <cstring>

// 輔助：校正座標點擊（相對→螢幕座標）
static void ClickCoord(HWND hWnd, CalibIndex idx) {
    if (!hWnd) return;
    int sx, sz;
    CoordCalibrator& calib = CoordCalibrator::Instance();
    calib.SetGameHwnd(hWnd);
    if (calib.ToScreen(idx, &sx, &sz)) {
        DMVisual::ClickAt(sx, sz);
    }
}

namespace VisualSupply {

    // ============================================================
    // 內部狀態
    // ============================================================
    static SupplyPhase s_phase = SupplyPhase::IDLE;
    static SupplyConfig s_config;
    static DWORD s_phaseStartTime = 0;
    static int s_retryCount = 0;
    static int s_buyStage = 0;  // 0=箭矢, 1=HP, 2=MP, 3=SP
    static HWND s_hwnd = NULL;
    static bool s_supplyCompleted = false;  // 標記是否真正完成補給
    static bool s_npcFound = false;        // 標記是否找到 NPC
    static int s_findNpcEnterCount = 0;
    static DWORD s_findNpcLastEnterTime = 0;
    static DWORD s_findNpcWalkTick = 0;
    static bool s_findNpcStartedWalk = false;
    static DWORD s_findNpcLastCheckTime = 0;
    static DWORD s_walkNpcLastCheckTime = 0;
    static DWORD s_walkNpcLastEnterTime = 0;
    static DWORD s_waitDialogLastCheckTime = 0;

    // NPC 查找超時配置（毫秒）
    static int s_npcFindTimeoutMs = 8000;   // 8 秒內找不到 NPC 就 fallback
    static int s_walkTimeoutMs = 12000;     // 走路超時 12 秒
    static int s_dialogTimeoutMs = 1500;    // 對話框等待 1.5 秒

    // ============================================================
    // 工具函式
    // ============================================================

    static void ResetPhaseRuntime() {
        s_findNpcEnterCount = 0;
        s_findNpcLastEnterTime = 0;
        s_findNpcWalkTick = 0;
        s_findNpcStartedWalk = false;
        s_findNpcLastCheckTime = 0;
        s_walkNpcLastCheckTime = 0;
        s_walkNpcLastEnterTime = 0;
        s_waitDialogLastCheckTime = 0;
    }

    static bool FailSupply(const char* reason) {
        Logf("DM", "❌ [VisualSupply] %s", reason ? reason : "補給失敗");
        s_npcFound = false;
        s_supplyCompleted = false;
        s_phase = SupplyPhase::IDLE;
        ResetPhaseRuntime();
        return true;
    }

    // 檢查是否超時
    static bool IsTimeout(DWORD timeoutMs) {
        return (GetTickCount() - s_phaseStartTime) > timeoutMs;
    }

    // 進入下一階段
    static void NextPhase() {
        s_phase = (SupplyPhase)((int)s_phase + 1);
        s_phaseStartTime = GetTickCount();
        s_retryCount = 0;
        ResetPhaseRuntime();
        Logf("DM", "[補給] Phase: %d", (int)s_phase);
    }

    // 增加重試計數
    static bool IncRetry() {
        s_retryCount++;
        if (s_retryCount >= s_config.maxRetries) {
            Logf("DM", "[補給] 重試次數過多，放棄此階段");
            return true;  // 超過最大重試
        }
        return false;
    }

    // 發送數字鍵（使用 SendKeyFast）
    static void SendNumber(int num) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", num);
        for (char* p = buf; *p; p++) {
            BYTE vk = *p - '0' + VK_NUMPAD0;
            if (*p == '0') vk = VK_NUMPAD0;
            SendKeyFast(vk);
            Sleep(50);
        }
    }

    // 發送普通數字（非小鍵盤）
    static void SendNumberNormal(int num) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", num);
        for (char* p = buf; *p; p++) {
            BYTE vk = *p;
            INPUT inputs[2] = {};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = vk;
            inputs[0].ki.dwFlags = 0;
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = vk;
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, inputs, sizeof(INPUT));
            Sleep(30);
        }
    }

    // ============================================================
    // 初始化
    // ============================================================
    void Init() {
        s_phase = SupplyPhase::IDLE;
        s_buyStage = 0;
        s_retryCount = 0;
        s_supplyCompleted = false;
        s_npcFound = false;
        ResetPhaseRuntime();
        s_hwnd = DMWrapper::g_dm.GetBindWindow();

        // 預設配置
        s_config.buyArrowQty = 300;
        s_config.buyHPQty = 300;
        s_config.buyMPQty = 300;
        s_config.buySPQty = 300;
        s_config.maxRetries = 3;
        s_config.phaseTimeoutMs = 5000;

        Logf("DM", "✅ 視覺補給模組初始化");
    }

    // ============================================================
    // 開始供應
    // ============================================================
    void Begin() {
        s_phase = SupplyPhase::FIND_NPC;
        s_phaseStartTime = GetTickCount();
        s_retryCount = 0;
        s_buyStage = 0;
        s_supplyCompleted = false;
        s_npcFound = false;
        ResetPhaseRuntime();
        // 每次開始補給前刷新視窗句柄
        s_hwnd = DMWrapper::g_dm.GetBindWindow();
        Logf("DM", "📦 開始視覺供應");
    }

    // ============================================================
    // 供應流程 Tick
    // ============================================================
    bool Tick() {
        // 確保只在 TOWN_SUPPLY 狀態執行
        if (GetBotState() != BotState::TOWN_SUPPLY) {
            return false;
        }

        DWORD now = GetTickCount();

        // 激活遊戲窗口（確保有焦點）- 已禁用，避免黑屏
        // 如果需要滑鼠操作，可手動開啟：
        // GameHandle gh = GetGameHandle();
        // if (gh.hWnd && GetForegroundWindow() != gh.hWnd) {
        //     SetForegroundWindow(gh.hWnd);
        // }

        // 添加調試：每 3 秒打印一次當前階段
        static DWORD s_lastPhaseLog = 0;
        if (now - s_lastPhaseLog > 3000) {
            Logf("DM", "📦 [VisualSupply] Phase=%d Time=%lums",
                (int)s_phase, now - s_phaseStartTime);
            s_lastPhaseLog = now;
        }

        switch (s_phase) {
            // ============================================================
            // Phase 1: 嘗試對話（盲按 Enter + 走路）
            // ============================================================
            case SupplyPhase::FIND_NPC: {
                DWORD elapsed = now - s_phaseStartTime;

                // 持續走路
                if (!s_findNpcStartedWalk) {
                    s_findNpcWalkTick = now;
                    s_findNpcStartedWalk = true;
                    Logf("DM", "🚶 [FIND_NPC] 開始走路");
                }

                // 每 300ms 按一次前進鍵（改用方向鍵上，避免和空白鍵撿物衝突）
                if (now - s_findNpcWalkTick > 300) {
                    BYTE vk = VK_UP;  // 方向鍵上走路
                    INPUT inputs[2] = {};
                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.wVk = vk;
                    inputs[0].ki.dwFlags = 0;
                    inputs[1].type = INPUT_KEYBOARD;
                    inputs[1].ki.wVk = vk;
                    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, inputs, sizeof(INPUT));
                    s_findNpcWalkTick = now;
                }

                // 每 1 秒按一次 Enter
                if (now - s_findNpcLastEnterTime > 1000) {
                    BYTE vk = VK_RETURN;
                    INPUT inputs[2] = {};
                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.wVk = vk;
                    inputs[0].ki.dwFlags = 0;
                    inputs[1].type = INPUT_KEYBOARD;
                    inputs[1].ki.wVk = vk;
                    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, inputs, sizeof(INPUT));
                    Logf("DM", "⌨️ [FIND_NPC] 按 Enter #%d", s_findNpcEnterCount + 1);
                    s_findNpcEnterCount++;
                    s_findNpcLastEnterTime = now;
                }

                // 每 500ms 檢查是否進入商店
                if (now - s_findNpcLastCheckTime > 500) {
                    DMVisual::FindResult r = DMVisual::FindPic("buy.png", 0, 0, 1000, 1000, 0.5);
                    if (r.found) {
                        Logf("DM", "✅ [FIND_NPC] 進入商店！");
                        s_npcFound = true;
                        NextPhase();
                        break;
                    }
                    s_findNpcLastCheckTime = now;
                }

                // 找不到 NPC/商店時不要假裝成功，避免在野外盲目補給。
                if (s_findNpcEnterCount >= 20 || elapsed > 20000) {
                    return FailSupply("FIND_NPC 找不到 NPC/商店，停止補給");
                }
                break;
            }

            // ============================================================
            // Phase 2: 等待商店開啟
            // ============================================================
            case SupplyPhase::WALK_TO_NPC: {
                // 每 800ms 按一次 Enter 確認
                if (now - s_walkNpcLastEnterTime > 800) {
                    BYTE vk = VK_RETURN;
                    INPUT inputs[2] = {};
                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.wVk = vk;
                    inputs[0].ki.dwFlags = 0;
                    inputs[1].type = INPUT_KEYBOARD;
                    inputs[1].ki.wVk = vk;
                    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, inputs, sizeof(INPUT));
                    s_walkNpcLastEnterTime = now;
                }

                // 每 500ms 檢查商店
                if (now - s_walkNpcLastCheckTime > 500) {
                    DMVisual::FindResult r = DMVisual::FindPic("buy.png", 0, 0, 1000, 1000, 0.5);
                    if (r.found) {
                        Logf("DM", "✅ [WALK_TO_NPC] 商店已開啟！");
                        NextPhase();
                        break;
                    }
                    s_walkNpcLastCheckTime = now;
                }

                // 10 秒超時
                if (IsTimeout(10000)) {
                    return FailSupply("WALK_TO_NPC 等不到商店開啟");
                }
                break;
            }

            // ============================================================
            // Phase 3: 等待對話框
            // ============================================================
            case SupplyPhase::WAIT_DIALOG: {
                // 每 500ms 檢查商店
                if (now - s_waitDialogLastCheckTime > 500) {
                    DMVisual::FindResult r = DMVisual::FindPic("buy.png", 0, 0, 1000, 1000, 0.5);
                    if (r.found) {
                        Logf("DM", "✅ [WAIT_DIALOG] 商店已開啟！");
                        NextPhase();
                        break;
                    }
                    s_waitDialogLastCheckTime = now;
                }

                // 5 秒超時
                if (IsTimeout(5000)) {
                    return FailSupply("WAIT_DIALOG 等不到商店畫面");
                }
                break;
            }

            // ============================================================
            // Phase 4: 點擊購買分頁（開啟購買介面）
            // ============================================================
            case SupplyPhase::OPEN_SHOP: {
                // 點擊校正後的商店開啟座標
                ClickCoord(s_hwnd, CalibIndex::SHOP_OPEN);
                Logf("DM", "🛒 點擊購買分頁 (SHOP_OPEN)");
                NextPhase();
                break;
            }

            // ============================================================
            // Phase 5: 點擊 HP 水分頁
            // ============================================================
            case SupplyPhase::SELECT_ITEM_TAB: {
                // 點擊校正後的 HP 分頁座標
                ClickCoord(s_hwnd, CalibIndex::SHOP_TAB_HP);
                Logf("DM", "📑 點擊 HP 分頁 (SHOP_TAB_HP)");
                NextPhase();
                break;
            }

            // ============================================================
            // Phase 6: 點擊物品格子選擇 HP 水
            // ============================================================
            case SupplyPhase::SELECT_ITEM: {
                // 點擊校正後的物品格子座標
                ClickCoord(s_hwnd, CalibIndex::SHOP_ITEM_SLOT);
                Logf("DM", "✅ 點擊物品格子 (SHOP_ITEM_SLOT)");
                NextPhase();
                break;
            }

            // ============================================================
            // Phase 7: 點擊數量輸入框並輸入數量
            // ============================================================
            case SupplyPhase::CONFIRM_SELECT: {
                // 等待一下讓數量框出現
                Sleep(300);

                // 點擊校正後的數量輸入框座標
                ClickCoord(s_hwnd, CalibIndex::SHOP_QUANTITY_INPUT);
                Sleep(200);

                // 輸入購買數量
                SendNumberNormal(300);
                Sleep(100);

                // 點擊校正後的數量確認按鈕
                ClickCoord(s_hwnd, CalibIndex::SHOP_QUANTITY_CONFIRM);
                Logf("DM", "🔢 數量輸入完成 (SHOP_QUANTITY_INPUT → 300 → SHOP_QUANTITY_CONFIRM)");
                NextPhase();
                break;
            }

            // ============================================================
            // Phase 8: 點擊購買確認按鈕
            // ============================================================
            case SupplyPhase::INPUT_QUANTITY: {
                // 等待一下讓購買確認框出現
                Sleep(300);

                // 點擊校正後的購買確認按鈕
                ClickCoord(s_hwnd, CalibIndex::SHOP_BUY_CONFIRM);
                Logf("DM", "💰 點擊購買確認 (SHOP_BUY_CONFIRM)");
                NextPhase();
                break;
            }

            // ============================================================
            // Phase 9: 跳過賣物品
            // ============================================================
            case SupplyPhase::CONFIRM_BUY: {
                // 跳過賣物品
                Logf("DM", "📤 跳過賣物品");
                NextPhase();
                break;
            }

            // ============================================================
            // Phase 10: 跳過
            // ============================================================
            case SupplyPhase::SELL_ITEMS: {
                NextPhase();
                break;
            }

            // ============================================================
            // Phase 11: 確認賣
            // ============================================================
            case SupplyPhase::CONFIRM_SELL: {
                NextPhase();
                break;
            }

            // ============================================================
            // Phase 12: 關閉商店
            // ============================================================
            case SupplyPhase::CLOSE_SHOP: {
                // 點擊校正後的關閉按鈕
                ClickCoord(s_hwnd, CalibIndex::SHOP_CLOSE);
                Sleep(200);
                // 雙重保障：再按一次 ESC
                BYTE vk = VK_ESCAPE;
                INPUT inputs[2] = {};
                inputs[0].type = INPUT_KEYBOARD;
                inputs[0].ki.wVk = vk;
                inputs[0].ki.dwFlags = 0;
                inputs[1].type = INPUT_KEYBOARD;
                inputs[1].ki.wVk = vk;
                inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(2, inputs, sizeof(INPUT));
                Logf("DM", "🔚 關閉商店 (SHOP_CLOSE + ESC)");
                NextPhase();
                break;
            }

            // ============================================================
            // Phase 13: 完成
            // ============================================================
            case SupplyPhase::DONE: {
                // 只有找到 NPC 並完成購買才算真正完成
                if (s_npcFound) {
                    Logf("DM", "✅ 視覺供應完成 (NPC已找到並完成購買)");
                    s_supplyCompleted = true;
                } else {
                    Logf("DM", "⚠️ 視覺供應未完成 (未找到NPC)");
                    s_supplyCompleted = false;
                }
                s_phase = SupplyPhase::IDLE;
                return s_supplyCompleted;  // 返回是否真正完成
            }

            // ============================================================
            // 空閒
            // ============================================================
            case SupplyPhase::IDLE:
            default:
                break;
        }

        return false;  // 未完成
    }

    // ============================================================
    // 重置
    // ============================================================
    void Reset() {
        s_phase = SupplyPhase::IDLE;
        s_phaseStartTime = 0;
        s_retryCount = 0;
        s_buyStage = 0;
        s_supplyCompleted = false;
        s_npcFound = false;
        ResetPhaseRuntime();
    }

    // 獲取補給是否真正完成
    bool IsSupplyCompleted() {
        return s_supplyCompleted;
    }

    // 獲取補給是否失敗
    bool IsSupplyFailed() {
        return (s_phase == SupplyPhase::IDLE && !s_supplyCompleted);
    }

    // ============================================================
    // 獲取當前階段
    // ============================================================
    SupplyPhase GetCurrentPhase() {
        return s_phase;
    }

    // ============================================================
    // 配置
    // ============================================================
    void SetConfig(const SupplyConfig& cfg) {
        s_config = cfg;
    }

    SupplyConfig GetConfig() {
        return s_config;
    }

} // namespace VisualSupply
