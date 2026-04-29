// ============================================================
// screenshot_universal.cpp
// 統一截圖系統 - 僅使用 PrintWindow + PW_RENDERFULLCONTENT (3)
// 穿透硬體加速黑屏
// ============================================================

#include "screenshot_universal.h"
#include "../platform/coord_calib.h"  // CoordConv 統一座標轉換

// ============================================================
// Internal State
// ============================================================
namespace {
    static HWND s_hWnd = NULL;
    static int s_clientW = 1024;
    static int s_clientH = 768;
    static float s_dpiScaleX = 1.0f;
    static float s_dpiScaleY = 1.0f;

    static void Log(const char* tag, const char* msg) {
        HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hCon && hCon != INVALID_HANDLE_VALUE) {
            char buf[512];
            wsprintfA(buf, "[%s] %s\n", tag, msg);
            DWORD written;
            WriteFile(hCon, buf, (DWORD)strlen(buf), &written, NULL);
        }
    }

    static void UpdateDPIScale(HWND hWnd) {
        if (!hWnd) return;

        HDC hdc = GetDC(hWnd);
        if (hdc) {
            int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
            int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
            s_dpiScaleX = dpiX / 96.0f;
            s_dpiScaleY = dpiY / 96.0f;
            ReleaseDC(hWnd, hdc);
        }

        RECT rc;
        if (GetClientRect(hWnd, &rc)) {
            s_clientW = rc.right - rc.left;
            s_clientH = rc.bottom - rc.top;
        }
    }
}

// ============================================================
// Core: PrintWindow with PW_RENDERFULLCONTENT (3)
// 穿透 DWM/硬體加速黑屏
// ============================================================
static bool CaptureByPrintWindow(HWND hWnd, std::vector<uint8_t>& outPixels, int w, int h) {
    outPixels.clear();

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

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
    // PW_RENDERFULLCONTENT = 3
    // Forces PrintWindow to capture the actual frame buffer content,
    // bypassing DWM composition and hardware acceleration black screen
    // ============================================================
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
// Public API
// ============================================================
bool ScreenshotUniversal::Init(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) {
        return false;
    }

    s_hWnd = hWnd;
    UpdateDPIScale(hWnd);

    char buf[128];
    wsprintfA(buf, "PrintWindow+PW_RENDERFULLCONTENT(3), DPI=%.2fx%.2f, Client=%dx%d",
              s_dpiScaleX, s_dpiScaleY, s_clientW, s_clientH);
    Log("Screenshot", buf);

    return true;
}

ScreenshotUniversal::CaptureResult ScreenshotUniversal::Capture(HWND hWnd) {
    CaptureResult result;

    // 截圖已全局禁用，直接返回失敗
    // 避免 PrintWindow 導致閃黑屏
    return result;
}

ScreenshotUniversal::CaptureResult ScreenshotUniversal::CaptureRGBA(HWND hWnd) {
    return Capture(hWnd);
}

void ScreenshotUniversal::Shutdown() {
    s_hWnd = NULL;
    s_clientW = 1024;
    s_clientH = 768;
    s_dpiScaleX = 1.0f;
    s_dpiScaleY = 1.0f;
}

// ============================================================
// Coordinate Conversion (1000x1000)
// ============================================================
ScreenshotUniversal::GameCoords ScreenshotUniversal::ScreenToGame(int screenX, int screenY, HWND hWnd) {
    GameCoords coords;

    RECT rc;
    if (hWnd && GetClientRect(hWnd, &rc)) {
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        if (w > 0 && h > 0) {
            coords.x = (int)((float)screenX * 1000.0f / (float)w);
            coords.y = (int)((float)screenY * 1000.0f / (float)h);

            if (coords.x < 0) coords.x = 0;
            if (coords.x > 999) coords.x = 999;
            if (coords.y < 0) coords.y = 0;
            if (coords.y > 999) coords.y = 999;
        }
    }

    return coords;
}

ScreenshotUniversal::ScreenCoords ScreenshotUniversal::GameToScreen(int gameX, int gameY, HWND hWnd) {
    ScreenCoords coords;
    // 使用統一 CoordConv 命名空間
    int sx, sy;
    if (CoordConv::RelToScreen(hWnd, gameX, gameY, &sx, &sy)) {
        coords.x = sx;
        coords.y = sy;
    }
    return coords;
}

float ScreenshotUniversal::GetDPIScaleX(HWND hWnd) {
    if (hWnd) {
        UpdateDPIScale(hWnd);
    }
    return s_dpiScaleX;
}

float ScreenshotUniversal::GetDPIScaleY(HWND hWnd) {
    if (hWnd) {
        UpdateDPIScale(hWnd);
    }
    return s_dpiScaleY;
}

void ScreenshotUniversal::GetClientSize(HWND hWnd, int* outW, int* outH) {
    if (outW) *outW = s_clientW;
    if (outH) *outH = s_clientH;

    if (hWnd) {
        RECT rc;
        if (GetClientRect(hWnd, &rc)) {
            if (outW) *outW = rc.right - rc.left;
            if (outH) *outH = rc.bottom - rc.top;
        }
    }
}

// ============================================================
// Save Screenshot
// ============================================================
bool ScreenshotUniversal::SaveScreenshot(const char* filepath, const CaptureResult& cap) {
    if (!cap.success || cap.pixels.empty()) {
        return false;
    }

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cap.width;
    bmi.bmiHeader.biHeight = -cap.height;
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
    SetDIBits(hdcMem, hBmp, 0, cap.height, cap.pixels.data(), &bmi, DIB_RGB_COLORS);

    bool saved = false;
    PBITMAPINFO pbi = (PBITMAPINFO)LocalAlloc(LPTR, sizeof(BITMAPINFOHEADER));
    if (pbi) {
        memcpy(pbi, &bmi, sizeof(BITMAPINFOHEADER));

        wchar_t wpath[MAX_PATH] = {0};
        MultiByteToWideChar(CP_ACP, 0, filepath, -1, wpath, MAX_PATH);

        HANDLE hFile = CreateFileW(wpath, GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            BITMAPFILEHEADER bfh = {};
            bfh.bfType = 0x4D42;
            bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

            DWORD written = 0;
            WriteFile(hFile, &bfh, sizeof(BITMAPFILEHEADER), &written, NULL);
            WriteFile(hFile, pbi, sizeof(BITMAPINFOHEADER), &written, NULL);

            int rowSize = ((cap.width * 32 + 31) / 32) * 4;
            byte* rowBuf = (byte*)LocalAlloc(LPTR, rowSize);
            for (int y = 0; y < cap.height; y++) {
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
// Diagnostic
// ============================================================
void ScreenshotUniversal::DiagnosticCapture(HWND hWnd) {
    char buf[256];

    wsprintfA(buf, "=== Screenshot Diagnostic ===");
    Log("Screenshot", buf);

    wsprintfA(buf, "Method: PrintWindow + PW_RENDERFULLCONTENT (3)");
    Log("Screenshot", buf);

    wsprintfA(buf, "DPI Scale: %.2fx%.2f", s_dpiScaleX, s_dpiScaleY);
    Log("Screenshot", buf);

    wsprintfA(buf, "Client Size: %dx%d", s_clientW, s_clientH);
    Log("Screenshot", buf);

    CaptureResult cap = Capture(hWnd);
    if (cap.success) {
        wsprintfA(buf, "Capture OK: %dx%d", cap.width, cap.height);
        Log("Screenshot", buf);
        SaveScreenshot("test_capture.bmp", cap);
    } else {
        Log("Screenshot", "Capture FAILED - game may be minimized or using exclusive fullscreen");
    }

    Log("Screenshot", "=== End Diagnostic ===");
}
