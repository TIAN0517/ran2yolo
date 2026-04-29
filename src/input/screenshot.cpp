// ============================================================
// screenshot.cpp - 統一截圖模組
// 僅使用 PrintWindow + PW_RENDERFULLCONTENT (3)
// 穿透 DWM / 硬體加速黑屏
// ============================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "screenshot.h"
#include <cstring>

// ============================================================
// 內部狀態
// ============================================================
namespace {
    // 最後一次截圖的客戶區大小（用於座標轉換）
    static int s_lastClientW = 1024;
    static int s_lastClientH = 768;

    static void LogDebug(const char* fmt, ...) {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        OutputDebugStringA(buf);
    }
}

// ============================================================
// 核心截圖：PrintWindow + PW_RENDERFULLCONTENT (3)
// 穿透 DWM/硬體加速黑屏
// ============================================================
bool CaptureGameWindow(HWND hWnd, std::vector<uint8_t>& outPixels, int outW, int outH) {
    outPixels.clear();

    if (!hWnd || !IsWindow(hWnd)) {
        return false;
    }

    RECT rc;
    if (!GetClientRect(hWnd, &rc)) {
        return false;
    }

    int w = (outW > 0) ? outW : (rc.right - rc.left);
    int h = (outH > 0) ? outH : (rc.bottom - rc.top);
    if (w <= 0 || h <= 0) {
        return false;
    }

    // 記錄客戶區大小
    s_lastClientW = w;
    s_lastClientH = h;

    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen) {
        return false;
    }

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) {
        ReleaseDC(NULL, hdcScreen);
        return false;
    }

    // 使用 DIB Section 以支援 GetDIBits
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HBITMAP hBmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, NULL, NULL, 0);
    if (!hBmp) {
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return false;
    }

    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    // ============================================================
    // 統一使用 PrintWindow + PW_RENDERFULLCONTENT (3)
    // 這是穿透硬體加速黑屏的唯一可靠方法
    // ============================================================
    // PW_RENDERFULLCONTENT = 3
    // Forces PrintWindow to capture the actual frame buffer content,
    // bypassing DWM composition and hardware acceleration black screen
    BOOL ok = PrintWindow(hWnd, hdcMem, 3);

    SelectObject(hdcMem, hOldBmp);

    if (ok) {
        outPixels.resize(w * h * 4);
        if (!GetDIBits(hdcMem, hBmp, 0, h, outPixels.data(), &bmi, DIB_RGB_COLORS)) {
            outPixels.clear();
            ok = FALSE;
        }
    }

    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return ok && !outPixels.empty();
}

// ============================================================
// 取得客戶區大小
// ============================================================
void GetWindowClientSize(HWND hWnd, int* outW, int* outH) {
    if (outW) *outW = s_lastClientW;
    if (outH) *outH = s_lastClientH;

    if (hWnd && IsWindow(hWnd)) {
        RECT rc;
        if (GetClientRect(hWnd, &rc)) {
            if (outW) *outW = rc.right - rc.left;
            if (outH) *outH = rc.bottom - rc.top;
        }
    }
}

// ============================================================
// 診斷截圖保存
// ============================================================
bool SaveDiagnosticScreenshot(HWND hWnd, const char* filename) {
    if (!hWnd || !IsWindow(hWnd) || !filename) {
        return false;
    }

    std::vector<uint8_t> pixels;
    if (!CaptureGameWindow(hWnd, pixels, 0, 0)) {
        return false;
    }

    int w = s_lastClientW;
    int h = s_lastClientH;

    // 建立 BMP
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HBITMAP hBmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, NULL, NULL, 0);
    if (!hBmp) {
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return false;
    }

    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);
    SetDIBits(hdcMem, hBmp, 0, h, pixels.data(), &bmi, DIB_RGB_COLORS);

    // 儲存為 BMP 檔案
    bool saved = false;
    PBITMAPINFO pbi = (PBITMAPINFO)LocalAlloc(LPTR, sizeof(BITMAPINFOHEADER));
    if (pbi) {
        memcpy(pbi, &bmi, sizeof(BITMAPINFOHEADER));

        wchar_t wpath[MAX_PATH] = {0};
        MultiByteToWideChar(CP_ACP, 0, filename, -1, wpath, MAX_PATH);

        HANDLE hFile = CreateFileW(wpath, GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            BITMAPFILEHEADER bfh = {};
            bfh.bfType = 0x4D42;
            bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

            DWORD written = 0;
            WriteFile(hFile, &bfh, sizeof(BITMAPFILEHEADER), &written, NULL);
            WriteFile(hFile, pbi, sizeof(BITMAPINFOHEADER), &written, NULL);

            int rowSize = ((w * 32 + 31) / 32) * 4;
            BYTE* rowBuf = (BYTE*)LocalAlloc(LPTR, rowSize);
            for (int y = 0; y < h; y++) {
                GetDIBits(hdcMem, hBmp, y, 1, rowBuf, pbi, DIB_RGB_COLORS);
                WriteFile(hFile, rowBuf, rowSize, &written, NULL);
            }
            LocalFree(rowBuf);
            CloseHandle(hFile);
            saved = true;
        }
        LocalFree(pbi);
    }

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return saved;
}

// ============================================================
// 座標轉換
// ============================================================
int ScreenToRelX(int screenX, int screenW) {
    if (screenW <= 0) screenW = s_lastClientW;
    if (screenW <= 0) return 0;
    return (screenX * 1000) / screenW;
}

int ScreenToRelY(int screenY, int screenH) {
    if (screenH <= 0) screenH = s_lastClientH;
    if (screenH <= 0) return 0;
    return (screenY * 1000) / screenH;
}

// ============================================================
// Legacy Compatibility: CaptureWindowClient
// 直接截圖並返回 HDC（用於目標鎖定）
// ============================================================
HDC CaptureWindowClient(HWND hWnd, HDC* outMemDC, HBITMAP* outBmp, int* outW, int* outH) {
    if (outMemDC) *outMemDC = NULL;
    if (outBmp) *outBmp = NULL;
    if (outW) *outW = 0;
    if (outH) *outH = 0;

    if (!hWnd || !IsWindow(hWnd)) {
        return NULL;
    }

    RECT rc;
    if (!GetClientRect(hWnd, &rc)) {
        return NULL;
    }

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) {
        return NULL;
    }

    if (outW) *outW = w;
    if (outH) *outH = h;

    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen) {
        return NULL;
    }

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) {
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HBITMAP hBmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, NULL, NULL, 0);
    if (!hBmp) {
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }

    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    // 使用 PrintWindow + PW_RENDERFULLCONTENT (3)
    PrintWindow(hWnd, hdcMem, 3);

    SelectObject(hdcMem, hOldBmp);

    if (outMemDC) *outMemDC = hdcMem;
    if (outBmp) *outBmp = hBmp;

    ReleaseDC(NULL, hdcScreen);
    return hdcMem;
}

// ============================================================
// 釋放 CaptureWindowClient 分配的資源
// ============================================================
void ReleaseCapture(HDC hMemDC, HBITMAP hBmp) {
    if (hBmp && hMemDC) {
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBmp);
        if (hOldBmp) DeleteObject(hOldBmp);
    }
    if (hBmp) DeleteObject(hBmp);
    if (hMemDC) DeleteDC(hMemDC);
}
