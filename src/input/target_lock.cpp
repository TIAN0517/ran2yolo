#include "target_lock.h"
#include "screenshot.h"
#include "input_sender.h"
#include "../config/coords.h"
#include "../config/offset_config.h"
#include "../platform/coord_calib.h"  // CoordConv 統一座標轉換
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>

static bool s_targetLockDebug = false;

void TargetLock_SetDebug(bool enabled)
{
    s_targetLockDebug = enabled;
}

static void TL_Log(const char* fmt, ...)
{
    if (!s_targetLockDebug) return;

    char buf[512] = {};
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hCon && hCon != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        char line[560] = {};
        sprintf_s(line, "[TargetLock] %s\n", buf);
        WriteFile(hCon, line, (DWORD)strlen(line), &written, NULL);
    }
}

static inline int ClampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline bool IsMonsterHpColor(COLORREF c)
{
    const int r = GetRValue(c);
    const int g = GetGValue(c);
    const int b = GetBValue(c);

    // 乱2怪物血條：R極高、綠藍極少
    // 實測值：255,0,0 (純紅) 和 165,1,1 (深紅)
    // 門檻：R >= 160, G <= 5, B <= 5
    return (r >= 160 && g <= 5 && b <= 5);
}

static bool HasMonsterBodyBelow(HDC hdc, int px, int py, int w, int h)
{
    const int bodyTop    = py + 2;
    const int bodyBottom = py + 22;
    const int bodyLeft   = px - 14;
    const int bodyRight  = px + 14;

    int nonBgCount = 0;

    for (int y = bodyTop; y <= bodyBottom; ++y) {
        if (y < 0 || y >= h) continue;

        for (int x = bodyLeft; x <= bodyRight; ++x) {
            if (x < 0 || x >= w) continue;

            COLORREF c = GetPixel(hdc, x, y);
            if (c == CLR_INVALID) continue;

            const int r = GetRValue(c);
            const int g = GetGValue(c);
            const int b = GetBValue(c);

            const bool nearBlack = (r < 18 && g < 18 && b < 18);
            const bool nearWhite = (r > 220 && g > 220 && b > 220);
            const bool hpColor   = IsMonsterHpColor(c);

            if (!nearBlack && !nearWhite && !hpColor) {
                ++nonBgCount;
                if (nonBgCount >= 8) {
                    return true;
                }
            }
        }
    }

    return false;
}

static int ScoreCandidate(int px, int py, int w, int h, COLORREF c)
{
    const int centerX = w / 2;
    const int centerY = h / 2;

    const int dx = px - centerX;
    const int dy = py - centerY;
    const int dist = (int)std::sqrt((double)(dx * dx + dy * dy));

    const int r = GetRValue(c);
    const int g = GetGValue(c);
    const int b = GetBValue(c);

    int score = 10000;
    score -= dist * 6;
    score += r * 8;
    score -= g * 2;
    score -= b * 2;

    return score;
}

static bool SearchAroundPoint(
    HDC hdc,
    int w,
    int h,
    int cx,
    int cy,
    int radius,
    POINT* outBestPt,
    int* outBestScore)
{
    if (!hdc || !outBestPt || !outBestScore) return false;

    bool found = false;

    const int left   = ClampInt(cx - radius, 0, w - 1);
    const int right  = ClampInt(cx + radius, 0, w - 1);
    const int top    = ClampInt(cy - radius, 0, h - 1);
    const int bottom = ClampInt(cy + radius, 0, h - 1);

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            COLORREF c = GetPixel(hdc, x, y);
            if (c == CLR_INVALID) continue;
            if (!IsMonsterHpColor(c)) continue;
            if (!HasMonsterBodyBelow(hdc, x, y, w, h)) continue;

            const int score = ScoreCandidate(x, y, w, h, c);
            if (!found || score > *outBestScore) {
                outBestPt->x = x;
                outBestPt->y = y;
                *outBestScore = score;
                found = true;
            }
        }
    }

    return found;
}

static bool SearchByAttackScanPoints(
    HDC hdc,
    int w,
    int h,
    POINT* outBestPt,
    int* outBestScore)
{
    bool found = false;
    const Coords::ScanPoint* scanPoints = Coords::GetAttackScanPoints();

    for (int i = 0; i < Coords::ATTACK_SCAN_COUNT; ++i) {
        int cx, cy;
        CoordConv::RelToClient(scanPoints[i].x, scanPoints[i].z, &cx, &cy, w, h);

        POINT pt = { 0, 0 };
        int score = 0;

        if (SearchAroundPoint(hdc, w, h, cx, cy, 60, &pt, &score)) {
            if (!found || score > *outBestScore) {
                *outBestPt = pt;
                *outBestScore = score;
                found = true;
            }
        }
    }

    return found;
}

static bool SearchByFullTopHalf(
    HDC hdc,
    int w,
    int h,
    POINT* outBestPt,
    int* outBestScore)
{
    bool found = false;

    const int scanTop = 20;
    const int scanBottom = ClampInt(h / 2, 0, h - 1);

    for (int y = scanTop; y < scanBottom; y += 2) {
        for (int x = 10; x < w - 10; x += 2) {
            COLORREF c = GetPixel(hdc, x, y);
            if (c == CLR_INVALID) continue;
            if (!IsMonsterHpColor(c)) continue;
            if (!HasMonsterBodyBelow(hdc, x, y, w, h)) continue;

            const int score = ScoreCandidate(x, y, w, h, c);
            if (!found || score > *outBestScore) {
                outBestPt->x = x;
                outBestPt->y = y;
                *outBestScore = score;
                found = true;
            }
        }
    }

    return found;
}

static POINT CaptureToGameRelative(int px, int py, int w, int h)
{
    POINT pt = { 0, 0 };

    py += 12;

    // 使用統一 0-1000 座標系統
    int relX, relY;
    CoordConv::RelToClient(px, py, &relX, &relY, w, h);
    pt.x = relX;
    pt.y = relY;

    // Clamp to 0-999 range (標準化相對座標)
    pt.x = ClampInt(pt.x, 0, 999);
    pt.y = ClampInt(pt.y, 0, 999);
    return pt;
}

bool TargetLock_Find(HWND hWnd, TargetLockResult* outResult)
{
    if (!outResult) return false;

    memset(outResult, 0, sizeof(*outResult));
    outResult->found = false;
    strcpy_s(outResult->reason, sizeof(outResult->reason), "not_found");

    if (!hWnd || !IsWindow(hWnd)) {
        strcpy_s(outResult->reason, sizeof(outResult->reason), "invalid_window");
        return false;
    }

    HDC hdcMem = NULL;
    HBITMAP hBmp = NULL;
    int w = 0;
    int h = 0;

    HDC hdc = CaptureWindowClient(hWnd, &hdcMem, &hBmp, &w, &h);
    if (!hdc || w <= 0 || h <= 0) {
        strcpy_s(outResult->reason, sizeof(outResult->reason), "capture_failed");
        return false;
    }

    POINT bestPt = { 0, 0 };
    int bestScore = 0;
    bool found = false;

    found = SearchByAttackScanPoints(hdc, w, h, &bestPt, &bestScore);

    if (!found) {
        found = SearchByFullTopHalf(hdc, w, h, &bestPt, &bestScore);
    }

    if (found) {
        outResult->found = true;
        outResult->rawPt = bestPt;
        outResult->gamePt = CaptureToGameRelative(bestPt.x, bestPt.y, w, h);
        outResult->score = bestScore;
        strcpy_s(outResult->reason, sizeof(outResult->reason), "vision_lock_ok");

        TL_Log("found raw=(%d,%d) game=(%d,%d) score=%d",
               outResult->rawPt.x, outResult->rawPt.y,
               outResult->gamePt.x, outResult->gamePt.y,
               outResult->score);
    }

    ReleaseCapture(hdcMem, hBmp);
    return outResult->found;
}

bool TargetLock_Click(HWND hWnd, TargetLockResult* outResult)
{
    TargetLockResult local = {};
    TargetLockResult* r = outResult ? outResult : &local;

    if (!TargetLock_Find(hWnd, r)) {
        return false;
    }

    ClickAtDirect(hWnd, r->gamePt.x, r->gamePt.y);
    Sleep(80);
    return true;
}

// ============================================================
// 記憶體鎖定檢測
// 使用 ObjectLocked 偏移: Game.exe+0x93338C
// 返回值: 0 = 無鎖定, 1+ = 已鎖定目標
// ============================================================
bool IsTargetLockedByMemory(HANDLE hProcess, DWORD baseAddr)
{
    if (!hProcess || !baseAddr) return false;

    DWORD addr = baseAddr + OffsetConfig::TargetObjectLocked();
    BYTE value = 0;
    SIZE_T bytesRead = 0;

    BOOL ok = ReadProcessMemory(hProcess, (LPCVOID)addr, &value, sizeof(value), &bytesRead);
    if (!ok || bytesRead != sizeof(value)) {
        return false;
    }

    // 0 = 無鎖定, 1+ = 已鎖定目標
    return value >= 1;
}
