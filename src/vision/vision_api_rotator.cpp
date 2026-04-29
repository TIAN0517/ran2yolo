// ============================================================
// vision_api_rotator.cpp
// API 輪替器實現 - 取代 VLM 視覺系統
// ============================================================

#include "vision_api_rotator.h"
#include "../core/bot_logic.h"
#include <ctime>
#include <algorithm>

// ============================================================
// API 配置（請填入實際的金鑰）
// ============================================================
namespace Config {
    // Tencent Cloud
    static const char* g_tencent_secret_id = "";
    static const char* g_tencent_secret_key = "";

    // Intel Cloud
    static const char* g_intel_endpoint = "https://ai.eu.ilab.intel.com/v1/chat/completions";
    static const char* g_intel_api_key = "";

    // Alibaba Cloud
    static const char* g_alibaba_access_key_id = "";
    static const char* g_alibaba_access_key_secret = "";

    // Baidu AI
    static const char* g_baidu_api_key = "";
    static const char* g_baidu_secret_key = "";
}

// ============================================================
// VisionApiRotator 實現
// ============================================================
VisionApiRotator::VisionApiRotator() {
    m_apis = {VisionApiType::TENCENT, VisionApiType::INTEL,
              VisionApiType::ALIBABA, VisionApiType::BAIDU};
}

VisionApiType VisionApiRotator::GetNextApi() {
    size_t idx = m_currentIndex.load();
    return m_apis[idx % m_apis.size()];
}

void VisionApiRotator::ReportFailure(VisionApiType api) {
    int idx = static_cast<int>(api);
    m_failureCount[idx]++;

    if (m_failureCount[idx] >= MAX_CONSECUTIVE_FAILURES) {
        // 連續失敗太多，切到下一個 API
        m_currentIndex = (m_currentIndex + 1) % m_apis.size();
        m_failureCount[idx] = 0;
        m_lastFailureTime = GetTickCount();
        Logf("Vision", "[輪替] %s 連續失敗，切換到下一個 API",
             GetApiName(api));
    }
}

void VisionApiRotator::ReportSuccess(VisionApiType api) {
    int idx = static_cast<int>(api);
    m_failureCount[idx] = 0;
}

void VisionApiRotator::SetCurrentApi(VisionApiType api) {
    for (size_t i = 0; i < m_apis.size(); i++) {
        if (m_apis[i] == api) {
            m_currentIndex = i;
            break;
        }
    }
}

const char* VisionApiRotator::GetApiName(VisionApiType api) const {
    switch (api) {
        case VisionApiType::TENCENT: return "Tencent";
        case VisionApiType::INTEL: return "Intel";
        case VisionApiType::ALIBABA: return "Alibaba";
        case VisionApiType::BAIDU: return "Baidu";
        default: return "Unknown";
    }
}

bool VisionApiRotator::ShouldUseCloudApi() const {
    // 如果上次失敗在 5 秒內，不使用雲端 API
    if (GetTickCount() - m_lastFailureTime < 5000) {
        return false;
    }
    return true;
}

int VisionApiRotator::GetFailureCount(VisionApiType api) const {
    return m_failureCount[static_cast<int>(api)];
}

// Singleton
static VisionApiRotator s_rotator;
VisionApiRotator& GetVisionApiRotator() {
    return s_rotator;
}

// ============================================================
// Tencent Vision 實現
// 使用騰訊雲 OCR 檢測安全碼文字
// ============================================================
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

namespace TencentVision {
    void Init(const char* secret_id, const char* secret_key) {
        Config::g_tencent_secret_id = secret_id;
        Config::g_tencent_secret_key = secret_key;
        Log("Vision", "Tencent Cloud Vision 初始化");
    }

    static std::string HttpPostJson(const char* url, const char* body,
                                     const char* secret_id, const char* secret_key) {
        std::string result;

        URL_COMPONENTSW urlComp = {};
        urlComp.dwStructSize = sizeof(urlComp);
        wchar_t host[256] = {};
        wchar_t path[256] = {};
        urlComp.lpszHostName = host;
        urlComp.lpszUrlPath = path;
        urlComp.dwHostNameLength = 256;
        urlComp.dwUrlPathLength = 256;

        // 轉換 URL 為 Unicode
        wchar_t wurl[512] = {};
        MultiByteToWideChar(CP_ACP, 0, url, -1, wurl, 512);

        if (!WinHttpCrackUrl(wurl, 0, 0, &urlComp)) {
            return result;
        }

        HINTERNET hSession = WinHttpOpen(L"JyTrainer/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return result;

        HINTERNET hConnect = WinHttpConnect(hSession, host,
            urlComp.nPort, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

        BOOL bSSL = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST",
            path, NULL, NULL, NULL,
            bSSL ? WINHTTP_FLAG_SECURE : 0);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return result;
        }

        std::wstring headers = L"Content-Type: application/json\r\n";
        // 轉換 body 為 ANSI
        char ansiBody[8192] = {};
        WideCharToMultiByte(CP_ACP, 0, L"{body}", -1, ansiBody, sizeof(ansiBody), NULL, NULL);
        strcpy_s(ansiBody, body);

        WinHttpSendRequest(hRequest, headers.c_str(), headers.length(),
            ansiBody, strlen(ansiBody), strlen(ansiBody), 0);

        WinHttpReceiveResponse(hRequest, NULL);

        char buffer[8192];
        DWORD bytesRead = 0;
        while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            result.append(buffer, bytesRead);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        return result;
    }

    bool IsSecurityCodeWindow(int screenshotData, int width, int height) {
        // 快速本地檢測（優先）
        HWND hCaptcha1 = FindWindowW(NULL, L"轉轉樂安全碼");
        HWND hCaptcha2 = FindWindowW(NULL, L"抽獎轉轉樂");
        if ((hCaptcha1 && IsWindowVisible(hCaptcha1)) ||
            (hCaptcha2 && IsWindowVisible(hCaptcha2))) {
            return true;
        }

        // 如果配置了 API key，使用騰訊雲 OCR
        if (!Config::g_tencent_secret_id || strlen(Config::g_tencent_secret_id) == 0) {
            return false;
        }

        // TODO: 實現騰訊雲 OCR 調用
        // 騰訊雲 OCR API 文檔：https://cloud.tencent.com/document/product/866/17600
        // 1. 獲取簽名
        // 2. 上傳截圖
        // 3. 調用 OCR API
        // 4. 檢測關鍵字：轉轉樂、安全碼、驗證、輸入

        return false;
    }

    std::string ClassifyImage(int screenshotData, int width, int height) {
        // 使用騰訊雲圖像分析
        return "";
    }

    std::string ExtractText(int screenshotData, int width, int height) {
        // 使用騰訊雲 OCR
        // POST https://ocr.ap-shanghai.tencentcloudapi.com
        // Action=GeneralBasicOCR
        return "";
    }
}

// ============================================================
// Intel Cloud Vision 實現
// ============================================================
namespace IntelCloudVision {
    void Init(const char* api_endpoint, const char* api_key) {
        Config::g_intel_endpoint = api_endpoint;
        Config::g_intel_api_key = api_key;
        Log("Vision", "Intel Cloud Vision 初始化");
    }

    bool ClassifyImage(int screenshotData, int width, int height, const char* target_class) {
        // TODO: 實現英特爾雲 API 調用
        // 使用 Intel AI API 進行圖像分類
        Log("Vision", "[Intel] ClassifyImage 尚未實現");
        return false;
    }

    bool IsSecurityCodeWindow(int screenshotData, int width, int height) {
        // TODO: 實現安全碼檢測
        Log("Vision", "[Intel] IsSecurityCodeWindow 尚未實現");
        return false;
    }
}

// ============================================================
// Alibaba Vision 實現
// ============================================================
namespace AlibabaVision {
    void Init(const char* access_key_id, const char* access_key_secret) {
        Config::g_alibaba_access_key_id = access_key_id;
        Config::g_alibaba_access_key_secret = access_key_secret;
        Log("Vision", "Alibaba Cloud Vision 初始化");
    }

    std::string ClassifyImage(int screenshotData, int width, int height) {
        // TODO: 實現阿里雲 API 調用
        return "";
    }

    std::string ExtractText(int screenshotData, int width, int height) {
        // TODO: 實現阿里雲 OCR
        return "";
    }
}

// ============================================================
// Baidu AI 實現
// ============================================================
namespace BaiduAI {
    void Init(const char* api_key, const char* secret_key) {
        Config::g_baidu_api_key = api_key;
        Config::g_baidu_secret_key = secret_key;
        Log("Vision", "Baidu AI 初始化");
    }

    std::string ExtractText(int screenshotData, int width, int height) {
        // TODO: 實現百度 OCR
        return "";
    }

    std::string DetectObjects(int screenshotData, int width, int height) {
        // TODO: 實現百度物體檢測
        return "";
    }
}

// ============================================================
// 統一檢查函數
// ============================================================
VisionCheckResult CheckWithRotator(int screenshotData, int width, int height) {
    VisionCheckResult result = {false, nullptr, 0.0f, ""};
    VisionApiRotator& rotator = GetVisionApiRotator();

    if (!rotator.ShouldUseCloudApi()) {
        return result;
    }

    VisionApiType currentApi = rotator.GetNextApi();
    bool detected = false;

    switch (currentApi) {
        case VisionApiType::TENCENT:
            detected = TencentVision::IsSecurityCodeWindow(screenshotData, width, height);
            result.detected_by = "Tencent";
            break;

        case VisionApiType::INTEL:
            detected = IntelCloudVision::IsSecurityCodeWindow(screenshotData, width, height);
            result.detected_by = "Intel";
            break;

        case VisionApiType::ALIBABA:
            detected = !AlibabaVision::ClassifyImage(screenshotData, width, height).empty();
            result.detected_by = "Alibaba";
            break;

        case VisionApiType::BAIDU:
            detected = !BaiduAI::ExtractText(screenshotData, width, height).empty();
            result.detected_by = "Baidu";
            break;
    }

    if (detected) {
        result.is_captcha = true;
        result.confidence = 0.9f;
        rotator.ReportSuccess(currentApi);
        Logf("Vision", "[輪替] %s 檢測到安全碼", rotator.GetApiName(currentApi));
    } else {
        rotator.ReportFailure(currentApi);
    }

    return result;
}
