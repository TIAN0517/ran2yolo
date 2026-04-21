#pragma once
// ============================================================
// Win32 GDI 截圖模組（Win7~Win11 通用）
// ============================================================
#include <windows.h>
#include <vector>

// 取得遊戲視窗客戶區域的像素顏色
// 返回 RGB 值 (0-255)，失敗返回 -1
int GetPixelColor(HWND hWnd, int screenX, int screenY);

// 截圖：抓取整個視窗客戶區域到 HDC
// 調用者負責釋放 HDC (DeleteDC)
// 返回 NULL 表示失敗
HDC CaptureWindowClient(HWND hWnd, HDC* outMemDC, HBITMAP* outBitmap, int* outW, int* outH);

// 從 HDC 讀取指定座標的像素 RGB
// hdcDC = GetPixelColor(hMemDC, x, y) 讀取
// 返回 RGB 打包值 (R<<16 | G<<8 | B)，失敗返回 -1
COLORREF GetPixelRGB(HDC hdcDC, int x, int y);

// 釋放截圖資源（截圖後必須調用）
void ReleaseCapture(HDC hdcMem, HBITMAP hBitmap);

// 在 hdcDC 上讀取以 (cx, cz) 為中心、半徑 r 範圍內的像素
// 搜尋「紅色 HP 條」：R > 200, G < 50, B < 50
// 返回第一個找到的怪物位置，沒有找到返回 {-1, -1}
POINT FindMonsterHPBar(HDC hdcDC, int cx, int cz, int radius);

// 搜尋多個採樣點，找到第一個有 HP 條的位置
// scanPoints: 陣列座標，count: 數量，scanRadius: 每點掃描半徑
// 返回 {-1, -1} 表示都沒找到
POINT ScanForMonsters(HDC hdcDC, const POINT* scanPoints, int count, int scanRadius);

// ============================================================
// 座標校正巨集：截圖 + 標記點擊位置
// phaseName: 階段名稱（如 "Phase3_Tab", "Phase3_BuyHP"）
// markers: 標記點座標数组
// markerLabels: 對應標記名稱
// count: 標記數量
// 保存到: {exe目錄}\shop_debug\{phaseName}_{timestamp].bmp
// ============================================================
bool SaveCalibrationScreenshot(HWND hWnd, const char* phaseName,
    const POINT* markers, const char** markerLabels, int count);

// ============================================================
// 視覺辨識專用截圖（Win7~Win11 通用）
// 使用 PrintWindow + DIB Section，取代 BitBlt
// ============================================================

// 截圖並存入 std::vector<uint8_t>（RGBA 格式，用於後續處理）
// Win7/Win11 通用，自動處理不同顯卡驅動兼容性
// 返回 true 表示成功，outPixels 大小為 w*h*4
bool CaptureGameWindow(HWND hWnd, std::vector<uint8_t>& outPixels, int outW, int outH);

// 讀取 HP/MP/SP 條百分比（純像素計數，不需要 OCR）
// 原理：截取狀態列區域，計算紅色 bar / 藍色 bar 的寬度比例
// HP 條：紅色 (R>200, G<80, B<80)
// MP 條：藍色 (R<80, G<80, B>200)
// SP 條：綠色 (R<80, G>200, B<80)
// 返回 true 表示成功，outHpPct/outMpPct 為 0-100
bool ReadHPMPSPBars(HWND hWnd, int* outHpPct, int* outMpPct, int* outSpPct);
