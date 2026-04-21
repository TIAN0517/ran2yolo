#pragma once
// 截圖輔助：坐標點擊失敗時，用截圖比對確認按鈕位置

#include <windows.h>
#include <cstdint>

struct ScreenshotRef {
    const char* name;
    const uint8_t* data;  // RGBA pixels
    int w;
    int h;
    int matchThreshold;    // 0-100, 匹配相似度閾值
};

// 初始化：截圖比對初始化（截圖當前畫面）
bool ScreenshotAssist_Init();

// 釋放
void ScreenshotAssist_Shutdown();

// 在當前截圖中搜尋指定參考圖
// 返回畫面中的客戶區座標（相對座標），若找不到返回 (-1,-1)
bool ScreenshotAssist_Find(const char* refName, int* outX, int* outY, int* outScore);

// 搜尋多個候選點，返回最佳者
bool ScreenshotAssist_FindBest(const char* refName, int radius, int* outX, int* outY, int* outScore);

// 截圖並搜尋（一次完成）
bool ScreenshotAndFind(HWND hWnd, const char* refName, int* outX, int* outY, int* outScore);

// 將截圖座標（絕對畫素）轉為相對座標 0-1000
int ScreenshotToRelX(int screenX, int screenW);
int ScreenshotToRelY(int screenY, int screenH);

// 取得最後一次截圖的解析度
int GetScreenshotWH(int* outW, int* outH);
