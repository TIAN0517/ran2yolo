#pragma once
#ifndef _VISION_API_ROTATOR_H_
#define _VISION_API_ROTATOR_H_

#include <windows.h>
#include <string>
#include <vector>
#include <atomic>

// ============================================================
// Vision API Types
// ============================================================
enum class VisionApiType {
    TENCENT = 0,    // 騰訊雲視覺（首選）
    INTEL = 1,      // 英特爾雲視覺（輪替）
    ALIBABA = 2,    // 阿里雲視覺（備援）
    BAIDU = 3       // 百度AI（最後備援）
};

// ============================================================
// API Rotator Class
// ============================================================
class VisionApiRotator {
public:
    VisionApiRotator();

    // 取得下一個要使用的 API（輪替）
    VisionApiType GetNextApi();

    // 回報某個 API 失敗，自動切下一個
    void ReportFailure(VisionApiType api);

    // 成功時重置失敗計數
    void ReportSuccess(VisionApiType api);

    // 手動切換到指定 API
    void SetCurrentApi(VisionApiType api);

    // 取得 API 名稱
    const char* GetApiName(VisionApiType api) const;

    // 檢查是否應該使用雲端 API
    bool ShouldUseCloudApi() const;

    // 取得當前失敗次數
    int GetFailureCount(VisionApiType api) const;

private:
    std::vector<VisionApiType> m_apis;
    std::atomic<size_t> m_currentIndex{0};
    int m_failureCount[4] = {0};   // 每個 API 的連續失敗次數
    DWORD m_lastFailureTime = 0;
    static constexpr int MAX_CONSECUTIVE_FAILURES = 3;
};

// ============================================================
// Singleton accessor
// ============================================================
VisionApiRotator& GetVisionApiRotator();

// ============================================================
// Vision APIs
// ============================================================
namespace TencentVision {
    // 初始化騰訊雲配置
    void Init(const char* secret_id, const char* secret_key);

    // 檢查是否是安全碼視窗
    bool IsSecurityCodeWindow(int screenshotData, int width, int height);

    // 圖像分類
    std::string ClassifyImage(int screenshotData, int width, int height);

    // OCR 文字識別
    std::string ExtractText(int screenshotData, int width, int height);
}

namespace IntelCloudVision {
    // 初始化英特爾雲配置
    void Init(const char* api_endpoint, const char* api_key);

    // 圖像理解分類
    bool ClassifyImage(int screenshotData, int width, int height, const char* target_class);

    // 檢查是否包含安全碼 UI
    bool IsSecurityCodeWindow(int screenshotData, int width, int height);
}

namespace AlibabaVision {
    // 初始化阿里雲配置
    void Init(const char* access_key_id, const char* access_key_secret);

    // 圖像分類
    std::string ClassifyImage(int screenshotData, int width, int height);

    // OCR 文字識別
    std::string ExtractText(int screenshotData, int width, int height);
}

namespace BaiduAI {
    // 初始化百度AI配置
    void Init(const char* api_key, const char* secret_key);

    // OCR 文字識別
    std::string ExtractText(int screenshotData, int width, int height);

    // 通用物體檢測
    std::string DetectObjects(int screenshotData, int width, int height);
}

// ============================================================
// Unified vision check function
// ============================================================
struct VisionCheckResult {
    bool is_captcha = false;
    const char* detected_by = nullptr;
    float confidence = 0.0f;
    std::string raw_result;
};

// 使用輪替器檢查視覺
VisionCheckResult CheckWithRotator(int screenshotData, int width, int height);

#endif // _VISION_API_ROTATOR_H_
