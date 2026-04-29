#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// 授權管理工具 - 前向宣告
extern HWND g_adminHwnd;

// 初始化 GUI
bool InitLicenseAdminGui(HINSTANCE hInstance, int nCmdShow);

// 視窗訊息處理
LRESULT CALLBACK AdminWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
