#pragma once
#include <windows.h>

struct TargetLockResult
{
    bool  found;
    POINT gamePt;     // 1024x768 遊戲內相對座標
    POINT rawPt;      // 截圖內原始座標
    int   score;
    char  reason[128];
};

void TargetLock_SetDebug(bool enabled);
bool TargetLock_Find(HWND hWnd, TargetLockResult* outResult);
bool TargetLock_Click(HWND hWnd, TargetLockResult* outResult);

// 記憶體鎖定檢測：使用 ObjectLocked 偏移
// 返回 true = 已鎖定目標, false = 未鎖定
bool IsTargetLockedByMemory(HANDLE hProcess, DWORD baseAddr);
