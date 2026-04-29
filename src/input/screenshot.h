// ============================================================
// screenshot.h - 統一截圖模組
// 僅使用 PrintWindow + PW_RENDERFULLCONTENT 穿透硬體加速黑屏
// ============================================================
#pragma once

#include <windows.h>
#include <vector>
#include <cstdint>

// 截圖並存入 outPixels (BGRA format)
// 使用 PrintWindow + PW_RENDERFULLCONTENT (3)
bool CaptureGameWindow(HWND hWnd, std::vector<uint8_t>& outPixels, int outW, int outH);

// 取得客戶區大小
void GetWindowClientSize(HWND hWnd, int* outW, int* outH);

// 診斷截圖保存
bool SaveDiagnosticScreenshot(HWND hWnd, const char* filename);

// 座標轉換
int ScreenToRelX(int screenX, int screenW);
int ScreenToRelY(int screenY, int screenH);

// ============================================================
// Legacy Compatibility: CaptureWindowClient
// 直接截圖並返回 HDC（用於目標鎖定）
// ============================================================
HDC CaptureWindowClient(HWND hWnd, HDC* outMemDC, HBITMAP* outBmp, int* outW, int* outH);

// 釋放 CaptureWindowClient 分配的資源
void ReleaseCapture(HDC hMemDC, HBITMAP hBmp);
