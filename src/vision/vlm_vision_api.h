#pragma once
#ifndef _VLM_VISION_API_H_
#define _VLM_VISION_API_H_

// ============================================================
// [已棄用] VLM Vision API
// 請使用 vision_api_rotator.h 代替
// ============================================================
#include "vision_api_rotator.h"

#include <windows.h>
#include <string>
#include <vector>

// ============================================================
// VLM Vision API Interface (Legacy)
// Used as DM vision fallback when DM fails
// ============================================================

namespace VLMVision {

// ============================================================
// Allowed States (for state classification)
// ============================================================
enum class GameState {
    UNKNOWN          = 0,
    LOGIN            = 1,
    SERVER_SELECT    = 2,
    LOADING         = 3,
    SPAWN           = 4,
    HUNTING         = 5,
    DIALOG          = 6,
    SHOP            = 7,
    DEAD            = 8,
    RETURNING       = 9,
    TOWN_SUPPLY     = 10,
    COMBAT          = 11,
    WAYPOINT        = 12,
    CAPTCHA         = 13  // 安全碼/驗證碼視窗
};

// ============================================================
// Vision Result
// ============================================================
struct VisionResult {
    bool found = false;
    GameState state = GameState::UNKNOWN;
    float confidence = 0.0f;
    int x = -1;
    int y = -1;
    int width = 0;
    int height = 0;
    std::vector<std::string> evidence;
    std::string error;
};

// ============================================================
// Image Data Structure
// ============================================================
struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // BGRA format
    std::string sha256;
};

// ============================================================
// VLM Config
// ============================================================
struct VLMConfig {
    std::string api_endpoint = "https://integrate.api.nvidia.com/v1/chat/completions";

    // ★ 免費推薦模型（已挑選最適合你的用途）
    std::string model_name = "mistralai/mistral-small-4-119b-2603";

    std::string api_key = "";   // 使用內建 6 Key 輪替
    int connect_timeout_ms = 3000;
    int read_timeout_ms = 10000;
    int max_retries = 2;
    int retry_delay_ms = 500;

    std::string system_prompt = R"(
你是 Ran2 Online（亂2 Online）遊戲專家，負責分析遊戲截圖判斷 bot 是否被卡住。

## 任務
分析截圖，判斷是否出現「轉轉樂安全碼」或「RANRI」驗證視窗。
如果出現，請輸出以下 JSON：
{
  "current_blocking_issue": "SecurityCode",
  "suggested_action": "PauseForCaptcha",
  "confidence": 0.0~1.0
}

如果沒有出現驗證碼視窗，請正常輸出遊戲狀態（HUNTING / DEAD / DIALOG / SHOP 等）。

## 驗證碼特徵（務必牢記）
- 黑色彈出視窗，中央有大大的彩色文字 "RANRI"
- 下面有兩排格子：
  第一排字母：H N E I M T R L C A
  第二排數字：4 8 2 6 0 9 5 3 1 7
- 有輸入框 + 「確定」按鈕
- 視窗標題可能顯示「轉轉樂安全碼」

只要看到以上任何特徵，就判定為 SecurityCode。
)";

    bool simple_mode = false;
    std::string simple_prompt = "";
};

// ============================================================
// API Functions
// ============================================================

bool Init(const VLMConfig& config);
void SetConfig(const VLMConfig& config);
VLMConfig GetConfig();
void Shutdown();
bool IsInited();

// Vision Recognition
VisionResult ClassifyState(const ImageData& image);
VisionResult ClassifyStateFromFile(const char* filepath);
VisionResult Query(const ImageData& image, const char* prompt);
std::vector<VisionResult> BatchQuery(const std::vector<ImageData>& images, const char* prompt);

// ============================================================
// Simple: Yes/No Check
// ============================================================
struct YesNoResult {
    bool answer = false;
    float confidence = 0.0f;
    std::string reason;
};

YesNoResult CheckPresence(const ImageData& image, const char* question);

// ============================================================
// HTTP Helper
// ============================================================
struct HTTPResponse {
    int status_code = 0;
    std::string body;
    std::string error;
};

HTTPResponse PostJSON(const char* url, const char* json_body,
                      int timeout_ms);

// ============================================================
// Debug
// ============================================================
const char* GetLastError();
void SetDebug(bool enable);
bool TestConnection();

// ============================================================
// History Tracking
// ============================================================
struct CallRecord {
    DWORD timestamp_ms = 0;
    std::string prompt_hash;
    int image_size = 0;
    VisionResult result;
    int latency_ms = 0;
};

const CallRecord* GetCallHistory(int index);
int GetCallHistoryCount();

} // namespace VLMVision

#endif // _VLM_VISION_API_H_
