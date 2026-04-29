// ============================================================
// vision_fusion.h
// Vision Fusion - DM + Multi-scale Template Matching
// ============================================================
#pragma once
#ifndef _VISION_FUSION_H_
#define _VISION_FUSION_H_

#include <windows.h>
#include <string>
#include <atomic>

namespace VisionFusion {

// ============================================================
// Configuration
// ============================================================
struct FusionConfig {
    bool enable_dm = true;
    bool enable_multiscale_template = true;  // Enable multi-scale template matching
    bool use_vlm_api = false;
    bool enable_vlm_fallback = false;        // ⚠️ 預設關閉（避免閃黑屏，需要時手動開啟）
    int consecutive_fail_threshold = 3;
    float vlm_confidence_threshold = 0.75f;
    float multiscale_min_scale = 0.8f;   // Minimum scale factor
    float multiscale_max_scale = 1.2f;    // Maximum scale factor
    float multiscale_scale_step = 0.05f;  // Scale step
    std::string vlm_api_key;
    std::string vlm_endpoint;
};

// ============================================================
// Multi-scale Template Match Result
// ============================================================
struct TemplateMatchResult {
    bool found = false;
    int x = -1;              // Best match X
    int y = -1;              // Best match Y
    int width = 0;           // Template width at best match
    int height = 0;          // Template height at best match
    float scale = 1.0f;      // Best scale factor
    float confidence = 0.0f; // Match confidence (0-1)
};

// ============================================================
// Find Result
// ============================================================
struct FindResult {
    bool found = false;
    int x = -1;
    int y = -1;
    float confidence = 0.0f;
    bool from_dm = true;
    std::string method;
    std::string image_name;
    std::string error;
};

// ============================================================
// State Result
// ============================================================
struct StateResult {
    bool success = false;
    int state = 0;
    float confidence = 0.0f;
    bool from_dm = false;
    std::string error;
};

// ============================================================
// Transition Log
// ============================================================
struct TransitionLog {
    DWORD timestamp_ms = 0;
    std::string from_state;
    std::string to_state;
    std::string event;
    bool guard_passed = false;
    float guard_confidence = 0.0f;
    FindResult last_find_result;
};

// ============================================================
// API Functions
// ============================================================
bool Init(HWND hWnd, const FusionConfig& config);
void SetConfig(const FusionConfig& config);
FusionConfig GetConfig();
void Shutdown();
bool ReInit(HWND hWnd);

FindResult FindPic(const char* imageName, int x1, int y1, int x2, int y2, float sim);
FindResult FindMap(const char* mapName);
FindResult FindNPC(const char* npcName);
FindResult FindMonsterHP();
FindResult FindPlayerHP();
FindResult FindShopElement(const char* elementName);
FindResult Find(const char* imageName, int x1, int y1, int x2, int y2, float sim);

// Multi-scale template matching (no DM required)
TemplateMatchResult MultiScaleTemplateMatch(
    const uint8_t* imagePixels, int imageW, int imageH,
    const uint8_t* templatePixels, int templateW, int templateH,
    int searchX1, int searchY1, int searchX2, int searchY2,
    float minSim
);

// Multi-scale FindPic (uses internal capture + template matching)
FindResult MultiScaleFindPic(const char* templateImageName,
                              int x1, int y1, int x2, int y2, float sim);

StateResult ClassifyState();
bool IsState(int state, float minConfidence);

bool Click(FindResult& result);
void ClickAtGameCoords(int gameX, int gameY);

const TransitionLog* GetTransitionLog(int index);
int GetTransitionLogCount();
void ClearTransitionLog();

struct Stats {
    int dm_find_count = 0;
    int dm_success_count = 0;
    int multiscale_count = 0;
    int multiscale_success_count = 0;
    int vlm_fallback_count = 0;
    int total_latency_ms = 0;
    int consecutive_fails = 0;
};
Stats GetStats();

void SetDebug(bool enable);
void Diagnostic(HWND hWnd);

FindResult WaitForPic(const char* imageName, int timeoutMs, float sim);
StateResult WaitForState(int targetState, int timeoutMs);
bool ClickAndWait(const char* imageName, int waitMs, float sim);

// 安全碼檢測（使用 NVIDIA VLM API）
bool CheckSecurityCode();

} // namespace VisionFusion

#endif // _VISION_FUSION_H_
