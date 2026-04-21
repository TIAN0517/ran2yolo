#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include "screenshot.h"
#include <cmath>
#include <shlwapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "shlwapi.lib")
#endif

// ============================================================
// 取得遊戲視窗指定螢幕座標的像素 RGB
// ============================================================
int GetPixelColor(HWND hWnd, int screenX, int screenY) {
    (void)hWnd;
    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen) return -1;
    COLORREF c = GetPixel(hdcScreen, screenX, screenY);
    ReleaseDC(NULL, hdcScreen);
    return (int)c;
}

// ============================================================
// 截圖：抓取視窗客戶區域（Win7 兼容）
// 使用 PrintWindow 支援 DirectX/D3D 遊戲
// ============================================================
HDC CaptureWindowClient(HWND hWnd, HDC* outMemDC, HBITMAP* outBitmap, int* outW, int* outH) {
    if (!hWnd || !outMemDC || !outBitmap) {
        static DWORD lastWarn = 0;
        if (GetTickCount() - lastWarn > 5000) {
            HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD written;
            const char* msg = "[截圖] 失敗: hWnd 或 outMemDC 為 NULL\n";
            WriteFile(hCon, msg, (DWORD)strlen(msg), &written, NULL);
            lastWarn = GetTickCount();
        }
        return NULL;
    }

    if (!IsWindow(hWnd)) {
        static DWORD lastWarn = 0;
        if (GetTickCount() - lastWarn > 5000) {
            HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD written;
            const char* msg = "[截圖] 失敗: IsWindow(hWnd)=FALSE\n";
            WriteFile(hCon, msg, (DWORD)strlen(msg), &written, NULL);
            lastWarn = GetTickCount();
        }
        return NULL;
    }

    RECT rc;
    if (!GetClientRect(hWnd, &rc)) {
        static DWORD lastWarn = 0;
        if (GetTickCount() - lastWarn > 5000) {
            HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD written;
            const char* msg = "[截圖] 失敗: GetClientRect()=FALSE\n";
            WriteFile(hCon, msg, (DWORD)strlen(msg), &written, NULL);
            lastWarn = GetTickCount();
        }
        return NULL;
    }
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) {
        static DWORD lastWarn = 0;
        if (GetTickCount() - lastWarn > 5000) {
            char buf[128];
            wsprintfA(buf, "[截圖] 失敗: w=%d h=%d\n", w, h);
            HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD written;
            WriteFile(hCon, buf, (DWORD)strlen(buf), &written, NULL);
            lastWarn = GetTickCount();
        }
        return NULL;
    }

    // 創建與視窗 DC 兼容的記憶體 DC
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, w, h);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    // 方法1 (首選): PrintWindow - 對 D3D9 視窗模式最有效
    // D3D9 渲染到 GPU 後台緩衝，GetDC/BitBlt 無法捕到，PrintWindow 可以
    BOOL ok = FALSE;
    for (int retry = 0; retry < 3 && !ok; retry++) {
        if (retry > 0) Sleep(5);  // 等待渲染完成
        ok = PrintWindow(hWnd, hdcMem, PW_CLIENTONLY);
    }

    // 方法2 (fallback): GetDCEx + BitBlt（對 GDI 遊戲有效）
    if (!ok) {
        HDC hdcClient = GetDCEx(hWnd, NULL, DCX_WINDOW | DCX_LOCKWINDOWUPDATE);
        if (hdcClient) {
            ok = BitBlt(hdcMem, 0, 0, w, h, hdcClient, 0, 0, SRCCOPY);
            ReleaseDC(NULL, hdcClient);
        }
    }

    // 方法3 (last resort): GetDC + BitBlt
    if (!ok) {
        HDC hdcWindow = GetDC(hWnd);
        if (hdcWindow) {
            BitBlt(hdcMem, 0, 0, w, h, hdcWindow, 0, 0, SRCCOPY);
            ReleaseDC(hWnd, hdcWindow);
            ok = TRUE;
        }
    }

    SelectObject(hdcMem, hOldBmp);
    ReleaseDC(NULL, hdcScreen);

    if (!ok) {
        static DWORD lastWarn = 0;
        if (GetTickCount() - lastWarn > 5000) {
            DWORD err = GetLastError();
            char buf[128];
            wsprintfA(buf, "[截圖] 截圖失敗: err=%lu w=%d h=%d\n", err, w, h);
            HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD written;
            WriteFile(hCon, buf, (DWORD)strlen(buf), &written, NULL);
            lastWarn = GetTickCount();
        }
        DeleteDC(hdcMem);
        DeleteObject(hBmp);
        return NULL;
    }

    *outMemDC = hdcMem;
    *outBitmap = hBmp;
    if (outW) *outW = w;
    if (outH) *outH = h;
    return hdcMem;
}

// ============================================================
// 從 HDC 讀取像素 RGB
// ============================================================
COLORREF GetPixelRGB(HDC hdcDC, int x, int y) {
    if (!hdcDC) return CLR_INVALID;
    return GetPixel(hdcDC, x, y);
}

// ============================================================
// 釋放截圖資源
// ============================================================
void ReleaseCapture(HDC hdcMem, HBITMAP hBitmap) {
    if (hdcMem) DeleteDC(hdcMem);
    if (hBitmap) DeleteObject(hBitmap);
}

// ============================================================
// 在 (cx, cz) 為中心、半徑 r 範圍內搜尋 HP 條
// HP 條特徵：紅色 R>200, G<50, B<50
// 返回第一個找到的位置（相對 hdc 的座標），沒找到返回 {-1,-1}
// ============================================================
POINT FindMonsterHPBar(HDC hdcDC, int cx, int cz, int radius) {
    POINT empty = {-1, -1};
    if (!hdcDC) return empty;

    // HP 條大約在怪物頭頂，所以往上偏移
    // 從怪物中心往上搜尋
    for (int dy = 0; dy >= -radius; dy--) {
        for (int dx = -radius; dx <= radius; dx++) {
            int px = cx + dx;
            int py = cz + dy;
            COLORREF c = GetPixel(hdcDC, px, py);
            if (c == CLR_INVALID) continue;
            int r = GetRValue(c);
            int g = GetGValue(c);
            int b = GetBValue(c);
            // HP 條：紅色主調
            if (r > 180 && g < 80 && b < 80 && r > g + 100) {
                POINT pt = {px, py};
                return pt;
            }
        }
    }
    return empty;
}

// ============================================================
// 搜尋多個採樣點（過濾漂浮 HP 條）
// ============================================================
POINT ScanForMonsters(HDC hdcDC, const POINT* scanPoints, int count, int scanRadius) {
    POINT empty = {-1, -1};
    if (!hdcDC || !scanPoints || count <= 0) return empty;

    for (int i = 0; i < count; i++) {
        int cx = scanPoints[i].x;
        int cz = scanPoints[i].y;

        // 搜尋附近是否有 HP 條（紅色）
        for (int dy = -scanRadius; dy <= scanRadius; dy++) {
            for (int dx = -scanRadius; dx <= scanRadius; dx++) {
                int px = cx + dx;
                int py = cz + dy;
                COLORREF c = GetPixel(hdcDC, px, py);
                if (c == CLR_INVALID) continue;
                int r = GetRValue(c);
                int g = GetGValue(c);
                int b = GetBValue(c);

                // HP 條特徵：紅色 R>180, G<80, B<80
                if (r > 180 && g < 80 && b < 80 && r > g + 80) {
                    // ── 確認這不是漂浮 HP 條 ──
                    // 檢查 HP 條下方（怪物身體方向）是否有非背景像素
                    bool hasMonsterBody = false;
                    for (int bodyY = py + 2; bodyY <= py + 20; bodyY++) {
                        for (int bodyX = px - 15; bodyX <= px + 15; bodyX++) {
                            COLORREF bc = GetPixel(hdcDC, bodyX, bodyY);
                            if (bc == CLR_INVALID) continue;
                            int br = GetRValue(bc);
                            int bg = GetGValue(bc);
                            int bb = GetBValue(bc);
                            // 跳過紅色（HP條）、黑色/深色（輪廓）、白色（高光）
                            if (br < 50 && bg < 50 && bb < 50) continue;  // 黑色輪廓
                            if (br > 240 && bg > 240 && bb > 240) continue; // 白色高光
                            if (br > 180 && bg < 80 && bb < 80) continue;  // 紅色HP條
                            if (br < 30 && bg < 30 && bb < 30) continue;  // 純黑
                            // 有其他顏色 → 怪物身體
                            hasMonsterBody = true;
                            break;
                        }
                        if (hasMonsterBody) break;
                    }
                    if (hasMonsterBody) {
                        // 找到真正的怪物！點擊 HP 條下方的怪物身體中心
                        int clickX = px;
                        int clickY = py + 12;  // HP條下方12px為怪物身體
                        return {clickX, clickY};
                    }
                    // 漂浮HP條，跳過繼續找
                }
            }
        }
    }
    return empty;
}

// ============================================================
// 座標校正巨集：截圖 + 標記點擊位置 + 保存為 BMP
// ============================================================

// 座標校正截圖保存
// phaseName: 階段名稱（如 "Phase3_Tab", "Phase3_BuyHP"）
// markers: 標記點座標数组
// markerLabels: 對應標記名稱
// count: 標記數量
bool SaveCalibrationScreenshot(HWND hWnd, const char* phaseName, const POINT* markers, const char** markerLabels, int count) {
    if (!hWnd || !phaseName) return false;

    // 1. 創建調試目錄
    wchar_t dirPath[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, dirPath, MAX_PATH);
    wchar_t* slash = wcsrchr(dirPath, L'\\');
    if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - dirPath), L"shop_debug");
    CreateDirectoryW(dirPath, NULL);

    // 2. 截圖
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    RECT rc;
    GetClientRect(hWnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, w, h);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    // 填充白色背景
    HBRUSH whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdcMem, &rc, whiteBrush);
    DeleteObject(whiteBrush);

    // 嘗試 PrintWindow
    BOOL ok = PrintWindow(hWnd, hdcMem, PW_CLIENTONLY);
    if (!ok) {
        HDC hdcWindow = GetDC(hWnd);
        BitBlt(hdcMem, 0, 0, w, h, hdcWindow, 0, 0, SRCCOPY);
        ReleaseDC(hWnd, hdcWindow);
    }

    SelectObject(hdcMem, hOldBmp);

    // 3. 標記所有點（綠色圓圈 + 黃色文字）
    HPEN greenPen = CreatePen(PS_SOLID, 3, RGB(0, 255, 0));
    HBRUSH transBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    SelectObject(hdcMem, greenPen);
    SelectObject(hdcMem, transBrush);

    for (int i = 0; i < count; i++) {
        int x = markers[i].x;
        int y = markers[i].y;
        const char* label = markerLabels ? markerLabels[i] : "";

        // 圓圈
        Ellipse(hdcMem, x - 10, y - 10, x + 10, y + 10);
        // 十字
        MoveToEx(hdcMem, x - 15, y, NULL);
        LineTo(hdcMem, x + 15, y);
        MoveToEx(hdcMem, x, y - 15, NULL);
        LineTo(hdcMem, x, y + 15);

        // 標籤
        if (label && label[0]) {
            SetBkMode(hdcMem, TRANSPARENT);
            SetTextColor(hdcMem, RGB(255, 255, 0));
            HFONT hFont = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
            HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont);

            RECT rcText = {x + 15, y - 20, x + 200, y + 5};
            SetBkColor(hdcMem, RGB(0, 0, 0));
            ExtTextOut(hdcMem, x + 15, y - 18, ETO_OPAQUE, &rcText, NULL, 0, NULL);

            wchar_t wbuf[64] = {0};
            MultiByteToWideChar(CP_ACP, 0, label, -1, wbuf, 64);
            TextOutW(hdcMem, x + 15, y - 18, wbuf, wcslen(wbuf));

            SelectObject(hdcMem, hOldFont);
            DeleteObject(hFont);
        }
    }

    DeleteObject(greenPen);

    // 4. 保存為 BMP
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t filepath[MAX_PATH] = {0};
    wsprintfW(filepath, L"%s\\%S_%04d%02d%02d_%02d%02d%02d.bmp",
        dirPath, phaseName,
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);

    bool saved = false;
    PBITMAPINFO pbi = (PBITMAPINFO)LocalAlloc(LPTR, sizeof(BITMAPINFOHEADER));
    if (pbi) {
        pbi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        pbi->bmiHeader.biWidth = w;
        pbi->bmiHeader.biHeight = -h;
        pbi->bmiHeader.biPlanes = 1;
        pbi->bmiHeader.biBitCount = 24;
        pbi->bmiHeader.biCompression = BI_RGB;

        HANDLE hFile = CreateFileW(filepath, GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            BITMAPFILEHEADER bfh = {};
            bfh.bfType = 0x4D42;
            bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

            DWORD written = 0;
            WriteFile(hFile, &bfh, sizeof(BITMAPFILEHEADER), &written, NULL);
            WriteFile(hFile, &pbi->bmiHeader, sizeof(BITMAPINFOHEADER), &written, NULL);

            int rowSize = ((w * 24 + 31) / 32) * 4;
            byte* rowBuf = (byte*)LocalAlloc(LPTR, rowSize);
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

    // 5. 清理
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    // 6. 輸出調試信息
    if (saved) {
        char msg[256];
        wsprintfA(msg, "[校正] 截圖已保存: shop_debug\\%s_*.bmp", phaseName);
        HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD written;
        WriteFile(hCon, msg, (DWORD)strlen(msg), &written, NULL);
    }

    return saved;
}

// ============================================================
// 視覺辨識專用截圖（Win7~Win11 通用）
// 使用 PrintWindow + DIB Section，取代 BitBlt
// ============================================================
bool CaptureGameWindow(HWND hWnd, std::vector<uint8_t>& outPixels, int outW, int outH) {
    outPixels.clear();
    if (!hWnd || !IsWindow(hWnd)) return false;

    RECT rc;
    if (!GetClientRect(hWnd, &rc)) return false;

    int w = (outW > 0) ? outW : (rc.right - rc.left);
    int h = (outH > 0) ? outH : (rc.bottom - rc.top);
    if (w <= 0 || h <= 0) return false;

    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen) return false;

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) {
        ReleaseDC(NULL, hdcScreen);
        return false;
    }

    // 使用 DIB Section 以支援 GetDIBits
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // 自上而下
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

    // 使用 PrintWindow（Win7~Win11 通用）
    // PW_CLIENTONLY 確保只截取客戶區
    BOOL ok = PrintWindow(hWnd, hdcMem, PW_CLIENTONLY);

    // Fallback: 如果 PrintWindow 失敗，使用 GetDCEx
    if (!ok) {
        HDC hdcClient = GetDCEx(hWnd, NULL, DCX_WINDOW | DCX_LOCKWINDOWUPDATE);
        if (hdcClient) {
            ok = BitBlt(hdcMem, 0, 0, w, h, hdcClient, 0, 0, SRCCOPY);
            ReleaseDC(hWnd, hdcClient);
        }
    }

    SelectObject(hdcMem, hOldBmp);

    if (ok) {
        outPixels.resize(w * h * 4);
        GetDIBits(hdcMem, hBmp, 0, h, outPixels.data(), &bmi, DIB_RGB_COLORS);
    }

    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return ok && !outPixels.empty();
}

// ============================================================
// 讀取 HP/MP/SP 條百分比（純像素計數）
// HP: 紅色條 (R>200, G<80, B<80)
// MP: 藍色條 (R<80, G<80, B>200)
// SP: 綠色條 (R<80, G>200, B<80)
// ============================================================
bool ReadHPMPSPBars(HWND hWnd, int* outHpPct, int* outMpPct, int* outSpPct) {
    if (outHpPct) *outHpPct = 100;
    if (outMpPct) *outMpPct = 100;
    if (outSpPct) *outSpPct = 100;

    if (!hWnd || !IsWindow(hWnd)) return false;

    RECT rc;
    if (!GetClientRect(hWnd, &rc)) return false;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return false;

    // 截圖
    std::vector<uint8_t> pixels;
    if (!CaptureGameWindow(hWnd, pixels, w, h)) return false;

    // HP/MP/SP 條通常在視窗左下角或底部狀態列
    // 假設狀態列在底部 50-80px 範圍內
    const int statusBarTop = h - 80;
    const int statusBarBottom = h - 20;

    auto scanBarWidth = [&](int yStart, int yEnd, int rThresh, int gThresh, int bThresh) -> int {
        int maxWidth = 0;
        for (int y = yStart; y < yEnd; y++) {
            int barStart = -1;
            int barEnd = -1;
            for (int x = 0; x < w; x++) {
                int idx = (y * w + x) * 4;
                int r = pixels[idx + 2];
                int g = pixels[idx + 1];
                int b = pixels[idx + 0];

                bool isBar = false;
                if (rThresh > 200 && r > 200 && g < 80 && b < 80) isBar = true;  // HP 紅
                else if (bThresh > 200 && b > 200 && r < 80 && g < 80) isBar = true;  // MP 藍
                else if (gThresh > 200 && g > 200 && r < 80 && b < 80) isBar = true;  // SP 綠

                if (isBar && barStart < 0) barStart = x;
                if (!isBar && barStart >= 0 && barEnd < 0) barEnd = x;
            }
            if (barStart >= 0) {
                int barWidth = (barEnd > barStart) ? (barEnd - barStart) : (w - barStart);
                if (barWidth > maxWidth) maxWidth = barWidth;
            }
        }
        return maxWidth;
    };

    // 找最大 HP 條寬度（估計總寬度）
    int hpMaxWidth = 0;
    int hpCurrentWidth = 0;
    for (int y = statusBarTop; y < statusBarBottom; y++) {
        int barStart = -1;
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * 4;
            int r = pixels[idx + 2];
            int g = pixels[idx + 1];
            int b = pixels[idx + 0];
            if (r > 200 && g < 80 && b < 80) {
                if (barStart < 0) barStart = x;
                hpCurrentWidth = x - barStart + 1;
            }
        }
        if (hpCurrentWidth > hpMaxWidth) hpMaxWidth = hpCurrentWidth;
    }

    // 估算 HP 條總寬度（假設最大不超過 200px）
    const int HP_MAX_WIDTH = 200;
    if (hpMaxWidth > 0 && hpMaxWidth <= HP_MAX_WIDTH) {
        if (outHpPct) *outHpPct = (hpMaxWidth * 100) / HP_MAX_WIDTH;
        if (*outHpPct > 100) *outHpPct = 100;
        if (*outHpPct < 0) *outHpPct = 0;
    }

    // MP/SP 條識別（簡化版，預設 100%）
    if (outMpPct) *outMpPct = 100;
    if (outSpPct) *outSpPct = 100;

    return true;
}
