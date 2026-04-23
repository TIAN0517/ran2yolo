#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// ============================================================
// 遊戲進程管理（外部模式）
// FindWindow + OpenProcess (非注入，安全穩定)
// x86/x64 雙支援
// ============================================================

// 統一地址類型 (x86/x64 兼容)
#ifdef _WIN64
    typedef DWORD64 ADDR;
    #define ADDR_FORMAT "0x%016llX"
#else
    typedef DWORD ADDR;
    #define ADDR_FORMAT "0x%08X"
#endif

struct GameHandle {
    DWORD   pid       = 0;      // 程序 ID
    HANDLE  hProcess = NULL;    // 進程句柄
    HWND    hWnd     = NULL;    // 視窗句柄
    ADDR    baseAddr = 0;       // Game.exe 模組基底 (x64 下為 64 位)
    bool    attached  = false;   // 是否已附加
};

// 找到遊戲視窗和進程
extern bool FindGameProcess(GameHandle* gh);

// 關閉進程 handle
extern void CloseGameHandle(GameHandle* gh);

// 檢查遊戲是否還在執行
extern bool IsGameRunning(GameHandle* gh);

// 取得客戶區域解析度
extern bool GetGameClientSize(GameHandle* gh, int* w, int* h);
extern bool EnsureGameClientSize(GameHandle* gh, int targetW, int targetH);

// 刷新遊戲模組基址（傳入現有 GameHandle，不重新 OpenProcess）
extern ADDR RefreshGameBaseAddress(GameHandle* gh);

// 取得遊戲主模組基址（內部使用）
extern ADDR GetGameBaseAddress(GameHandle* gh);

// 全域遊戲句柄（BotThread 寫入，GUI 唯讀）
extern GameHandle GetGameHandle();
extern void SetGameHandle(GameHandle* gh);
