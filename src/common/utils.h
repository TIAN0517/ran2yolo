#pragma once
#ifndef UTILS_H
#define UTILS_H

#include <windows.h>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

// ============================================================
// Throttle / Cooldown Helper
// 避免重複記錄/打印同一訊息（節流器）
// ============================================================
class Throttle {
public:
    Throttle(DWORD intervalMs = 1000) : m_interval(intervalMs), m_last(0) {}

    void SetInterval(DWORD ms) { m_interval = ms; }

    // 是否允許通過（間隔是否已過）
    bool CanPass() {
        DWORD now = GetTickCount();
        if (now - m_last >= m_interval) {
            m_last = now;
            return true;
        }
        return false;
    }

    // 強制觸發（不論間隔）
    void Force() { m_last = 0; }

    // 重置計時
    void Reset() { m_last = GetTickCount(); }

    // 獲取剩餘等待時間
    DWORD RemainingMs() const {
        DWORD elapsed = GetTickCount() - m_last;
        if (elapsed >= m_interval) return 0;
        return m_interval - elapsed;
    }

private:
    DWORD m_interval;
    DWORD m_last;
};

// ============================================================
// 快速 Throttle 巨集（避免宣告成員變數）
// ============================================================
#define THROTTLE(var, intervalMs) \
    static DWORD var = 0; \
    DWORD now_##var = GetTickCount(); \
    if (now_##var - var > intervalMs)

// ============================================================
// 座標轉換工具（已移至 coord_calib.h 的 CoordConv 命名空間）

// ============================================================
// 視窗工具
// ============================================================
namespace WinUtil {
    // 檢查視窗是否有效
    inline bool IsValidWindow(HWND hWnd) {
        return hWnd && IsWindow(hWnd);
    }

    // 獲取視窗標題
    inline void GetWindowTitle(HWND hWnd, char* outBuf, int bufSize) {
        if (!hWnd || !IsWindow(hWnd) || !outBuf || bufSize <= 0) {
            if (outBuf && bufSize > 0) outBuf[0] = '\0';
            return;
        }
        int len = GetWindowTextA(hWnd, outBuf, bufSize - 1);
        outBuf[len] = '\0';
    }

    // 檢查視窗是否最小化
    inline bool IsMinimized(HWND hWnd) {
        if (!hWnd || !IsWindow(hWnd)) return true;
        return IsIconic(hWnd) != 0;
    }

    // 檢查視窗是否最大化
    inline bool IsMaximized(HWND hWnd) {
        if (!hWnd || !IsWindow(hWnd)) return false;
        return IsZoomed(hWnd) != 0;
    }

    // 獲取視窗顯示狀態
    inline bool IsVisible(HWND hWnd) {
        if (!hWnd || !IsWindow(hWnd)) return false;
        return IsWindowVisible(hWnd) != 0;
    }

    // 獲取前景視窗
    inline HWND GetForegroundWindowSafe() {
        HWND hFg = GetForegroundWindow();
        if (!hFg || !IsWindow(hFg)) return NULL;
        return hFg;
    }

    // 嘗試將視窗拉到前台
    inline bool BringToFront(HWND hWnd) {
        if (!hWnd || !IsWindow(hWnd)) return false;

        // 先嘗試 SetForegroundWindow
        if (SetForegroundWindow(hWnd)) return true;

        // 失敗時嘗試 AttachThreadInput
        HWND hFg = GetForegroundWindow();
        if (!hFg) return false;

        DWORD fgThread = GetWindowThreadProcessId(hFg, NULL);
        DWORD myThread = GetCurrentThreadId();

        if (fgThread == myThread) return true;

        AttachThreadInput(myThread, fgThread, TRUE);
        SetForegroundWindow(hWnd);
        AttachThreadInput(myThread, fgThread, FALSE);

        return true;
    }
}

// ============================================================
// 時間工具
// ============================================================
namespace TimeUtil {
    // 獲取自系統啟動後的毫秒數
    inline DWORD NowMs() { return GetTickCount(); }

    // 檢查是否已過指定時間
    inline bool HasElapsed(DWORD startTime, DWORD intervalMs) {
        return (NowMs() - startTime) >= intervalMs;
    }

    // 計算已耗時間
    inline DWORD ElapsedMs(DWORD startTime) {
        return NowMs() - startTime;
    }
}

// ============================================================
// 延遲工具
// ============================================================
namespace DelayUtil {
    // 標準延遲（包裝 Sleep）
    inline void Ms(DWORD ms) { Sleep(ms); }

    // 隨機延遲（防重複模式）
    inline void Jitter(DWORD baseMs, DWORD jitterRange = 20) {
        DWORD jitter = jitterRange > 0 ? (rand() % jitterRange) : 0;
        Sleep(baseMs + jitter);
    }

    // 等待直到超時或條件滿足
    // 回傳: true = 條件在超時前滿足, false = 超時
    inline bool WaitFor(DWORD timeoutMs, std::function<bool()> condition, DWORD checkIntervalMs = 50) {
        DWORD start = TimeUtil::NowMs();
        while (!condition()) {
            if (TimeUtil::HasElapsed(start, timeoutMs)) return false;
            Sleep(checkIntervalMs);
        }
        return true;
    }
}

// ============================================================
// 控制碼防重（避免按鍵/點擊重複觸發）
// ============================================================
class CooldownTracker {
public:
    CooldownTracker(DWORD defaultIntervalMs = 500) : m_default(defaultIntervalMs) {}

    // 檢查是否可以觸發（帶預設冷卻時間）
    bool Try(const char* actionName = nullptr) {
        return Try(0, m_default, actionName);
    }

    // 檢查是否可以觸發（指定冷卻時間）
    bool Try(DWORD cooldownMs, const char* actionName = nullptr) {
        return Try(0, cooldownMs, actionName);
    }

    // 檢查是否可以觸發（指定時間範圍內）
    bool Try(DWORD startTime, DWORD cooldownMs, const char* actionName = nullptr) {
        DWORD now = TimeUtil::NowMs();
        if (actionName) {
            // 具名操作：每個名稱獨立追蹤
            DWORD& last = m_namedLast[actionName];
            if (now - last < cooldownMs) return false;
            last = now;
        } else {
            // 無名操作：使用單一共用時間戳
            if (startTime == 0) {
                if (now - m_lastGeneric < cooldownMs) return false;
                m_lastGeneric = now;
            } else {
                if (now - startTime < cooldownMs) return false;
            }
        }
        return true;
    }

    // 強制重置
    void Reset() { m_lastGeneric = 0; m_namedLast.clear(); }

    void SetDefaultInterval(DWORD ms) { m_default = ms; }

private:
    DWORD m_default;
    DWORD m_lastGeneric = 0;
    std::map<std::string, DWORD> m_namedLast;
};

#endif // UTILS_H