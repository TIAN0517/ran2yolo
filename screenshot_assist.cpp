// screenshot_assist.cpp - 截圖輔助：坐標點擊失敗時用截圖比對確認按鈕位置
// 優化版：金字塔搜尋 + 顏色預篩 + 早期終止
#include "screenshot_assist.h"
#include "screenshot.h"
#include "embedded_images.h"
#include <windows.h>
#include <shlwapi.h>
#include <cstring>
#include <vector>
#include <string>

#pragma comment(lib, "shlwapi.lib")

// ============================================================
// 參考圖像結構（含顏色特徵）
// ============================================================
struct RefImage {
    const char* name;
    const uint8_t* data;
    int size;
    int w;
    int h;
    std::vector<uint8_t> pixels;       // 原始 BGRA 像素

    // 顏色特徵：用參考圖像裡出現的主要顏色區塊（用於快速預篩）
    // 每個 entry: {r_range_min, r_range_max, g_max, b_max, ratio}
    // 預先計算：不透明像素的顏色特徵
    uint32_t refColorR;    // 參考像素平均 R
    uint32_t refColorG;    // 參考像素平均 G
    uint32_t refColorB;    // 參考像素平均 B
    int      refOpaqueCount; // 不透明像素數量（用於比例判斷）
};

static std::vector<RefImage> s_refs;
static bool s_initialized = false;
static std::vector<uint8_t> s_screenCapture;
static int s_screenW = 0;
static int s_screenH = 0;

// ============================================================
// 從記憶體載入 PNG (使用 SHCreateMemStream + GDI+)
// ============================================================
static bool LoadPngFromMemory(const uint8_t* pngData, int pngSize, int* outW, int* outH, std::vector<uint8_t>* outPixels) {
    typedef int (WINAPI* GdipLoadImageFromStream_t)(void*, void**);
    typedef int (WINAPI* GdipGetImageWidth_t)(void*, unsigned int*);
    typedef int (WINAPI* GdipGetImageHeight_t)(void*, unsigned int*);
    typedef int (WINAPI* GdipDisposeImage_t)(void*);
    typedef int (WINAPI* GdipBitmapGetPixel_t)(void*, int, int, unsigned int*);

    static HMODULE gdiplus = NULL;
    static GdipLoadImageFromStream_t GdipLoadImageFromStream_fn = NULL;
    static GdipGetImageWidth_t GdipGetImageWidth_fn = NULL;
    static GdipGetImageHeight_t GdipGetImageHeight_fn = NULL;
    static GdipDisposeImage_t GdipDisposeImage_fn = NULL;
    static GdipBitmapGetPixel_t GdipBitmapGetPixel_fn = NULL;

    if (!gdiplus) {
        gdiplus = LoadLibraryA("gdiplus.dll");
        if (gdiplus) {
            GdipLoadImageFromStream_fn = (GdipLoadImageFromStream_t)GetProcAddress(gdiplus, "GdipLoadImageFromStream");
            GdipGetImageWidth_fn = (GdipGetImageWidth_t)GetProcAddress(gdiplus, "GdipGetImageWidth");
            GdipGetImageHeight_fn = (GdipGetImageHeight_t)GetProcAddress(gdiplus, "GdipGetImageHeight");
            GdipDisposeImage_fn = (GdipDisposeImage_t)GetProcAddress(gdiplus, "GdipDisposeImage");
            GdipBitmapGetPixel_fn = (GdipBitmapGetPixel_t)GetProcAddress(gdiplus, "GdipBitmapGetPixel");
        }
    }

    if (!GdipLoadImageFromStream_fn || !GdipBitmapGetPixel_fn) return false;

    IStream* stream = NULL;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, pngSize);
    if (!hMem) return false;
    void* pMem = GlobalLock(hMem);
    memcpy(pMem, pngData, pngSize);
    GlobalUnlock(hMem);

    HRESULT hr = CreateStreamOnHGlobal(hMem, TRUE, &stream);
    if (FAILED(hr) || !stream) {
        GlobalFree(hMem);
        return false;
    }

    void* img = NULL;
    int status = GdipLoadImageFromStream_fn(stream, &img);
    stream->Release();
    if (status != 0 || !img) {
        GlobalFree(hMem);
        return false;
    }

    unsigned int iw = 0, ih = 0;
    GdipGetImageWidth_fn(img, &iw);
    GdipGetImageHeight_fn(img, &ih);
    if (iw == 0 || ih == 0) {
        GdipDisposeImage_fn(img);
        GlobalFree(hMem);
        return false;
    }
    *outW = (int)iw;
    *outH = (int)ih;

    outPixels->resize(iw * ih * 4);

    for (unsigned int y = 0; y < ih; y++) {
        for (unsigned int x = 0; x < iw; x++) {
            unsigned int argb = 0;
            GdipBitmapGetPixel_fn(img, x, y, &argb);
            uint8_t a = (argb >> 24) & 0xFF;
            uint8_t r = (argb >> 16) & 0xFF;
            uint8_t g = (argb >> 8) & 0xFF;
            uint8_t b = argb & 0xFF;
            int idx = (y * iw + x) * 4;
            outPixels->data()[idx + 0] = b;
            outPixels->data()[idx + 1] = g;
            outPixels->data()[idx + 2] = r;
            outPixels->data()[idx + 3] = a;
        }
    }

    GdipDisposeImage_fn(img);
    return !outPixels->empty();
}

// 預先計算參考圖像的顏色特徵
static void ComputeColorSignature(RefImage& ref) {
    uint64_t sumR = 0, sumG = 0, sumB = 0;
    int opaqueCount = 0;
    for (int i = 0; i < ref.w * ref.h; i++) {
        uint8_t a = ref.pixels[i * 4 + 3];
        if (a > 10) {  // 不透明
            sumR += ref.pixels[i * 4 + 2];
            sumG += ref.pixels[i * 4 + 1];
            sumB += ref.pixels[i * 4 + 0];
            opaqueCount++;
        }
    }
    ref.refOpaqueCount = opaqueCount;
    if (opaqueCount > 0) {
        ref.refColorR = (uint32_t)(sumR / opaqueCount);
        ref.refColorG = (uint32_t)(sumG / opaqueCount);
        ref.refColorB = (uint32_t)(sumB / opaqueCount);
    } else {
        ref.refColorR = ref.refColorG = ref.refColorB = 0;
    }
}

// ============================================================
// 快速顏色預篩：檢查候選區域的大致顏色是否符合參考圖
// 如果參考圖的平均顏色和候選區域差太多，快速排除
// ============================================================
static bool QuickColorFilter(const uint8_t* screen, int screenW, int screenH,
                              int sx, int sy, const RefImage& ref) {
    // 探樣 8 個點（參考圖的四角 + 中心）
    const int sampleXs[] = { 0, ref.w / 2, ref.w - 1 };
    const int sampleYs[] = { 0, ref.h / 2, ref.h - 1 };
    int matchCount = 0;
    int samples = 0;

    const int tolerance = 60;  // 預篩用較寬鬆的容忍度

    for (int ty = 0; ty < 3; ty++) {
        for (int tx = 0; tx < 3; tx++) {
            int lx = sx + sampleXs[tx];
            int ly = sy + sampleYs[ty];
            if (lx < 0 || ly < 0 || lx >= screenW || ly >= screenH) continue;
            samples++;
            const uint8_t* sp = screen + (ly * screenW + lx) * 4;
            int dr = abs((int)sp[2] - (int)ref.refColorR);
            int dg = abs((int)sp[1] - (int)ref.refColorG);
            int db = abs((int)sp[0] - (int)ref.refColorB);
            if (dr + dg + db < (int)tolerance * 3) matchCount++;
        }
    }

    // 至少 50% 的取樣點顏色接近，才繼續
    return samples > 0 && matchCount * 2 >= samples;
}

// ============================================================
// 計算比對分數 (0-100)，含早期終止
// ============================================================
static int MatchScore(const uint8_t* screen, int screenW, int screenH,
                      int sx, int sy,
                      const uint8_t* ref, int refW, int refH,
                      int bestScore) {
    if (sx < 0 || sy < 0 || sx + refW > screenW || sy + refH > screenH)
        return 0;

    int match = 0;
    int total = 0;
    const int tolerance = 35;

    // 每 2 像素取樣（加速）
    for (int y = 0; y < refH; y += 2) {
        for (int x = 0; x < refW; x += 2) {
            const uint8_t* sp = screen + ((sy + y) * screenW + (sx + x)) * 4;
            const uint8_t* rp = ref + (y * refW + x) * 4;

            if (rp[3] < 10) continue;  // 透明像素

            int dr = abs((int)sp[0] - (int)rp[0]);
            int dg = abs((int)sp[1] - (int)rp[1]);
            int db = abs((int)sp[2] - (int)rp[2]);

            if (dr < tolerance && dg < tolerance && db < tolerance)
                match++;
            total++;
        }
    }

    if (total == 0) return 0;

    // 早期終止：如果已落後 bestScore 超過 20%，放棄
    int curScore = (match * 100) / total;
    int maxPossible = ((total + 3) / 4) * 100 / total;  // 樂觀估計剩餘都對
    if (curScore + 15 < bestScore) return 0;  // 早期終止

    return curScore;
}

// ============================================================
// 初始化
// ============================================================
bool ScreenshotAssist_Init() {
    if (s_initialized) return true;

    struct ImageEntry {
        const char* name;
        const uint8_t* data;
        int size;
    };

    static const ImageEntry entries[] = {
        {"mob.png",                       img_mob_png,                       sizeof(img_mob_png)},
        {"mob_hp.png",                    img_mob_hp_png,                     sizeof(img_mob_hp_png)},
        {"HP_MP_SP.png",                  img_HP_MP_SP_png,                  sizeof(img_HP_MP_SP_png)},
        {"Soul_Returning_Pearl.png",      img_Soul_Returning_Pearl_png,      sizeof(img_Soul_Returning_Pearl_png)},
        {"resurrection.png",              img_resurrection_png,               sizeof(img_resurrection_png)},
        {"mobname.png",                   img_mobname_png,                   sizeof(img_mobname_png)},
        {"arrow.png",                     img_arrow_png,                     sizeof(img_arrow_png)},
        {"charm.png",                     img_charm_png,                     sizeof(img_charm_png)},
        {"buy.png",                       img_buy_png,                       sizeof(img_buy_png)},
        {"NPC_NAME.png",                  img_NPC_NAME_png,                  sizeof(img_NPC_NAME_png)},
        {"mob_name.png",                  img_mob_name_png,                  sizeof(img_mob_name_png)},
        {"HP_Recovery_Enhanced.png",       img_HP_Recovery_Enhanced_png,       sizeof(img_HP_Recovery_Enhanced_png)},
        {"MP_Recovery_Enhancement.png",   img_MP_Recovery_Enhancement_png,   sizeof(img_MP_Recovery_Enhancement_png)},
        {"SP_Recovery_Enhanced.png",      img_SP_Recovery_Enhanced_png,      sizeof(img_SP_Recovery_Enhanced_png)},
    };

    for (const auto& e : entries) {
        RefImage img = {};
        img.name = e.name;
        img.data = e.data;
        img.size = e.size;
        if (LoadPngFromMemory(e.data, e.size, &img.w, &img.h, &img.pixels)) {
            ComputeColorSignature(img);
            s_refs.push_back(img);
        }
    }

    s_initialized = true;
    return !s_refs.empty();
}

void ScreenshotAssist_Shutdown() {
    s_refs.clear();
    s_screenCapture.clear();
    s_screenW = 0;
    s_screenH = 0;
    s_initialized = false;
}

// 將截圖座標（絕對畫素）轉為遊戲內座標 0-1023 / 0-767
int ScreenshotToRelX(int screenX, int screenW) {
    if (screenW <= 0) return screenX;
    return (int)((int64_t)screenX * 1024 / screenW);
}
int ScreenshotToRelY(int screenY, int screenH) {
    if (screenH <= 0) return screenY;
    return (int)((int64_t)screenY * 768 / screenH);
}

// ============================================================
// 金字塔搜尋：先用粗網格預篩，再用細網格確認
// ============================================================
bool ScreenshotAssist_Find(const char* refName, int* outX, int* outY, int* outScore) {
    if (!s_initialized || s_screenCapture.empty()) return false;

    const RefImage* found = nullptr;
    for (const auto& ref : s_refs) {
        if (strcmp(ref.name, refName) == 0) {
            found = &ref;
            break;
        }
    }
    if (!found) return false;

    const RefImage& ref = *found;

    // ── 第一遍：粗網格掃描 (20px 間隔) + 顏色預篩 ──
    // 候選清單：只存通過預篩的位置
    struct Candidate { int x, y; };
    std::vector<Candidate> candidates;
    candidates.reserve(256);

    const int COARSE_STEP = 20;
    for (int sy = 0; sy < s_screenH - ref.h; sy += COARSE_STEP) {
        for (int sx = 0; sx < s_screenW - ref.w; sx += COARSE_STEP) {
            // 顏色預篩：平均顏色差太多直接跳過
            if (!QuickColorFilter(s_screenCapture.data(), s_screenW, s_screenH, sx, sy, ref))
                continue;

            candidates.push_back({sx, sy});
        }
    }

    if (candidates.empty()) return false;

    // ── 第二遍：細網格確認 (2px 間隔) ──
    // 對每個候選位置，往周圍 20px 範圍做細掃描
    int bestScore = 0;
    int bestX = -1, bestY = -1;
    const int FINE_STEP = 2;
    const int REFINE_RANGE = 20;  // 只在候選點周圍 20px 範圍內做細掃描

    for (const auto& cand : candidates) {
        int startX = cand.x - REFINE_RANGE;
        int endX   = cand.x + REFINE_RANGE + COARSE_STEP;
        int startY = cand.y - REFINE_RANGE;
        int endY   = cand.y + REFINE_RANGE + COARSE_STEP;

        if (startX < 0) startX = 0;
        if (startY < 0) startY = 0;
        if (endX > s_screenW - ref.w) endX = s_screenW - ref.w;
        if (endY > s_screenH - ref.h) endY = s_screenH - ref.h;

        for (int sy = startY; sy <= endY; sy += FINE_STEP) {
            for (int sx = startX; sx <= endX; sx += FINE_STEP) {
                if (sx < 0 || sy < 0 || sx + ref.w > s_screenW || sy + ref.h > s_screenH)
                    continue;

                // 再次快速顏色預篩（細網格也要過濾）
                if (!QuickColorFilter(s_screenCapture.data(), s_screenW, s_screenH, sx, sy, ref))
                    continue;

                int score = MatchScore(s_screenCapture.data(), s_screenW, s_screenH,
                                       sx, sy, ref.pixels.data(), ref.w, ref.h, bestScore);
                if (score > bestScore) {
                    bestScore = score;
                    bestX = sx;
                    bestY = sy;
                }
            }
        }
    }

    if (bestScore >= 45) {
        *outX = bestX + ref.w / 2;
        *outY = bestY + ref.h / 2;
        *outScore = bestScore;
        return true;
    }
    return false;
}

// 保留舊函式簽名（向後兼容）
bool ScreenshotAssist_FindBest(const char* refName, int radius, int* outX, int* outY, int* outScore) {
    return ScreenshotAssist_Find(refName, outX, outY, outScore);
}

// ============================================================
// 單次截圖 + 比對（用於 TryReviveClick）
// ============================================================
bool ScreenshotAndFind(HWND hWnd, const char* refName, int* outX, int* outY, int* outScore) {
    if (!s_initialized) ScreenshotAssist_Init();
    if (!s_initialized) return false;

    HDC hdc = GetDC(hWnd);
    if (!hdc) return false;

    RECT rc;
    GetClientRect(hWnd, &rc);
    int w = rc.right;
    int h = rc.bottom;

    HDC memdc = CreateCompatibleDC(hdc);
    HBITMAP hbmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memdc, hbmp);

    // 使用 PrintWindow 取代 BitBlt（Win7~Win11 通用）
    BOOL ok = PrintWindow(hWnd, memdc, PW_CLIENTONLY);
    if (!ok) {
        BitBlt(memdc, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    s_screenCapture.resize(w * h * 4);
    GetDIBits(memdc, hbmp, 0, h, s_screenCapture.data(), &bmi, DIB_RGB_COLORS);
    s_screenW = w;
    s_screenH = h;

    SelectObject(memdc, oldBmp);
    DeleteObject(hbmp);
    DeleteDC(memdc);
    ReleaseDC(hWnd, hdc);

    // 回傳絕對像素座標（呼叫端要自行轉相對座標）
    return ScreenshotAssist_Find(refName, outX, outY, outScore);
}

// 外部查詢：截圖解析度（用於座標轉換）
int GetScreenshotWH(int* outW, int* outH) {
    if (outW) *outW = s_screenW;
    if (outH) *outH = s_screenH;
    return s_screenW;
}
