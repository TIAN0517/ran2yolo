#include "screenshot_assist_compat.h"
#include "screenshot_universal.h"

static bool s_initialized = false;
static int s_lastW = 0;
static int s_lastH = 0;

static void SetMiss(int* outX, int* outY, int* outScore) {
    if (outX) *outX = -1;
    if (outY) *outY = -1;
    if (outScore) *outScore = 0;
}

bool ScreenshotAssist_Init() {
    s_initialized = true;
    return true;
}

void ScreenshotAssist_Shutdown() {
    s_initialized = false;
    s_lastW = 0;
    s_lastH = 0;
}

bool ScreenshotAssist_Find(const char* refName, int* outX, int* outY, int* outScore) {
    (void)refName;
    SetMiss(outX, outY, outScore);
    return false;
}

bool ScreenshotAssist_FindBest(const char* refName, int radius, int* outX, int* outY, int* outScore) {
    (void)radius;
    return ScreenshotAssist_Find(refName, outX, outY, outScore);
}

bool ScreenshotAndFind(HWND hWnd, const char* refName, int* outX, int* outY, int* outScore) {
    if (!s_initialized) ScreenshotAssist_Init();

    ScreenshotUniversal::CaptureResult cap = ScreenshotUniversal::Capture(hWnd);
    if (cap.success) {
        s_lastW = cap.width;
        s_lastH = cap.height;
    }

    return ScreenshotAssist_Find(refName, outX, outY, outScore);
}

int ScreenshotToRelX(int screenX, int screenW) {
    if (screenW <= 0) return screenX;
    return (int)((long long)screenX * 1024 / screenW);
}

int ScreenshotToRelY(int screenY, int screenH) {
    if (screenH <= 0) return screenY;
    return (int)((long long)screenY * 768 / screenH);
}

int GetScreenshotWH(int* outW, int* outH) {
    if (outW) *outW = s_lastW;
    if (outH) *outH = s_lastH;
    return s_lastW;
}
