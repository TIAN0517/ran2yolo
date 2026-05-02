// ============================================================
// dm_visual.cpp
// 大漠插件視覺辨識模組實現（已禁用視覺模式）
// ============================================================

#include "pch.h"
#include "dm_visual.h"
#include "../embed/dm_plugin.h"
#include "../input/input_sender.h"
#include "../core/bot_logic.h"
#include "../game/game_process.h"
#include "../game/memory_reader.h"
#include "../config/offset_config.h"

// ============================================================
// 地圖圖檔
// ============================================================
static const char* g_mapImages[] = {
    "",                                    // UNKNOWN
    "map_shengmen.png",                    // SHENGMEN
    "map_shangdong.png",                   // SHANGDONG
    "map_xuan.png",                        // XUANYAN
    "map_fenghuang.png"                    // FENGHUANG
};

namespace DMVisual {

    static bool s_dmInited = false;

    // ============================================================
    // 初始化（已禁用視覺模式，不再初始化 DM）
    // ============================================================
    bool Init() {
        // 視覺模式已禁用，不初始化大漠插件
        s_dmInited = false;  // 明確標記為未初始化
        Logf("DM", "⚠️ 視覺辨識已禁用（不再使用圖檔）");
        return false;  // 返回 false 表示不使用視覺模式
    }

    // ============================================================
    // 找圖並返回座標（已禁用視覺模式，直接返回失敗）
    // ============================================================
    FindResult FindPic(const char* imageName, int left, int top,
                       int right, int bottom, double sim) {
        FindResult result;
        // 視覺模式已禁用，不執行任何圖檔掃描
        result.found = false;
        result.x = -1;
        result.y = -1;
        return result;
    }

    // ============================================================
    // 點擊座標（使用 SendInput，DirectX 相容）
    // ============================================================
    void ClickAt(int x, int y) {
        if (x < 0 || y < 0) return;

        HWND hwnd = GetForegroundWindow();  // Fallback to foreground window
        if (!hwnd) return;

        // 轉換為螢幕座標
        POINT pt = { x, y };
        ClientToScreen(hwnd, &pt);

        // 發送左鍵點擊
        int absX = (int)((pt.x * 65535) / GetSystemMetrics(SM_CXSCREEN));
        int absY = (int)((pt.y * 65535) / GetSystemMetrics(SM_CYSCREEN));

        INPUT inputs[3] = {};
        inputs[0].type = INPUT_MOUSE;
        inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        inputs[0].mi.dx = absX;
        inputs[0].mi.dy = absY;

        inputs[1].type = INPUT_MOUSE;
        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE;
        inputs[1].mi.dx = absX;
        inputs[1].mi.dy = absY;

        inputs[2].type = INPUT_MOUSE;
        inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE;
        inputs[2].mi.dx = absX;
        inputs[2].mi.dy = absY;

        SendInput(3, inputs, sizeof(INPUT));
    }

    // ============================================================
    // 辨識地圖
    // ============================================================
    GameMap DetectMap() {
        for (int i = (int)GameMap::SHENGMEN; i <= (int)GameMap::FENGHUANG; i++) {
            FindResult r = FindPic(g_mapImages[i], 0, 0, 200, 100, 0.8);
            if (r.found) {
                Logf("DM", "📍 偵測地圖: %s", g_mapImages[i]);
                return (GameMap)i;
            }
        }
        return GameMap::UNKNOWN;
    }

    // ============================================================
    // 找怪物血條（用於鎖定）
    // ============================================================
    FindResult FindMonsterHP() {
        return FindPic("mob_hp.png", 0, 0, 1000, 1000, 0.75);
    }

    // ============================================================
    // 找玩家血條
    // ============================================================
    FindResult FindPlayerHP() {
        return FindPic("Player_pkt.png", 0, 0, 1000, 1000, 0.75);
    }

    // ============================================================
    // 找箭矢
    // ============================================================
    FindResult FindArrow() {
        return FindPic("arrow.png", 0, 0, 1000, 1000, 0.85);
    }

    // ============================================================
    // 找歸魂珠
    // ============================================================
    FindResult FindSoulPearl() {
        return FindPic("Soul_Returning_Pearl.png", 0, 0, 1000, 1000, 0.85);
    }

    // ============================================================
    // 找背包物品
    // ============================================================
    FindResult FindInventoryItem() {
        return FindPic("背包前五格.png", 0, 0, 1000, 1000, 0.85);
    }

    // ============================================================
    // 找商店購買介面
    // ============================================================
    FindResult FindBuyArrow() {
        return FindPic("購買箭矢.png", 0, 0, 1000, 1000, 0.85);
    }

    FindResult FindBuyArrowSelect() {
        return FindPic("購買箭矢選擇.png", 0, 0, 1000, 1000, 0.85);
    }

    FindResult FindBuyArrowYes() {
        return FindPic("購買箭矢選擇是.png", 0, 0, 1000, 1000, 0.85);
    }

    // ============================================================
    // 找 NPC（按城鎮）
    // ============================================================
    FindResult FindNPC_聖門() {
        return FindPic("聖門npc.png", 0, 0, 1000, 1000, 0.85);
    }

    FindResult FindNPC_商洞() {
        return FindPic("商洞npc.png", 0, 0, 1000, 1000, 0.85);
    }

    FindResult FindNPC_玄巖() {
        return FindPic("玄巖npc.png", 0, 0, 1000, 1000, 0.85);
    }

    FindResult FindNPC_鳳凰() {
        return FindPic("鳳凰npc.png", 0, 0, 1000, 1000, 0.85);
    }

    // ============================================================
    // 除錯測試（只執行一次）
    // ============================================================
    bool TestImageFind() {
        static bool s_tested = false;
        if (s_tested) return true;
        s_tested = true;

        Logf("DM", "🔍 [DM測試] 測試 buy.png...");
        FindResult r = FindPic("buy.png", 0, 0, 1000, 1000, 0.7);
        Logf("DM", "🔍 [DM測試] buy.png found=%d x=%d y=%d", r.found, r.x, r.y);

        Logf("DM", "🔍 [DM測試] 測試 聖門npc.png...");
        r = FindPic("聖門npc.png", 0, 0, 1000, 1000, 0.85);
        Logf("DM", "🔍 [DM測試] 聖門npc.png found=%d x=%d y=%d", r.found, r.x, r.y);

        return true;
    }

    // ============================================================
    // 通用 NPC 找圖（自動嘗試所有城鎮）
    // ============================================================
    FindResult FindNPC_Any() {
        FindResult r = FindNPC_聖門();
        if (r.found) return r;
        r = FindNPC_商洞();
        if (r.found) return r;
        r = FindNPC_玄巖();
        if (r.found) return r;
        r = FindNPC_鳳凰();
        return r;
    }

    // ============================================================
    // 購買水/藥水介面
    // ============================================================
    FindResult FindBuyWater() {
        return FindPic("購買水數量.png", 0, 0, 1000, 1000, 0.85);
    }

    FindResult FindBuyWaterConfirm() {
        return FindPic("購買水確認.png", 0, 0, 1000, 1000, 0.85);
    }

    // ============================================================
    // 賣物
    // ============================================================
    FindResult FindSellItem() {
        return FindPic("拖一+賣.png", 0, 0, 1000, 1000, 0.85);
    }

    // ============================================================
    // 關閉商店（按 ESC x2，不影響其他狀態）
    // ============================================================
    void CloseShop() {
        // 只發送 ESC 鍵，不影響 Bot 狀態機
        keybd_event(VK_ESCAPE, 0, 0, 0);
        Sleep(100);
        keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
        Sleep(100);
        keybd_event(VK_ESCAPE, 0, 0, 0);
        Sleep(100);
        keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
        Logf("DM", "🔚 關閉商店 (ESC x2)");
    }

} // namespace DMVisual
