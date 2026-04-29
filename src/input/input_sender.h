#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "coord_calib.h"

// ============================================================
// 外部輸入模擬
// ============================================================

struct GameHandle; // 前向宣告

// 按鍵按下 + 放開（SendInput + 確保前景，DirectX 遊戲適用）
extern void SendKeyDirect(HWND hWnd, BYTE vk);

// 快速攻擊鍵：無 Focus，直接發送 PostMessage（避免黑屏）
extern void SendKeyAttack(HWND hWnd, BYTE vk);

// 快速按鍵（無 Focus/Sleep 延遲，用於高頻技能施放）
extern void SendKeyFast(BYTE vk);

// 快速右鍵點擊（前景模式，直接定位，不做平滑拖曳）
extern void RClickFast(HWND hWnd, int cx, int cy);

// 按鍵按下 + 放開（SendInput，並自動將目標視窗拉到前景再發送）
extern void SendKeyInputFocused(BYTE vk, HWND hWnd);

// 按鍵按下 + 放開（純背景 PostMessage，Win11 專用）
extern void SendKeyDownUp(HWND hWnd, BYTE vk);

// 滑鼠左鍵點擊（遊戲 client 內相對座標；不移動系統游標）
extern void ClickAt(HWND hWnd, int cx, int cy);

// 滑鼠左鍵雙擊
extern void DClickAt(HWND hWnd, int cx, int cy);

// 滑鼠右鍵點擊
extern void RClickAt(HWND hWnd, int cx, int cy);

// 滑鼠右鍵點擊（前景驗證版，移動游標）
extern void RClickAtDirect(HWND hWnd, int cx, int cy);

// 滑鼠點擊（通用版，支援相對座標，Win10/11 使用 PostMessage）
extern void SendMouseClickDirect(HWND hWnd, int cx, int cy, DWORD downFlag, DWORD upFlag);

// 滑鼠左鍵點擊（前景驗證版，移動游標）
extern void ClickAtDirect(HWND hWnd, int cx, int cy);

// 右鍵點擊（盲目賣物用）
extern void ClickAtDirectRight(HWND hWnd, int cx, int cy);

// 帶校正覆寫的左鍵點擊（CalibIndex 查 CoordCalibrator 自動套用覆寫）
extern void ClickAtCalib(HWND hWnd, int cx, int cy, CalibIndex idx);

// 帶校正覆寫的右鍵點擊（自動查攻擊定點的校正值，沒設過就用原始值）
extern void ClickAttackPoint(HWND hWnd, int pointIndex);

// 取得攻擊點座標（用於 RClickAt 同步）
extern void GetAttackPointCoords(int pointIndex, int* outX, int* outY);

// 拖曳：左鍵按住移動後右鍵釋放（寵物餵食用）
extern void DragFeedToPet(HWND hWnd, int fromX, int fromZ, int toX, int toZ);

// 將遊戲視窗帶到前景（SetForegroundWindow + AttachThreadInput）
extern bool FocusGameWindow(HWND hWnd);

// 檢查視窗是否為已知的遊戲視窗（用於驗證輸入目標）
extern bool IsGameWindow(HWND hWnd);

// 延遲（毫秒，替代 Sleep）
extern void Delay(int ms);

// 釋放所有行走鍵（WASD）PostMessage 版
extern void ReleaseAllMovementKeysPost(HWND hWnd);

// 輸入數字（用於買藥數量輸入）
extern void TypeNumber(HWND hWnd, int number);

// Ctrl+A 全選目前輸入框內容
extern void SendCtrlA(HWND hWnd);

// 平台檢測（運行時自動判斷 Win7 vs Win10/11）
extern bool IsWin7Platform();
extern void InitPlatformDetect();

// 平滑鼠標移動開關（預設開啟）
extern void SetSmoothMouseEnabled(bool enabled);

// ============================================================
// 精準座標轉換（解決亂起點問題）
// ============================================================

// 遊戲相對座標 (0-1000) → 螢幕絕對座標
// 返回 true = 轉換成功
extern bool GameCoordsToScreen(HWND hWnd, int gameX, int gameZ, POINT* outScreen);

// 螢幕絕對座標 → 遊戲相對座標 (0-1000)
// 返回 true = 轉換成功
extern bool ScreenToGameCoords(HWND hWnd, int screenX, int screenY, int* outGameX, int* outGameZ);

// 取得遊戲視窗客戶區大小
extern void GetGameClientSize(HWND hWnd, int* outW, int* outH);
