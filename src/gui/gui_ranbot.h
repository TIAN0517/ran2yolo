#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// GUI 全域
extern bool g_guiVisible;
extern HWND g_hWnd;

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

// 平台
extern bool IsWin7OrEarlier();
