// ============================================================
// screenshot_universal.h
// 統一截圖介面 - 僅使用 PrintWindow + PW_RENDERFULLCONTENT
// 穿透 DWM / 硬體加速黑屏
// ============================================================
#pragma once
#ifndef _SCREENSHOT_UNIVERSAL_H_
#define _SCREENSHOT_UNIVERSAL_H_

#include <windows.h>
#include <vector>
#include <cstdint>

// ============================================================
// Core Capture Functions
// ============================================================
namespace ScreenshotUniversal {

// ============================================================
// Capture Result
// ============================================================
struct CaptureResult {
    bool success = false;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // BGRA format
};

// ============================================================
// Coordinate Systems (1000x1000)
// ============================================================
struct GameCoords {
    int x = 0;
    int y = 0;
};

struct ScreenCoords {
    int x = 0;
    int y = 0;
};

// Initialize
bool Init(HWND hWnd);

// Capture using PrintWindow + PW_RENDERFULLCONTENT (3)
CaptureResult Capture(HWND hWnd);

// Capture RGBA (alias)
CaptureResult CaptureRGBA(HWND hWnd);

// Release
void Shutdown();

// Coordinate Conversion
GameCoords ScreenToGame(int screenX, int screenY, HWND hWnd);
ScreenCoords GameToScreen(int gameX, int gameY, HWND hWnd);

// DPI
float GetDPIScaleX(HWND hWnd);
float GetDPIScaleY(HWND hWnd);

// Client size
void GetClientSize(HWND hWnd, int* outW, int* outH);

// Save screenshot
bool SaveScreenshot(const char* filepath, const CaptureResult& cap);

// Diagnostic capture
void DiagnosticCapture(HWND hWnd);

} // namespace ScreenshotUniversal

#endif // _SCREENSHOT_UNIVERSAL_H_
