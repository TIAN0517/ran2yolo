#pragma once
// ============================================================
// 視覺實體掃描模組（Win7~Win11 通用）
// 純像素掃描取代記憶體讀取
// ============================================================
#include <windows.h>

// ============================================================
// 視覺怪物結構
// ============================================================
struct VisualMonster {
    int screenX = 0;      // 螢幕座標 X
    int screenY = 0;      // 螢幕座標 Y
    int relX = 0;        // 遊戲相對座標 X (0-1023)
    int relY = 0;         // 遊戲相對座標 Y (0-767)
    int hpPct = 100;     // 血量百分比 0-100
    int priority = 0;    // 優先級（值越大越優先）
    int width = 0;       // 血條寬度
};

// ============================================================
// 視覺讀取的玩家狀態（取代記憶體讀值）
// ============================================================
struct VisualPlayerState {
    int hpPct = 100;       // HP 百分比 0-100
    int mpPct = 100;       // MP 百分比 0-100
    int spPct = 100;       // SP 百分比 0-100
    int mapId = 0;         // 地圖 ID（從視覺辨識得到）
    int screenX = 0;       // 玩家畫面座標
    int screenY = 0;
    int gold = 0;          // 金幣（像素計數估算）
    bool found = false;    // 是否成功讀到
};

// ============================================================
// 函式宣告
// ============================================================

// 掃描畫面所有怪物血條（取代記憶體實體池掃描）
// 原理：從上往下、從左往右掃描 RGBA 像素
// 條件：R > 200 且 G < 80 且 B < 80 的連續像素列（寬 3-5px，高 8-15px）= 血條
// 返回找到的怪物數量
int ScanVisualMonsters(HWND hWnd, VisualMonster* outMonsters, int maxMonsters);

// 讀取視覺玩家狀態（HP/MP/SP 百分比）
bool ReadVisualPlayerState(HWND hWnd, VisualPlayerState* outState);

// 估算怪物優先級（y 越大越近優先）
int EstimateMonsterPriority(const VisualMonster* m, int screenH);

// 對怪物列表按優先級排序
void SortVisualMonsters(VisualMonster* monsters, int count);
