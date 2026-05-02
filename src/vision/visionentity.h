// ============================================================
// visionentity.h
// 視覺實體掃描模組
// 使用統一截圖 + 像素掃描
// ============================================================
#pragma once
#include <windows.h>

// ============================================================
// 視覺怪物結構
// ============================================================
struct VisualMonster {
    int screenX = 0;      // 螢幕座標 X
    int screenY = 0;      // 螢幕座標 Y
    int relX = 0;        // 遊戲相對座標 X (0-1023)
    int relY = 0;        // 遊戲相對座標 Y (0-767)
    int hpPct = 100;     // 血量百分比 0-100
    int priority = 0;   // 優先級（值越大越優先）
    int width = 0;       // 血條寬度
};

// ============================================================
// 視覺讀取的玩家狀態
// ============================================================
struct VisualPlayerState {
    int hpPct = 100;
    int mpPct = 100;
    int spPct = 100;
    int mapId = 0;
    int screenX = 0;
    int screenY = 0;
    int gold = 0;
    bool found = false;
};

// ============================================================
// 函式宣告
// ============================================================

// 掃描畫面所有怪物血條
int ScanVisualMonsters(HWND hWnd, VisualMonster* outMonsters, int maxMonsters);

// 讀取視覺玩家狀態
bool ReadVisualPlayerState(HWND hWnd, VisualPlayerState* outState);

// 估算怪物優先級
int EstimateMonsterPriority(const VisualMonster* m, int screenH);

// 對怪物列表按優先級排序
void SortVisualMonsters(VisualMonster* monsters, int count);
