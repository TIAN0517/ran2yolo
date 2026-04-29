// ============================================================
// vision_fusion.cpp
// Vision Fusion - DM + VLM 整合層
// ============================================================

#include "vision_fusion.h"
#include "dm_wrapper.h"
#include <algorithm>

// ============================================================
// 內部狀態
// ============================================================
namespace {
    static HWND s_hWnd = NULL;
    static FusionConfig s_config;
    static bool s_inited = false;
    static bool s_debug = false;

    // 統計
    static std::atomic<int> s_dmFindCount{0};
    static std::atomic<int> s_dmSuccessCount{0};
    static std::atomic<int> s_vlmFallbackCount{0};
    static std::atomic<int> s_vlmSuccessCount{0};
    static std::atomic<int> s_totalLatencyMs{0};
    static std::atomic<int> s_consecutiveFails{0};

    // 日誌
    static TransitionLog s_logs[50];
    static int s_logIndex = 0;
    static int s_logCount = 0;

    // 最後截圖（用於 VLM）
    static ScreenshotUniversal::CaptureResult s_lastCapture;
    static VLMVision::ImageData s_lastImageData;

    // 診斷日誌
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

    // 記錄 Transition Log
    static void RecordLog(const char* fromState, const char* toState,
                          const char* event, bool guardPassed,
                          float guardConfidence, const FindResult& findResult) {
        TransitionLog& log = s_logs[s_logIndex % 50];
        log.timestamp_ms = GetTickCount();
        log.from_state = fromState;
        log.to_state = toState;
        log.event = event;
        log.guard_passed = guardPassed;
        log.guard_confidence = guardConfidence;
        log.last_find_result = findResult;

        s_logIndex++;
        if (s_logCount < 50) s_logCount++;
    }

    // 更新統計
    static void UpdateStats(bool fromDM, bool success, int latencyMs) {
        if (fromDM) {
            s_dmFindCount++;
            if (success) s_dmSuccessCount++;
        } else {
            s_vlmFallbackCount++;
            if (success) s_vlmSuccessCount++;
        }
        s_totalLatencyMs += latencyMs;
        if (success) {
            s_consecutiveFails = 0;
        } else {
            s_consecutiveFails++;
        }
    }
}

// ============================================================
// 初始化
// ============================================================
bool VisionFusion::Init(HWND hWnd, const FusionConfig& config) {
    if (!hWnd || !IsWindow(hWnd)) {
        return false;
    }

    s_hWnd = hWnd;
    s_config = config;

    // 初始化截圖系統
    if (!ScreenshotUniversal::Init(hWnd)) {
        Log("VisionFusion", "Failed to init screenshot");
        return false;
    }

    // 初始化 DM
    if (!g_dm.Init()) {
        Log("VisionFusion", "Failed to init DM");
        // DM 失敗不影響大局
    } else {
        g_dm.BindWindow(hWnd);
        g_dm.SetPath("C:\\Users\\tian7\\Desktop\\ahk\\");
    }

    s_inited = true;
    Log("VisionFusion", "Initialized successfully");

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

// ============================================================
// 找圖實現
// ============================================================
VisionFusion::FindResult VisionFusion::FindPic(const char* imageName,
                                                int x1, int y1, int x2, int y2, float sim) {
    FindResult result;
    result.image_name = imageName;

    if (!s_inited) {
        result.error = "Not initialized";
        return result;
    }

    DWORD startTime = GetTickCount();
    int w, h;
    ScreenshotUniversal::GetClientSize(s_hWnd, &w, &h);

    // 動態調整座標（支援高 DPI）
    float scaleX = (float)w / 1024.0f;
    float scaleY = (float)h / 768.0f;

    int adjX1 = (int)(x1 * scaleX);
    int adjY1 = (int)(y1 * scaleY);
    int adjX2 = (int)(x2 * scaleX);
    int adjY2 = (int)(y2 * scaleY);

    // 優先使用 DM
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

    // DM 失敗，檢查是否需要 VLM fallback
    s_consecutiveFails++;
    bool shouldFallback = (s_config.enable_vlm_fallback &&
                          s_consecutiveFails >= s_config.consecutive_fail_threshold);

    if (!shouldFallback) {
        result.confidence = sim * 0.5f;  // 降低信心度
        result.method = "DM_FindPic";
        Logf("VisionFusion", "DM not found %s (consecutive fails: %d)",
             imageName, (int)s_consecutiveFails);
        return result;
    }

    // VLM fallback
    s_vlmFallbackCount++;
    Logf("VisionFusion", "VLM fallback for %s", imageName);

    // 截圖
    ScreenshotUniversal::CaptureResult cap = ScreenshotUniversal::Capture(s_hWnd);
    if (!cap.success) {
        result.error = "Capture failed";
        return result;
    }

    s_lastCapture = cap;

    // 構建 ImageData
    VLMVision::ImageData imgData;
    imgData.width = cap.width;
    imgData.height = cap.height;
    imgData.pixels = cap.pixels;

    // 詢問 VLM
    char prompt[256];
    wsprintfA(prompt, "Does this image contain '%s'? Return JSON with location if found.",
              imageName);

    VLMVision::VisionResult vlmResult = VLMVision::Query(imgData, prompt);

    if (vlmResult.found && vlmResult.confidence >= s_config.vlm_confidence_threshold) {
        result.found = true;
        result.confidence = vlmResult.confidence;
        result.from_dm = false;
        result.method = "VLM_Query";

        // VLM 返回的座標需要轉換
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

// ============================================================
// 快捷找圖方法
// ============================================================
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

// ============================================================
// 狀態分類
// ============================================================
VisionFusion::StateResult VisionFusion::ClassifyState() {
    StateResult result;

    if (!s_inited) {
        result.error = "Not initialized";
        return result;
    }

    // 截圖
    ScreenshotUniversal::CaptureResult cap = ScreenshotUniversal::Capture(s_hWnd);
    if (!cap.success) {
        result.error = "Capture failed";
        return result;
    }

    // 構建 ImageData
    VLMVision::ImageData imgData;
    imgData.width = cap.width;
    imgData.height = cap.height;
    imgData.pixels = cap.pixels;

    // 呼叫 VLM 分類
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
    StateResult result = ClassifyState();
    return result.success &&
           result.state == state &&
           result.confidence >= minConfidence;
}

// ============================================================
// 點擊
// ============================================================
bool VisionFusion::Click(FindResult& result) {
    if (!result.found || result.x < 0 || result.y < 0) {
        return false;
    }

    if (!s_hWnd) return false;

    // 轉換為遊戲座標
    ScreenshotUniversal::GameCoords gameCoords = result.ToGameCoords(s_hWnd);

    // 使用 SendInput 點擊
    POINT pt = { result.x, result.y };
    ClientToScreen(s_hWnd, &pt);

    int absX = (int)((pt.x * 65535) / GetSystemMetrics(SM_CXSCREEN));
    int absY = (int)((pt.y * 65535) / GetSystemMetrics(SM_CYSCREEN));

    INPUT inputs[3] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    inputs[0].mi.dx = absX;
    inputs[0].mi.dy = absY;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE;
    inputs[1].mi.dx = absX;
    inputs[1].mi.dy = absY;
    inputs[2].type = INPUT_MOUSE;
    inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE;
    inputs[2].mi.dx = absX;
    inputs[2].mi.dy = absY;

    SendInput(3, inputs, sizeof(INPUT));

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

    int absX = (int)((pt.x * 65535) / GetSystemMetrics(SM_CXSCREEN));
    int absY = (int)((pt.y * 65535) / GetSystemMetrics(SM_CYSCREEN));

    INPUT inputs[3] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    inputs[0].mi.dx = absX;
    inputs[0].mi.dy = absY;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE;
    inputs[1].mi.dx = absX;
    inputs[1].mi.dy = absY;
    inputs[2].type = INPUT_MOUSE;
    inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE;
    inputs[2].mi.dx = absX;
    inputs[2].mi.dy = absY;

    SendInput(3, inputs, sizeof(INPUT));

    Logf("VisionFusion", "Clicked @ game(%d,%d) = screen(%d,%d)",
         gameX, gameY, pt.x, pt.y);
}

// ============================================================
// 日誌
// ============================================================
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

// ============================================================
// 統計
// ============================================================
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

// ============================================================
// 調試
// ============================================================
void VisionFusion::SetDebug(bool enable) {
    s_debug = enable;
}

void VisionFusion::Diagnostic(HWND hWnd) {
    if (!hWnd) hWnd = s_hWnd;

    Log("VisionFusion", "=== Vision Fusion Diagnostic ===");

    // OS 版本
    ScreenshotUniversal::OSVersion os = ScreenshotUniversal::GetOSVersion();
    Logf("VisionFusion", "OS: %d", (int)os);

    // DPI
    float dpiX = ScreenshotUniversal::GetDPIScaleX(hWnd);
    float dpiY = ScreenshotUniversal::GetDPIScaleY(hWnd);
    Logf("VisionFusion", "DPI Scale: %.2fx%.2f", dpiX, dpiY);

    // 截圖測試
    ScreenshotUniversal::CaptureResult cap = ScreenshotUniversal::Capture(hWnd);
    if (cap.success) {
        Logf("VisionFusion", "Capture OK: %dx%d, Method: %s",
             cap.width, cap.height, ScreenshotUniversal::GetLastMethodName());
    } else {
        Log("VisionFusion", "Capture FAILED");
    }

    // 統計
    Stats stats = GetStats();
    Logf("VisionFusion", "DM: %d/%d, VLM Fallback: %d/%d",
         stats.dm_success_count, stats.dm_find_count,
         stats.vlm_success_count, stats.vlm_fallback_count);
    Logf("VisionFusion", "Consecutive Fails: %d", stats.consecutive_fails);

    Log("VisionFusion", "=== End Diagnostic ===");
}

// ============================================================
// 快捷方法
// ============================================================
VisionFusion::FindResult VisionFusion::WaitForPic(const char* imageName,
                                                 int timeoutMs, float sim) {
    FindResult result;
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
    StateResult result;
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
    FindResult r = WaitForPic(imageName, 5000, sim);
    if (!r.found) return false;

    Click(r);
    Sleep(waitMs);
    return true;
}
