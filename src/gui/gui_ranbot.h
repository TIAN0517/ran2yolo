#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// GUI 全域
extern bool g_guiVisible;
extern HWND g_hWnd;

// 全局熱鍵 ID（必須與 main.cpp 中的 RegisterHotKey 一致）
#define HOTKEY_F9   1
#define HOTKEY_F10  2
#define HOTKEY_F11  3
#define HOTKEY_F12  4

// 量尺抓取模式（供 main.cpp WM_HOTKEY 使用）
extern volatile long s_captureMode;
extern volatile long s_captureFieldId;
extern HHOOK s_hMouseHookCapture;

// 熱鍵
extern void InitHotkeys();
extern void ShutdownHotkeys();

// 日誌
extern void UIAddLog(const char* fmt, ...);
extern void FlushAllLogs();

// 面板
extern void RenderMainGUI();

// 校正
extern void SetGameHwndForCalib(HWND hwnd);

// ═══════════════ F9 量尺座標抓取 ═══════════════
// 進入抓取模式，下一次游戲窗口點擊會自動填入目標輸入框
extern void EnterCoordCaptureMode(int fieldId);
// 離開抓取模式（取消）
extern void CancelCoordCaptureMode();
// 取得當前是否在抓取模式
extern bool IsCoordCaptureActive();
// 取得目前瞄準的 fieldId（0=無）
extern int  GetCaptureTargetFieldId();
// 路由被捕獲的座標到對應的輸入框（由 main.cpp WndProc 呼叫）
extern void RouteCapturedCoord(int fieldId, int x, int y);
