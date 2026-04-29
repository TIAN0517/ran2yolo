// ============================================================
// 大漠插件兼容實現
// 後台截圖 + 圖色識別 + 後台鍵鼠
// ============================================================

#ifdef DM_PLUGIN_EXPORTS
#define DM_API __declspec(dllexport)
#else
#define DM_API
#endif

#include "dm_plugin.h"
#include "embedded_vision_images.h"
#include "embedded_anti_pk_images.h"
#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <windows.h>
#include <excpt.h>
#include <gdiplus.h>
#include <shlwapi.h>

// 圖檔目錄（和 dm_visual.cpp 保持一致）
#define DM_VISUAL_IMAGE_PATH "C:\\Users\\tian7\\Desktop\\ahk\\"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")

// ============================================================
// 調試日誌
// ============================================================
static bool s_debug = false;
static char s_lastError[256] = { 0 };
static bool s_inited = false;
static void ShutdownGdiPlus();

static void DM_SetError(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    strncpy_s(s_lastError, buf, sizeof(s_lastError) - 1);
    if (s_debug) {
        printf("[DM] Error: %s\n", buf);
    }
}

static void DM_DebugLog(const char* fmt, ...) {
    if (!s_debug) return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("[DM] %s\n", buf);
}

// ============================================================
// 圖像緩衝區
// ============================================================
static DMImageBuffer s_captureBuf = { 0 };
static CRITICAL_SECTION s_bufCs;
static bool s_csInited = false;

// ============================================================
// 初始化/銷毀
// ============================================================
DM_API BOOL __stdcall DM_Init() {
    if (s_inited) return TRUE;
    InitializeCriticalSection(&s_bufCs);
    s_csInited = true;
    s_inited = true;
    DM_DebugLog("DM Plugin initialized");
    return TRUE;
}

DM_API void DM_Destroy() {
    if (!s_inited) return;
    if (s_csInited) {
        EnterCriticalSection(&s_bufCs);
        if (s_captureBuf.rgb) {
            delete[] s_captureBuf.rgb;
            s_captureBuf.rgb = nullptr;
        }
        memset(&s_captureBuf, 0, sizeof(s_captureBuf));
        LeaveCriticalSection(&s_bufCs);
        DeleteCriticalSection(&s_bufCs);
        s_csInited = false;
    }
    ShutdownGdiPlus();
    s_inited = false;
    DM_DebugLog("DM Plugin destroyed");
}

DM_API BOOL __stdcall DM_IsInited() { return s_inited; }
DM_API const char* __stdcall DM_GetLastError() { return s_lastError; }
DM_API void DM_SetDebug(BOOL enable) { s_debug = !!enable; }

// ============================================================
// 截圖核心：PrintWindow + PW_RENDERFULLCONTENT (Win8.1+)
// 強制抓取硬體加速畫面 (D3D/DX9/DX11)
// ============================================================
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 3  // Windows 8.1+ 專用
#endif

static bool CaptureWindowInternal(HWND hWnd, int destX, int destY, int w, int h, BYTE* rgb_out) {
    if (!hWnd || !IsWindow(hWnd)) {
        DM_SetError("Invalid window handle");
        return false;
    }

    RECT rc;
    if (!GetClientRect(hWnd, &rc)) {
        DM_SetError("GetClientRect failed");
        return false;
    }

    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    // 邊界檢查
    if (destX < 0) { w += destX; destX = 0; }
    if (destY < 0) { h += destY; destY = 0; }
    if (destX + w > winW) w = winW - destX;
    if (destY + h > winH) h = winH - destY;
    if (w <= 0 || h <= 0) {
        DM_SetError("Invalid capture area");
        return false;
    }

    // 創建兼容 DC
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, w, h);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    bool captured = false;

    // ============================================================
    // 方法1: PrintWindow with PW_RENDERFULLCONTENT (硬體加速專用)
    // ============================================================
    HDC hdcWindow = GetDC(hWnd);
    if (hdcWindow) {
        // 首先嘗試 PW_RENDERFULLCONTENT (Win8.1+ 強制抓取 D3D)
        if (PrintWindow(hWnd, hdcMem, PW_RENDERFULLCONTENT)) {
            captured = true;
            DM_DebugLog("PrintWindow(PW_RENDERFULLCONTENT) success: %dx%d", w, h);
        }
        ReleaseDC(hWnd, hdcWindow);
    }

    // ============================================================
    // 方法2: 如果方法1失敗，嘗試普通 PrintWindow
    // ============================================================
    if (!captured) {
        hdcWindow = GetDC(hWnd);
        if (hdcWindow) {
            if (PrintWindow(hWnd, hdcMem, 0)) {
                captured = true;
                DM_DebugLog("PrintWindow(0) fallback success");
            }
            ReleaseDC(hWnd, hdcWindow);
        }
    }

    // ============================================================
    // 方法3: BitBlt 抓取前台窗口（最後 fallback）
    // ============================================================
    if (!captured) {
        POINT pt = {0, 0};
        ClientToScreen(hWnd, &pt);
        if (BitBlt(hdcMem, 0, 0, w, h, hdcScreen,
            pt.x + destX, pt.y + destY, SRCCOPY)) {
            captured = true;
            DM_DebugLog("BitBlt fallback success");
        }
    }

    SelectObject(hdcMem, hOldBmp);

    if (!captured) {
        DM_SetError("All capture methods failed (PrintWindow/BitBlt)");
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return false;
    }

    // ============================================================
    // 獲取像素數據
    // ============================================================
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // 從上到下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<uint8_t> tempBuf(w * h * 3);
    if (!GetDIBits(hdcScreen, hBmp, 0, h, tempBuf.data(), &bmi, DIB_RGB_COLORS)) {
        DM_SetError("GetDIBits failed");
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return false;
    }

    // 轉換 BGR → RGB
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * 3;
            rgb_out[idx + 0] = tempBuf[idx + 2];  // R
            rgb_out[idx + 1] = tempBuf[idx + 1];  // G
            rgb_out[idx + 2] = tempBuf[idx + 0];  // B
        }
    }

    DM_DebugLog("Captured %dx%d at (%d,%d)", w, h, destX, destY);

    // 清理
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return true;
}

// ============================================================
// 公開截圖接口
// ============================================================
DM_API BOOL __stdcall DM_CaptureWindow(HWND hwnd, const char* file_name) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;
    if (!s_inited) DM_Init();

    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return FALSE;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    EnterCriticalSection(&s_bufCs);
    if (s_captureBuf.rgb) delete[] s_captureBuf.rgb;
    s_captureBuf.rgb = new BYTE[w * h * 3];
    s_captureBuf.width = w;
    s_captureBuf.height = h;
    s_captureBuf.rgb_size = w * h * 3;
    s_captureBuf.capture_time = GetTickCount();

    bool ok = CaptureWindowInternal(hwnd, 0, 0, w, h, s_captureBuf.rgb);
    LeaveCriticalSection(&s_bufCs);

    if (ok && file_name) {
        // 保存為 BMP
        char fname[MAX_PATH];
        if (strchr(file_name, '.')) {
            strncpy_s(fname, file_name, MAX_PATH - 1);
        } else {
            sprintf_s(fname, "%s.bmp", file_name);
        }

        FILE* fp = nullptr;
        fopen_s(&fp, fname, "wb");
        if (fp) {
            // BMP 文件頭
            BITMAPFILEHEADER bfh = {};
            BITMAPINFOHEADER bih = {};
            bfh.bfType = 0x4D42;
            bfh.bfSize = sizeof(bfh) + sizeof(bih) + w * h * 3;
            bfh.bfOffBits = sizeof(bfh) + sizeof(bih);

            bih.biSize = sizeof(bih);
            bih.biWidth = w;
            bih.biHeight = -h;
            bih.biPlanes = 1;
            bih.biBitCount = 24;
            bih.biCompression = BI_RGB;

            fwrite(&bfh, 1, sizeof(bfh), fp);
            fwrite(&bih, 1, sizeof(bih), fp);
            fwrite(s_captureBuf.rgb, 1, w * h * 3, fp);
            fclose(fp);
            DM_DebugLog("Saved to %s", fname);
        }
    }

    return ok ? TRUE : FALSE;
}

DM_API BOOL __stdcall DM_CaptureWindowEx(HWND hwnd, int x, int y, int w, int h, BYTE* out_rgb, int* out_size) {
    if (!hwnd || !IsWindow(hwnd) || !out_rgb) return FALSE;
    if (!s_inited) DM_Init();

    EnterCriticalSection(&s_bufCs);
    bool ok = CaptureWindowInternal(hwnd, x, y, w, h, out_rgb);
    if (ok && out_size) {
        *out_size = w * h * 3;
    }
    LeaveCriticalSection(&s_bufCs);
    return ok ? TRUE : FALSE;
}

const DMImageBuffer* DM_GetLastCapture() {
    return &s_captureBuf;
}

// ============================================================
// 顔色比較
// ============================================================
DM_API BOOL __stdcall DM_CompareColor(COLORREF c1, COLORREF c2, float sim) {
    if (sim <= 0.0f) return FALSE;
    if (sim >= 1.0f) return (c1 == c2) ? TRUE : FALSE;

    int dr = std::abs((int)(c1 & 0xFF) - (int)(c2 & 0xFF));
    int dg = std::abs((int)((c1 >> 8) & 0xFF) - (int)((c2 >> 8) & 0xFF));
    int db = std::abs((int)((c1 >> 16) & 0xFF) - (int)((c2 >> 16) & 0xFF));

    int maxDiff = (int)((1.0f - sim) * 255.0f * 3.0f);
    int totalDiff = dr + dg + db;

    return (totalDiff <= maxDiff) ? TRUE : FALSE;
}

COLORREF __stdcall DM_GetPixelColor(HWND hwnd, int x, int y) {
    if (!hwnd || !IsWindow(hwnd)) return CLR_INVALID;

    HDC hdc = GetDC(hwnd);
    if (!hdc) return CLR_INVALID;

    COLORREF color = CLR_INVALID;
    if (x >= 0 && y >= 0) {
        color = GetPixel(hdc, x, y);
    }
    ReleaseDC(hwnd, hdc);
    return color;
}

COLORREF __stdcall DM_GetPixelColorFromBuffer(const DMImageBuffer* buf, int x, int y) {
    if (!buf || !buf->rgb) return CLR_INVALID;
    if (x < 0 || x >= buf->width || y < 0 || y >= buf->height) return CLR_INVALID;

    int idx = (y * buf->width + x) * 3;
    return RGB(buf->rgb[idx + 0], buf->rgb[idx + 1], buf->rgb[idx + 2]);
}

// ============================================================
// 內存圖像顔色查找
// ============================================================
DM_API BOOL __stdcall DM_FindColorInBuffer(const DMImageBuffer* buf, int x1, int y1, int x2, int y2,
    COLORREF color, float sim, int dir, int* out_x, int* out_y) {

    if (!buf || !buf->rgb || !out_x || !out_y) return FALSE;
    if (x1 > x2 || y1 > y2) return FALSE;

    // 邊界修正
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= buf->width) x2 = buf->width - 1;
    if (y2 >= buf->height) y2 = buf->height - 1;

    int candX = -1, candY = -1;

    // 根據方向選擇掃描順序
    bool scanX = false, scanY = false;
    switch (dir) {
    case 0: scanX = true; scanY = true; break;   // 從左到右、從上到下
    case 1: scanX = true; scanY = true; break;   // 從左到右、從下到上
    case 2: scanX = true; scanY = true; break;   // 從右到左、從上到下
    case 3: scanX = true; scanY = true; break;   // 從右到左、從下到上
    default: scanX = true; scanY = true;
    }

    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            COLORREF px = DM_GetPixelColorFromBuffer(buf, x, y);
            if (DM_CompareColor(px, color, sim)) {
                *out_x = x;
                *out_y = y;
                return TRUE;
            }
        }
    }

    return FALSE;
}

DM_API int __stdcall DM_FindColorExInBuffer(const DMImageBuffer* buf, int x1, int y1, int x2, int y2,
    const char* color_format, float sim, int dir, int** out_points, int max_count) {

    if (!buf || !buf->rgb || !color_format || !out_points) return 0;
    *out_points = nullptr;
    return 0;
}

// ============================================================
// 公開顔色查找接口
// ============================================================
DM_API BOOL __stdcall DM_FindColor(HWND hwnd, int x1, int y1, int x2, int y2,
    COLORREF color, float sim, int dir, int* out_x, int* out_y) {

    if (!hwnd || !IsWindow(hwnd)) return FALSE;
    if (!s_inited) DM_Init();

    // 先截圖
    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return FALSE;
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    EnterCriticalSection(&s_bufCs);
    if (s_captureBuf.rgb) delete[] s_captureBuf.rgb;
    s_captureBuf.rgb = new BYTE[winW * winH * 3];
    s_captureBuf.width = winW;
    s_captureBuf.height = winH;
    s_captureBuf.rgb_size = winW * winH * 3;

    bool ok = CaptureWindowInternal(hwnd, 0, 0, winW, winH, s_captureBuf.rgb);
    LeaveCriticalSection(&s_bufCs);

    if (!ok) return FALSE;

    return DM_FindColorInBuffer(&s_captureBuf, x1, y1, x2, y2, color, sim, dir, out_x, out_y);
}

DM_API int __stdcall DM_FindColorEx(HWND hwnd, int x1, int y1, int x2, int y2,
    const char* color_format, float sim, int dir, int** out_points, int max_count) {

    if (!hwnd || !IsWindow(hwnd)) return 0;
    if (!color_format || !out_points) return 0;
    *out_points = nullptr;
    return 0;
}

// 模板匹配 - 多尺度 NCC + Otsu 二值化
static bool TemplateMatch(const uint8_t* screen, int screenW, int screenH,
                          const uint8_t* template_, int tmplW, int tmplH,
                          float sim, int* outX, int* outY) {
    if (!screen || !template_ || screenW <= 0 || screenH <= 0 ||
        tmplW <= 0 || tmplH <= 0 || screenW < tmplW || screenH < tmplH) {
        return false;
    }

    float bestScore = sim;
    int bestX = -1, bestY = -1;
    double bestScale = 1.0;

    // 預處理：轉灰度 (加快 Otsu 計算)
    std::vector<uint8_t> grayScreen(screenW * screenH);
    for (int i = 0; i < screenW * screenH; i++) {
        grayScreen[i] = static_cast<uint8_t>((screen[i * 3] * 0.299f) + (screen[i * 3 + 1] * 0.587f) + (screen[i * 3 + 2] * 0.114f));
    }

    std::vector<uint8_t> grayTemplate(tmplW * tmplH);
    for (int i = 0; i < tmplW * tmplH; i++) {
        grayTemplate[i] = static_cast<uint8_t>((template_[i * 3] * 0.299f) + (template_[i * 3 + 1] * 0.587f) + (template_[i * 3 + 2] * 0.114f));
    }

    // Otsu's 二值化
    auto otsu = [](std::vector<uint8_t>& img, int w, int h) {
        int hist[256] = {0};
        for (int i = 0; i < w * h; i++) hist[img[i]]++;
        int total = w * h;
        float sum = 0;
        for (int i = 0; i < 256; i++) sum += i * hist[i];

        float sumB = 0, wB = 0, wF = 0;
        float maxVar = 0;
        int thresh = 0;

        for (int t = 0; t < 256; t++) {
            wB += hist[t];
            if (wB == 0) continue;
            wF = total - wB;
            if (wF == 0) break;

            sumB += t * hist[t];
            float mB = sumB / wB;
            float mF = (sum - sumB) / wF;
            float var = wB * wF * (mB - mF) * (mB - mF);
            if (var > maxVar) {
                maxVar = var;
                thresh = t;
            }
        }
        for (int i = 0; i < w * h; i++) img[i] = (img[i] > thresh) ? 255 : 0;
    };

    otsu(grayScreen, screenW, screenH);
    otsu(grayTemplate, tmplW, tmplH);

    // 多尺度匹配 (0.8 - 1.2, step 0.1)
    const double minScale = 0.8;
    const double maxScale = 1.2;
    const double scaleStep = 0.1;

    for (double scale = minScale; scale <= maxScale; scale += scaleStep) {
        int scaledW = static_cast<int>(tmplW * scale);
        int scaledH = static_cast<int>(tmplH * scale);

        if (scaledW > screenW || scaledH > screenH) continue;

        // 雙線性插值縮放
        std::vector<uint8_t> scaledTemplate(scaledW * scaledH);
        for (int y = 0; y < scaledH; y++) {
            for (int x = 0; x < scaledW; x++) {
                float srcX = static_cast<float>(x) / static_cast<float>(scale);
                float srcY = static_cast<float>(y) / static_cast<float>(scale);
                int x0 = static_cast<int>(srcX);
                int y0 = static_cast<int>(srcY);
                float fx = srcX - x0;
                float fy = srcY - y0;

                if (x0 >= tmplW - 1 || y0 >= tmplH - 1) {
                    scaledTemplate[y * scaledW + x] = grayTemplate[y0 * tmplW + x0];
                } else {
                    float v00 = grayTemplate[y0 * tmplW + x0];
                    float v10 = grayTemplate[y0 * tmplW + (x0 + 1)];
                    float v01 = grayTemplate[(y0 + 1) * tmplW + x0];
                    float v11 = grayTemplate[(y0 + 1) * tmplW + (x0 + 1)];
                    scaledTemplate[y * scaledW + x] = (uint8_t)(v00 * (1 - fx) * (1 - fy) + v10 * fx * (1 - fy) + v01 * (1 - fx) * fy + v11 * fx * fy);
                }
            }
        }

        // NCC 遍歷 (步進 2 加速)
        for (int y = 0; y <= screenH - scaledH; y += 2) {
            for (int x = 0; x <= screenW - scaledW; x += 2) {
                double sum = 0.0, sumS = 0.0, sumT = 0.0;

                for (int ty = 0; ty < scaledH; ty += 2) {
                    for (int tx = 0; tx < scaledW; tx += 2) {
                        int sx = x + tx;
                        int sy = y + ty;
                        int si = sy * screenW + sx;
                        int ti = ty * scaledW + tx;

                        int s = grayScreen[si];
                        int t = scaledTemplate[ti];

                        sum += s * t;
                        sumS += s * s;
                        sumT += t * t;
                    }
                }

                if (sumS > 0 && sumT > 0) {
                    float ncc = (float)(sum / sqrt(sumS * sumT));
                    if (ncc > bestScore) {
                        bestScore = ncc;
                        bestX = x + scaledW / 2;
                        bestY = y + scaledH / 2;
                        bestScale = scale;
                    }
                }
            }
        }
    }

    if (bestX >= 0 && bestY >= 0) {
        if (outX) *outX = bestX;
        if (outY) *outY = bestY;
        return true;
    }
    return false;
}

// ============================================================
// GDI+ 用於 PNG 載入
// ============================================================
static bool s_gdipInited = false;
static ULONG_PTR s_gdipToken = 0;

static void InitGdiPlus() {
    if (s_gdipInited) return;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&s_gdipToken, &input, NULL);
    s_gdipInited = true;
}

static void ShutdownGdiPlus() {
    if (s_gdipInited && s_gdipToken) {
        Gdiplus::GdiplusShutdown(s_gdipToken);
        s_gdipToken = 0;
        s_gdipInited = false;
    }
}

// 使用 GDI+ 載入任意格式圖片（BMP/JPG/PNG/GIF）
static uint8_t* LoadImageWithGdiPlus(const char* path, int* outW, int* outH) {
    if (!path || !outW || !outH) return nullptr;

    InitGdiPlus();

    // 使用 Bitmap::FromFile 載入圖片
    wchar_t wpath[MAX_PATH] = {};
    int chars = MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);
    if (chars == 0) {
        chars = MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);
    }
    if (chars == 0) {
        return nullptr;
    }
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(wpath, FALSE);
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
        delete bmp;
        return nullptr;
    }

    int w = bmp->GetWidth();
    int h = bmp->GetHeight();
    if (w <= 0 || h <= 0 || w > 2000 || h > 2000) {
        delete bmp;
        return nullptr;
    }

    // 轉為 24-bit RGB
    Gdiplus::BitmapData data;
    Gdiplus::Rect rect(0, 0, w, h);
    if (bmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat24bppRGB, &data) != Gdiplus::Ok) {
        delete bmp;
        return nullptr;
    }

    uint8_t* rgb = new uint8_t[w * h * 3];
    uint8_t* src = (uint8_t*)data.Scan0;
    int stride = data.Stride;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int srcIdx = y * stride + x * 3;
            int dstIdx = y * w * 3 + x * 3;
            // BGR -> RGB
            rgb[dstIdx + 0] = src[srcIdx + 2];
            rgb[dstIdx + 1] = src[srcIdx + 1];
            rgb[dstIdx + 2] = src[srcIdx + 0];
        }
    }

    bmp->UnlockBits(&data);

    *outW = w;
    *outH = h;
    delete bmp;
    return rgb;
}

// 載入圖片到記憶體（支持 BMP/PNG/JPG 等格式）
static uint8_t* LoadImage(const char* path, int* outW, int* outH) {
    if (!path || !outW || !outH) return nullptr;

    // 先用 GDI+ 嘗試載入（支持所有格式）
    uint8_t* rgb = LoadImageWithGdiPlus(path, outW, outH);
    if (rgb) return rgb;

    // 降級：嘗試 BMP 格式
    FILE* fp = fopen(path, "rb");
    if (!fp) return nullptr;

    // 檢查是否是 BMP
    uint8_t hdr[2];
    if (fread(hdr, 1, 2, fp) != 2 || hdr[0] != 'B' || hdr[1] != 'M') {
        fclose(fp);
        return nullptr;
    }

    // 是 BMP 格式
    fseek(fp, 0, SEEK_SET);
    BITMAPFILEHEADER bfh;
    if (fread(&bfh, sizeof(bfh), 1, fp) != 1) {
        fclose(fp);
        return nullptr;
    }

    BITMAPINFOHEADER bih;
    if (fread(&bih, sizeof(bih), 1, fp) != 1) {
        fclose(fp);
        return nullptr;
    }

    int w = bih.biWidth;
    int h = abs(bih.biHeight);

    if (w <= 0 || h <= 0 || w > 2000 || h > 2000 || bih.biBitCount != 24) {
        fclose(fp);
        return nullptr;
    }

    int rowSize = ((w * 3 + 3) / 4) * 4;
    std::vector<uint8_t> bmpRow(rowSize);

    rgb = new uint8_t[w * h * 3];
    memset(rgb, 0, w * h * 3);

    fseek(fp, bfh.bfOffBits, SEEK_SET);
    for (int y = 0; y < h; y++) {
        if (fread(bmpRow.data(), 1, rowSize, fp) != (size_t)rowSize) {
            delete[] rgb;
            fclose(fp);
            return nullptr;
        }
        memcpy(&rgb[y * w * 3], bmpRow.data(), w * 3);
    }

    // BMP 是 BGR，轉為 RGB
    for (int i = 0; i < w * h; i++) {
        uint8_t tmp = rgb[i * 3 + 0];
        rgb[i * 3 + 0] = rgb[i * 3 + 2];
        rgb[i * 3 + 2] = tmp;
    }

    fclose(fp);

    *outW = w;
    *outH = h;
    return rgb;
}

// 模板緩存
struct TemplateEntry {
    std::string name;
    uint8_t* data;
    int w, h;
    DWORD lastUse;
};
static std::vector<TemplateEntry> s_templates;
static CRITICAL_SECTION s_tmplCs;

// 清理過期模板（60 秒）
static void CleanupTemplates() {
    DWORD now = GetTickCount();
    for (auto it = s_templates.begin(); it != s_templates.end(); ) {
        if (now - it->lastUse > 60000) {
            delete[] it->data;
            it = s_templates.erase(it);
        } else {
            ++it;
        }
    }
}

// 從嵌入式圖片載入 PNG（返回 RGBA 數據）
static uint8_t* LoadEmbeddedPNG(const uint8_t* pngData, size_t pngSize, int* outW, int* outH) {
    using namespace Gdiplus;

    InitGdiPlus();

    IStream* pStream = SHCreateMemStream(pngData, (UINT)pngSize);
    if (!pStream) return nullptr;

    Bitmap bmp(pStream);
    if (bmp.GetLastStatus() != Ok) {
        pStream->Release();
        return nullptr;
    }

    int w = bmp.GetWidth();
    int h = bmp.GetHeight();
    if (w <= 0 || h <= 0 || w > 2000 || h > 2000) {
        pStream->Release();
        return nullptr;
    }

    uint8_t* rgb = new(std::nothrow) uint8_t[w * h * 3];
    if (!rgb) {
        pStream->Release();
        return nullptr;
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            Color c;
            bmp.GetPixel(x, y, &c);
            int idx = (y * w + x) * 3;
            rgb[idx + 0] = c.GetR();
            rgb[idx + 1] = c.GetG();
            rgb[idx + 2] = c.GetB();
        }
    }

    pStream->Release();
    *outW = w;
    *outH = h;
    return rgb;
}

// 獲取或載入模板（優先從嵌入式載入）
static uint8_t* GetTemplate(const char* name, int* outW, int* outH) {
    EnterCriticalSection(&s_tmplCs);

    for (auto& t : s_templates) {
        if (t.name == name) {
            t.lastUse = GetTickCount();
            LeaveCriticalSection(&s_tmplCs);
            *outW = t.w;
            *outH = t.h;
            return t.data;
        }
    }

    // ===== 優先：檢查嵌入式反PK圖片 =====
    using namespace EmbeddedAntiPK;
    for (int i = 0; g_AntiPKImages[i].name != nullptr; i++) {
        if (_stricmp(g_AntiPKImages[i].name, name) == 0) {
            int w = 0, h = 0;
            uint8_t* data = LoadEmbeddedPNG(g_AntiPKImages[i].data, g_AntiPKImages[i].size, &w, &h);
            if (data && w > 0 && h > 0) {
                TemplateEntry t;
                t.name = name;
                t.data = data;
                t.w = w;
                t.h = h;
                t.lastUse = GetTickCount();
                s_templates.push_back(t);

                LeaveCriticalSection(&s_tmplCs);
                *outW = w;
                *outH = h;
                printf("[DM] ✅ 嵌入式模板載入成功: %s (%dx%d)\n", name, w, h);
                return data;
            }
        }
    }

    // 嘗試從檔案載入
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%s", DM_VISUAL_IMAGE_PATH, name);

    int w = 0, h = 0;
    uint8_t* data = LoadImage(path, &w, &h);

    if (data && w > 0 && h > 0) {
        TemplateEntry t;
        t.name = name;
        t.data = data;
        t.w = w;
        t.h = h;
        t.lastUse = GetTickCount();
        s_templates.push_back(t);

        LeaveCriticalSection(&s_tmplCs);
        *outW = w;
        *outH = h;
        printf("[DM] ✅ 模板載入成功: %s (%dx%d)\n", name, w, h);
        return data;
    }

    printf("[DM] ❌ 模板載入失敗: %s (路徑: %s)\n", name, path);
    LeaveCriticalSection(&s_tmplCs);
    return nullptr;
}

DM_API BOOL __stdcall DM_FindPic(HWND hwnd, int x1, int y1, int x2, int y2,
    const char* pic_name, const char* delta_color, float sim,
    int dir, int* out_x, int* out_y, const char** out_pic_name) {

    if (out_x) *out_x = -1;
    if (out_y) *out_y = -1;
    if (!hwnd || !pic_name) return FALSE;

    __try {
        // 截圖窗口
        RECT rc;
        if (!GetClientRect(hwnd, &rc)) return FALSE;
        int winW = rc.right - rc.left;
        int winH = rc.bottom - rc.top;

        if (!s_inited) DM_Init();

        EnterCriticalSection(&s_bufCs);
        // 安全檢查
        if (winW <= 0 || winH <= 0 || winW > 2000 || winH > 2000) {
            LeaveCriticalSection(&s_bufCs);
            return FALSE;
        }
        if (s_captureBuf.rgb) delete[] s_captureBuf.rgb;
        s_captureBuf.rgb = new(std::nothrow) uint8_t[winW * winH * 3];
        if (!s_captureBuf.rgb) {
            LeaveCriticalSection(&s_bufCs);
            return FALSE;
        }
        s_captureBuf.width = winW;
        s_captureBuf.height = winH;
        s_captureBuf.rgb_size = winW * winH * 3;
        LeaveCriticalSection(&s_bufCs);

        bool capOk = CaptureWindowInternal(hwnd, 0, 0, winW, winH, s_captureBuf.rgb);
        if (!capOk) return FALSE;

        // 載入模板
        int tmplW = 0, tmplH = 0;
        uint8_t* tmpl = GetTemplate(pic_name, &tmplW, &tmplH);
        if (!tmpl || tmplW == 0 || tmplH == 0) {
            DM_DebugLog("Failed to load template: %s", pic_name);
            return FALSE;
        }

        // 邊界調整
        if (x2 > winW) x2 = winW;
        if (y2 > winH) y2 = winH;
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;

        // 裁剪截圖區域
        int cropW = x2 - x1;
        int cropH = y2 - y1;
        if (cropW <= 0 || cropH <= 0 || cropW > winW || cropH > winH) return FALSE;

        uint8_t* cropScreen = new uint8_t[cropW * cropH * 3];
        memset(cropScreen, 0, cropW * cropH * 3);

        for (int y = 0; y < cropH; y++) {
            if ((y1 + y) * winW * 3 + x1 * 3 + cropW * 3 <= winW * winH * 3) {
                memcpy(&cropScreen[y * cropW * 3],
                       &s_captureBuf.rgb[(y1 + y) * winW * 3 + x1 * 3],
                       cropW * 3);
            }
        }

        // 模板匹配
        int foundX = -1, foundY = -1;
        bool found = TemplateMatch(cropScreen, cropW, cropH,
                                   tmpl, tmplW, tmplH, sim,
                                   &foundX, &foundY);

        delete[] cropScreen;

        if (found && foundX >= 0 && foundY >= 0) {
            if (out_x) *out_x = x1 + foundX;
            if (out_y) *out_y = y1 + foundY;
            DM_DebugLog("FindPic: %s found at (%d, %d)", pic_name, x1 + foundX, y1 + foundY);
            return TRUE;
        }

        return FALSE;
    } __except (1) {
        return FALSE;
    }
}

DM_API int __stdcall DM_FindPicEx(HWND hwnd, int x1, int y1, int x2, int y2,
    const char* pic_name, const char* delta_color, float sim,
    int dir, int** out_results, int max_count) {

    if (out_results) *out_results = nullptr;
    return 0;
}

const char* __stdcall DM_OCRExt(HWND hwnd, int x1, int y1, int x2, int y2,
    const char* color, float sim) {
    static char dummy[] = "";
    return dummy;
}

// ============================================================
// 後台鍵鼠操作（核心）
// ============================================================
static BOOL AttachToWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    DWORD targetTid = GetWindowThreadProcessId(hwnd, NULL);
    DWORD myTid = GetCurrentThreadId();

    if (targetTid && targetTid != myTid) {
        AttachThreadInput(myTid, targetTid, TRUE);
    }

    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);

    if (targetTid && targetTid != myTid) {
        AttachThreadInput(myTid, targetTid, FALSE);
    }

    return TRUE;
}

static void ScreenToClientPos(HWND hwnd, int* x, int* y) {
    if (!hwnd) return;
    POINT pt = { *x, *y };
    if (ScreenToClient(hwnd, &pt)) {
        *x = pt.x;
        *y = pt.y;
    }
}

DM_API BOOL __stdcall DM_KeyDown(HWND hwnd, DWORD key) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    // 不呼叫 AttachToWindow，避免搶走 JyTrainer 焦點
    keybd_event((BYTE)key, 0, 0, 0);
    return TRUE;
}

DM_API BOOL __stdcall DM_KeyUp(HWND hwnd, DWORD key) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    // 不呼叫 AttachToWindow，避免搶走 JyTrainer 焦點
    keybd_event((BYTE)key, 0, KEYEVENTF_KEYUP, 0);
    return TRUE;
}

DM_API BOOL __stdcall DM_KeyPress(HWND hwnd, DWORD key) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    // 使用 PostMessage 發送到遊戲視窗
    UINT scan = MapVirtualKey(key, 0);
    LPARAM downLP = (scan << 16) | (1 << 30) | (1 << 31);
    LPARAM upLP = (scan << 16) | (1 << 30) | (1 << 31) | (1 << 29);

    PostMessageA(hwnd, WM_KEYDOWN, key, downLP);
    Sleep(10);
    PostMessageA(hwnd, WM_KEYUP, key, upLP);
    return TRUE;
}

DM_API BOOL __stdcall DM_MoveTo(HWND hwnd, int x, int y) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    // 後台模式：使用 SendMessage 移動
    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return FALSE;

    LPARAM lParam = MAKELPARAM((WORD)x, (WORD)y);
    PostMessageA(hwnd, WM_MOUSEMOVE, 0, lParam);

    // 同時移動真實滑鼠（需要 ClientToScreen）
    POINT pt = { x, y };
    if (ClientToScreen(hwnd, &pt)) {
        SetCursorPos(pt.x, pt.y);
    }

    return TRUE;
}

DM_API BOOL __stdcall DM_LeftClick(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    // 取得目前滑鼠位置
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);

    // 發送左鍵點擊到目前滑鼠位置
    LPARAM lParam = MAKELPARAM((WORD)pt.x, (WORD)pt.y);
    PostMessageA(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lParam);
    Sleep(5);
    PostMessageA(hwnd, WM_LBUTTONUP, 0, lParam);

    return TRUE;
}

DM_API BOOL __stdcall DM_LeftDown(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    LPARAM lParam = MAKELPARAM((WORD)0, (WORD)0);
    PostMessageA(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lParam);
    return TRUE;
}

DM_API BOOL __stdcall DM_LeftUp(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    LPARAM lParam = MAKELPARAM((WORD)0, (WORD)0);
    PostMessageA(hwnd, WM_LBUTTONUP, 0, lParam);
    return TRUE;
}

DM_API BOOL __stdcall DM_RightClick(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    // 取得目前滑鼠位置
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);

    // 發送右鍵點擊到目前滑鼠位置
    LPARAM lParam = MAKELPARAM((WORD)pt.x, (WORD)pt.y);
    PostMessageA(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, lParam);
    Sleep(5);
    PostMessageA(hwnd, WM_RBUTTONUP, 0, lParam);

    return TRUE;
}

DM_API BOOL __stdcall DM_RightDown(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    LPARAM lParam = MAKELPARAM((WORD)0, (WORD)0);
    PostMessageA(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, lParam);
    return TRUE;
}

DM_API BOOL __stdcall DM_RightUp(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    LPARAM lParam = MAKELPARAM((WORD)0, (WORD)0);
    PostMessageA(hwnd, WM_RBUTTONUP, 0, lParam);
    return TRUE;
}

DM_API BOOL __stdcall DM_WheelDown(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    mouse_event(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)-120, 0);
    return TRUE;
}

DM_API BOOL __stdcall DM_WheelUp(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    AttachToWindow(hwnd);
    mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, 0);
    return TRUE;
}

// ============================================================
// 窗口信息
// ============================================================
DM_API HWND __stdcall DM_FindWindow(const char* lpClassName, const char* lpWindowName) {
    return FindWindowA(lpClassName, lpWindowName);
}

DM_API HWND __stdcall DM_EnumWindow(HWND parent, const char* proc_name, const char* title, DWORD filter) {
    if (parent) {
        return FindWindowExA(parent, NULL, NULL, NULL);
    }
    return FindWindowA(NULL, title);
}

DM_API BOOL __stdcall DM_SetWindowState(HWND hwnd, DWORD flag) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    switch (flag) {
    case 1:  // 激活
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        break;
    case 2:  // 關閉
        PostMessageA(hwnd, WM_CLOSE, 0, 0);
        break;
    case 3:  // 最小化
        ShowWindow(hwnd, SW_MINIMIZE);
        break;
    case 4:  // 恢復大小
        ShowWindow(hwnd, SW_RESTORE);
        break;
    case 12: // 開啟防護
        AttachToWindow(hwnd);
        break;
    default:
        break;
    }
    return TRUE;
}

DM_API BOOL __stdcall DM_GetClientSize(HWND hwnd, int* out_w, int* out_h) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return FALSE;

    if (out_w) *out_w = rc.right - rc.left;
    if (out_h) *out_h = rc.bottom - rc.top;
    return TRUE;
}

DM_API BOOL __stdcall DM_GetWindowRect(HWND hwnd, int* out_x, int* out_y, int* out_w, int* out_h) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;

    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return FALSE;

    if (out_x) *out_x = rc.left;
    if (out_y) *out_y = rc.top;
    if (out_w) *out_w = rc.right - rc.left;
    if (out_h) *out_h = rc.bottom - rc.top;
    return TRUE;
}

DM_API const char* __stdcall DM_GetWindowTitle(HWND hwnd) {
    static char title[256] = { 0 };
    if (!hwnd || !IsWindow(hwnd)) return "";
    GetWindowTextA(hwnd, title, sizeof(title));
    return title;
}

DM_API const char* __stdcall DM_GetWindowClass(HWND hwnd) {
    static char cls[256] = { 0 };
    if (!hwnd || !IsWindow(hwnd)) return "";
    GetClassNameA(hwnd, cls, sizeof(cls));
    return cls;
}

DM_API BOOL __stdcall DM_WindowExists(HWND hwnd) {
    return (hwnd && IsWindow(hwnd)) ? TRUE : FALSE;
}

DM_API BOOL __stdcall DM_SetForegroundWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;
    return SetForegroundWindow(hwnd) ? TRUE : FALSE;
}
