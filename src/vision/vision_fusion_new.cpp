// ============================================================
// vision_fusion.cpp
// Vision Fusion - DM + VLM Integration Layer
// ============================================================

#include "vision_fusion.h"
#include "vlm_vision_api.h"
#include "../embed/dm_wrapper.h"
#include <algorithm>

// ============================================================
// Internal State
// ============================================================
namespace {
    static HWND s_hWnd = NULL;
    static VisionFusion::FusionConfig s_config;
    static bool s_inited = false;
    static bool s_debug = false;

    static std::atomic<int> s_dmFindCount{0};
    static std::atomic<int> s_dmSuccessCount{0};
    static std::atomic<int> s_vlmFallbackCount{0};
    static std::atomic<int> s_vlmSuccessCount{0};
    static std::atomic<int> s_totalLatencyMs{0};
    static std::atomic<int> s_consecutiveFails{0};

    static VisionFusion::TransitionLog s_logs[50];
    static int s_logIndex = 0;
    static int s_logCount = 0;

    static ScreenshotUniversal::CaptureResult s_lastCapture;

    static void Log(const char* tag, const char* msg) {
        if (!s_debug) return;
        HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hCon && hCon != INVALID_HANDLE_VALUE) {
            char buf[512];
            wsprintfA(buf, "[%s] %s\n", tag, msg);
            DWORD written;
            WriteFile(hCon, buf, (DWORD)strlen(buf), &written, NULL);
        }
    }

    static void Logf(const char* tag, const char* fmt, ...) {
        if (!s_debug) return;
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        Log(tag, buf);
    }
}

// ============================================================
// Init
// ============================================================
bool VisionFusion::Init(HWND hWnd, const FusionConfig& config) {
    if (!hWnd || !IsWindow(hWnd)) {
        return false;
    }

    s_hWnd = hWnd;
    s_config = config;

    if (!ScreenshotUniversal::Init(hWnd)) {
        Log("VisionFusion", "Failed to init screenshot");
        return false;
    }

    // 視覺模式已禁用，不初始化 DM
    // if (!g_dm.Init()) {
    //     Log("VisionFusion", "Failed to init DM");
    // } else {
    //     g_dm.BindWindow(hWnd);
    //     g_dm.SetPath("C:\\Users\\tian7\\Desktop\\ahk\\");
    // }

    // 初始化 VLM Vision（使用 NVIDIA API）
    VLMVision::VLMConfig vlmConfig;
    vlmConfig.api_endpoint = "https://integrate.api.nvidia.com/v1/chat/completions";
    vlmConfig.model_name = "mistralai/mistral-small-4-119b-2603";  // 最快模型
    vlmConfig.connect_timeout_ms = 5000;
    vlmConfig.read_timeout_ms = 15000;
    VLMVision::Init(vlmConfig);

    s_inited = true;
    Log("VisionFusion", "✅ Vision Fusion 已啟用");
    if (s_config.enable_vlm_fallback) {
        Log("VisionFusion", "   VLM Fallback: ✅ 已啟用");
    } else {
        Log("VisionFusion", "   VLM Fallback: ❌ 已關閉（避免閃黑屏）");
    }

    return true;
}

void VisionFusion::SetConfig(const FusionConfig& config) {
    s_config = config;
}

VisionFusion::FusionConfig VisionFusion::GetConfig() {
    return s_config;
}

void VisionFusion::Shutdown() {
    ScreenshotUniversal::Shutdown();
    g_dm.Destroy();
    s_inited = false;
}

bool VisionFusion::ReInit(HWND hWnd) {
    Shutdown();
    return Init(hWnd, s_config);
}

VisionFusion::FindResult VisionFusion::FindPic(const char* imageName,
                                              int x1, int y1, int x2, int y2, float sim) {
    VisionFusion::FindResult result;
    result.image_name = imageName;

    if (!s_inited) {
        result.error = "Not initialized";
        return result;
    }

    DWORD startTime = GetTickCount();
    int w, h;
    ScreenshotUniversal::GetClientSize(s_hWnd, &w, &h);

    float scaleX = (float)w / 1024.0f;
    float scaleY = (float)h / 768.0f;

    int adjX1 = (int)(x1 * scaleX);
    int adjY1 = (int)(y1 * scaleY);
    int adjX2 = (int)(x2 * scaleX);
    int adjY2 = (int)(y2 * scaleY);

    if (g_dm.IsInited()) {
        s_dmFindCount++;
        DMWrapper::PicResult dmResult = g_dm.FindPicResult(adjX1, adjY1, adjX2, adjY2,
                                                            imageName, sim);

        if (dmResult.found) {
            result.found = true;
            result.x = dmResult.x;
            result.y = dmResult.y;
            result.confidence = sim;
            result.from_dm = true;
            result.method = "DM_FindPic";
            s_dmSuccessCount++;
            s_consecutiveFails = 0;

            int latency = (int)(GetTickCount() - startTime);
            s_totalLatencyMs += latency;

            Logf("VisionFusion", "DM found %s @ (%d,%d) in %dms",
                 imageName, result.x, result.y, latency);
            return result;
        }
    }

    s_consecutiveFails++;
    bool shouldFallback = (s_config.enable_vlm_fallback &&
                          s_consecutiveFails >= s_config.consecutive_fail_threshold);

    if (!shouldFallback) {
        result.confidence = sim * 0.5f;
        result.method = "DM_FindPic";
        Logf("VisionFusion", "DM not found %s (consecutive fails: %d)",
             imageName, (int)s_consecutiveFails.load());
        return result;
    }

    s_vlmFallbackCount++;
    Logf("VisionFusion", "VLM fallback for %s", imageName);

    ScreenshotUniversal::CaptureResult cap = ScreenshotUniversal::Capture(s_hWnd);
    if (!cap.success) {
        result.error = "Capture failed";
        return result;
    }

    s_lastCapture = cap;

    VLMVision::ImageData imgData;
    imgData.width = cap.width;
    imgData.height = cap.height;
    imgData.pixels = cap.pixels;

    char prompt[256];
    wsprintfA(prompt, "Does this image contain '%s'? Return JSON with location if found.",
              imageName);

    VLMVision::VisionResult vlmResult = VLMVision::Query(imgData, prompt);

    if (vlmResult.found && vlmResult.confidence >= s_config.vlm_confidence_threshold) {
        result.found = true;
        result.confidence = vlmResult.confidence;
        result.from_dm = false;
        result.method = "VLM_Query";

        result.x = adjX1 + (int)((vlmResult.x / 100.0f) * (adjX2 - adjX1));
        result.y = adjY1 + (int)((vlmResult.y / 100.0f) * (adjY2 - adjY1));

        s_vlmSuccessCount++;
        s_consecutiveFails = 0;

        Logf("VisionFusion", "VLM found %s @ (%d,%d) conf=%.2f",
             imageName, result.x, result.y, result.confidence);
    } else {
        result.confidence = vlmResult.confidence;
        result.error = vlmResult.error;
        Logf("VisionFusion", "VLM not found %s", imageName);
    }

    int latency = (int)(GetTickCount() - startTime);
    s_totalLatencyMs += latency;

    return result;
}

VisionFusion::FindResult VisionFusion::FindMap(const char* mapName) {
    return FindPic(mapName, 0, 0, 200, 100, 0.8f);
}

VisionFusion::FindResult VisionFusion::FindNPC(const char* npcName) {
    return FindPic(npcName, 0, 0, 1024, 768, 0.85f);
}

VisionFusion::FindResult VisionFusion::FindMonsterHP() {
    return FindPic("mob_hp.png", 0, 0, 1024, 768, 0.75f);
}

VisionFusion::FindResult VisionFusion::FindPlayerHP() {
    return FindPic("Player_pkt.png", 0, 0, 1024, 768, 0.75f);
}

VisionFusion::FindResult VisionFusion::FindShopElement(const char* elementName) {
    return FindPic(elementName, 0, 0, 1024, 768, 0.85f);
}

VisionFusion::FindResult VisionFusion::Find(const char* imageName,
                                            int x1, int y1, int x2, int y2, float sim) {
    return FindPic(imageName, x1, y1, x2, y2, sim);
}

VisionFusion::StateResult VisionFusion::ClassifyState() {
    VisionFusion::StateResult result;

    if (!s_inited) {
        result.error = "Not initialized";
        return result;
    }

    ScreenshotUniversal::CaptureResult cap = ScreenshotUniversal::Capture(s_hWnd);
    if (!cap.success) {
        result.error = "Capture failed";
        return result;
    }

    VLMVision::ImageData imgData;
    imgData.width = cap.width;
    imgData.height = cap.height;
    imgData.pixels = cap.pixels;

    VLMVision::VisionResult vlmResult = VLMVision::ClassifyState(imgData);

    result.success = vlmResult.found;
    result.state = vlmResult.state;
    result.confidence = vlmResult.confidence;
    result.from_dm = false;
    result.error = vlmResult.error;

    Logf("VisionFusion", "ClassifyState: %d, conf=%.2f",
         (int)result.state, result.confidence);

    return result;
}

bool VisionFusion::IsState(VLMVision::GameState state, float minConfidence) {
    VisionFusion::StateResult res = ClassifyState();
    return res.success && res.state == state && res.confidence >= minConfidence;
}

ScreenshotUniversal::GameCoords VisionFusion::FindResult::ToGameCoords(HWND hWnd) {
    return ScreenshotUniversal::ScreenToGame(x, y, hWnd);
}

bool VisionFusion::Click(FindResult& result) {
    if (!result.found || result.x < 0 || result.y < 0) {
        return false;
    }

    if (!s_hWnd) return false;

    ScreenshotUniversal::GameCoords gameCoords = result.ToGameCoords(s_hWnd);

    POINT pt = { result.x, result.y };
    ClientToScreen(s_hWnd, &pt);

    // 使用視窗訊息，不移動真實滑鼠，不影響視窗外工作
    WPARAM moveMask = MK_LBUTTON;
    LPARAM lParam = MAKELPARAM((unsigned short)pt.x, (unsigned short)pt.y);

    PostMessageA(s_hWnd, WM_MOUSEMOVE, 0, lParam);
    Sleep(5);
    PostMessageA(s_hWnd, WM_LBUTTONDOWN, moveMask, lParam);
    Sleep(12);
    PostMessageA(s_hWnd, WM_LBUTTONUP, 0, lParam);

    Logf("VisionFusion", "Clicked @ screen(%d,%d) = game(%d,%d)",
         pt.x, pt.y, gameCoords.x, gameCoords.y);

    return true;
}

void VisionFusion::ClickAtGameCoords(int gameX, int gameY) {
    if (!s_hWnd) return;

    ScreenshotUniversal::ScreenCoords screenCoords =
        ScreenshotUniversal::GameToScreen(gameX, gameY, s_hWnd);

    POINT pt = { screenCoords.x, screenCoords.y };
    ClientToScreen(s_hWnd, &pt);

    // 使用視窗訊息，不移動真實滑鼠，不影響視窗外工作
    WPARAM moveMask = MK_LBUTTON;
    LPARAM lParam = MAKELPARAM((unsigned short)pt.x, (unsigned short)pt.y);

    PostMessageA(s_hWnd, WM_MOUSEMOVE, 0, lParam);
    Sleep(5);
    PostMessageA(s_hWnd, WM_LBUTTONDOWN, moveMask, lParam);
    Sleep(12);
    PostMessageA(s_hWnd, WM_LBUTTONUP, 0, lParam);

    Logf("VisionFusion", "Clicked @ game(%d,%d) = screen(%d,%d)",
         gameX, gameY, pt.x, pt.y);
}

const VisionFusion::TransitionLog* VisionFusion::GetTransitionLog(int index) {
    if (index < 0 || index >= s_logCount) return nullptr;
    int actualIndex = (s_logIndex - s_logCount + index) % 50;
    return &s_logs[actualIndex];
}

int VisionFusion::GetTransitionLogCount() {
    return s_logCount;
}

void VisionFusion::ClearTransitionLog() {
    s_logIndex = 0;
    s_logCount = 0;
}

VisionFusion::Stats VisionFusion::GetStats() {
    Stats stats;
    stats.dm_find_count = s_dmFindCount.load();
    stats.dm_success_count = s_dmSuccessCount.load();
    stats.vlm_fallback_count = s_vlmFallbackCount.load();
    stats.vlm_success_count = s_vlmSuccessCount.load();
    stats.total_latency_ms = s_totalLatencyMs.load();
    stats.consecutive_fails = s_consecutiveFails.load();
    return stats;
}

void VisionFusion::SetDebug(bool enable) {
    s_debug = enable;
}

void VisionFusion::Diagnostic(HWND hWnd) {
    if (!hWnd) hWnd = s_hWnd;

    Log("VisionFusion", "=== Vision Fusion Diagnostic ===");

    ScreenshotUniversal::OSVersion os = ScreenshotUniversal::GetOSVersion();
    Logf("VisionFusion", "OS: %d", (int)os);

    float dpiX = ScreenshotUniversal::GetDPIScaleX(hWnd);
    float dpiY = ScreenshotUniversal::GetDPIScaleY(hWnd);
    Logf("VisionFusion", "DPI Scale: %.2fx%.2f", dpiX, dpiY);

    ScreenshotUniversal::CaptureResult cap = ScreenshotUniversal::Capture(hWnd);
    if (cap.success) {
        Logf("VisionFusion", "Capture OK: %dx%d, Method: %s",
             cap.width, cap.height, ScreenshotUniversal::GetLastMethodName());
    } else {
        Log("VisionFusion", "Capture FAILED");
    }

    Stats stats = GetStats();
    Logf("VisionFusion", "DM: %d/%d, VLM Fallback: %d/%d",
         stats.dm_success_count, stats.dm_find_count,
         stats.vlm_success_count, stats.vlm_fallback_count);
    Logf("VisionFusion", "Consecutive Fails: %d", stats.consecutive_fails);

    Log("VisionFusion", "=== End Diagnostic ===");
}

VisionFusion::FindResult VisionFusion::WaitForPic(const char* imageName,
                                                  int timeoutMs, float sim) {
    VisionFusion::FindResult result;
    DWORD startTime = GetTickCount();

    while ((GetTickCount() - startTime) < (DWORD)timeoutMs) {
        result = FindPic(imageName, 0, 0, 1024, 768, sim);
        if (result.found) {
            return result;
        }
        Sleep(100);
    }

    result.error = "Timeout";
    return result;
}

VisionFusion::StateResult VisionFusion::WaitForState(VLMVision::GameState targetState,
                                                     int timeoutMs) {
    VisionFusion::StateResult result;
    DWORD startTime = GetTickCount();

    while ((GetTickCount() - startTime) < (DWORD)timeoutMs) {
        result = ClassifyState();
        if (result.success && result.state == targetState) {
            return result;
        }
        Sleep(200);
    }

    result.error = "Timeout";
    return result;
}

bool VisionFusion::ClickAndWait(const char* imageName, int waitMs, float sim) {
    VisionFusion::FindResult r = WaitForPic(imageName, 5000, sim);
    if (!r.found) return false;

    Click(r);
    Sleep(waitMs);
    return true;
}

// ============================================================
// 安全碼檢測（使用 NVIDIA VLM API）
// ============================================================
bool VisionFusion::CheckSecurityCode() {
    // 1. 快速本地檢測（窗口標題）
    HWND hCaptcha1 = FindWindowW(NULL, L"轉轉樂安全碼");
    HWND hCaptcha2 = FindWindowW(NULL, L"抽獎轉轉樂");
    if ((hCaptcha1 && IsWindowVisible(hCaptcha1)) ||
        (hCaptcha2 && IsWindowVisible(hCaptcha2))) {
        Log("VisionFusion", "✅ 本地檢測到安全碼視窗");
        return true;
    }

    // 2. 截圖已完全禁用，避免黑屏
    // VLM fallback 也已禁用於 vision_fusion.h
    // 只靠窗口標題檢測
    return false;
}
