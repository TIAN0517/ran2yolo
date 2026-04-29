// ============================================================
// vlm_vision_api.cpp
// VLM Vision API Implementation
// Using HTTP + JSON to call VLM model
// ============================================================

#include "vlm_vision_api.h"
#include "nvidia_key_rotator.h"
#include "screenshot_universal.h"
#include <winhttp.h>
#include <algorithm>

// ============================================================
// Internal State
// ============================================================
namespace {
    static VLMVision::VLMConfig s_config;
    static bool s_inited = false;
    static bool s_debug = false;
    static std::string s_lastError;

    static VLMVision::CallRecord s_history[20];
    static int s_historyIndex = 0;
    static int s_historyCount = 0;

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
// Base64 Encoding
// ============================================================
namespace {
    static const char s_base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    static std::string Base64Encode(const uint8_t* data, size_t size) {
        std::string result;
        int i = 0;
        int j = 0;
        uint8_t char_array_3[3];
        uint8_t char_array_4[4];

        while (size--) {
            char_array_3[i++] = *(data++);
            if (i == 3) {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;

                for (i = 0; i < 4; i++) {
                    result += s_base64_chars[char_array_4[i]];
                }
                i = 0;
            }
        }

        if (i) {
            for (j = i; j < 3; j++) {
                char_array_3[j] = 0;
            }

            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

            for (j = 0; j < i + 1; j++) {
                result += s_base64_chars[char_array_4[j]];
            }

            while ((i++ < 3)) {
                result += '=';
            }
        }

        return result;
    }
}

// ============================================================
// HTTP POST (WinHTTP)
// 使用 NVIDIA Key Rotator 自動輪替
// ============================================================
VLMVision::HTTPResponse VLMVision::PostJSON(const char* url, const char* json_body,
                                             int timeout_ms) {
    HTTPResponse response;

    // 使用 Key Rotator 取得目前有效的 Key
    std::string currentKey = GetNvidiaKeyRotator().GetCurrentKey();

    URL_COMPONENTS urlcomp = {};
    urlcomp.dwStructSize = sizeof(urlcomp);
    urlcomp.dwSchemeLength = -1;
    urlcomp.dwHostNameLength = -1;
    urlcomp.dwUrlPathLength = -1;

    wchar_t wurl[1024] = {0};
    MultiByteToWideChar(CP_ACP, 0, url, -1, wurl, 1024);

    if (!WinHttpCrackUrl(wurl, 0, 0, &urlcomp)) {
        response.error = "Failed to parse URL";
        return response;
    }

    HINTERNET hSession = WinHttpOpen(L"VLMVision/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        NULL, 0);
    if (!hSession) {
        response.error = "WinHttpOpen failed";
        GetNvidiaKeyRotator().ReportFailure();  // 回報失敗
        return response;
    }

    DWORD timeout = timeout_ms;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

    std::wstring host(urlcomp.lpszHostName, urlcomp.dwHostNameLength);
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
        urlcomp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        response.error = "WinHttpConnect failed";
        GetNvidiaKeyRotator().ReportFailure();  // 回報失敗
        return response;
    }

    std::wstring path(urlcomp.lpszUrlPath, urlcomp.dwUrlPathLength);
    if (urlcomp.dwExtraInfoLength > 0) {
        path += std::wstring(urlcomp.lpszExtraInfo, urlcomp.dwExtraInfoLength);
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
        NULL, NULL, NULL,
        (urlcomp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        response.error = "WinHttpOpenRequest failed";
        GetNvidiaKeyRotator().ReportFailure();  // 回報失敗
        return response;
    }

    // 使用輪替器取得的 Key
    std::wstring auth_header = L"Bearer ";
    wchar_t wkey[256] = {0};
    MultiByteToWideChar(CP_ACP, 0, currentKey.c_str(), -1, wkey, 256);
    auth_header += wkey;

    WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/json",
        (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    WinHttpAddRequestHeaders(hRequest, auth_header.c_str(),
        (DWORD)auth_header.length(), WINHTTP_ADDREQ_FLAG_ADD);

    BOOL ok = WinHttpSendRequest(hRequest,
        NULL, 0,
        (void*)json_body, (DWORD)strlen(json_body),
        (DWORD)strlen(json_body), 0);

    if (!ok) {
        response.error = "WinHttpSendRequest failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        GetNvidiaKeyRotator().ReportFailure();  // 回報失敗
        return response;
    }

    DWORD status_code = 0;
    DWORD status_code_len = sizeof(status_code);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        NULL, &status_code, &status_code_len, NULL);
    response.status_code = (int)status_code;

    std::string body;
    char buf[4096];
    DWORD bytes_read = 0;
    while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytes_read) && bytes_read > 0) {
        body.append(buf, bytes_read);
    }
    response.body = body;

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    // 檢查 HTTP 狀態碼，如果是 rate limit 就回報失敗
    if (status_code == 429 || status_code == 401 || status_code == 403) {
        GetNvidiaKeyRotator().ReportFailure();  // 回報失敗，輪替到下一個 Key
    } else {
        GetNvidiaKeyRotator().ReportSuccess();  // 成功，重置計數
    }

    return response;
}

// ============================================================
// JSON Parsing Helpers
// ============================================================
namespace {
    static std::string ExtractJSON(const std::string& text) {
        size_t start = text.find('{');
        size_t end = text.find_last_of('}');

        if (start != std::string::npos && end != std::string::npos && end > start) {
            return text.substr(start, end - start + 1);
        }

        return text;
    }

    static std::string EscapeJSONString(const std::string& s) {
        std::string result;
        result.reserve(s.size() + 16);
        for (char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
            }
        }
        return result;
    }

    static std::string ParseJSONString(const std::string& json, const char* key) {
        std::string pattern = std::string("\"") + key + "\"";
        size_t pos = json.find(pattern);
        if (pos == std::string::npos) return "";

        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";

        pos = json.find('"', pos + 1);
        if (pos == std::string::npos) return "";

        size_t end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";

        return json.substr(pos + 1, end - pos - 1);
    }

    static float ParseJSONFloat(const std::string& json, const char* key, float def = 0.0f) {
        std::string pattern = std::string("\"") + key + "\"";
        size_t pos = json.find(pattern);
        if (pos == std::string::npos) return def;

        pos = json.find(':', pos);
        if (pos == std::string::npos) return def;

        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ',')) pos++;

        char* end = nullptr;
        float val = (float)strtod(json.c_str() + pos, &end);
        if (end == json.c_str() + pos) return def;
        return val;
    }

    static VLMVision::GameState StringToGameState(const std::string& state) {
        if (state == "login") return VLMVision::GameState::LOGIN;
        if (state == "server_select") return VLMVision::GameState::SERVER_SELECT;
        if (state == "loading") return VLMVision::GameState::LOADING;
        if (state == "spawn") return VLMVision::GameState::SPAWN;
        if (state == "hunting") return VLMVision::GameState::HUNTING;
        if (state == "dialog") return VLMVision::GameState::DIALOG;
        if (state == "shop") return VLMVision::GameState::SHOP;
        if (state == "dead") return VLMVision::GameState::DEAD;
        if (state == "returning") return VLMVision::GameState::RETURNING;
        if (state == "town_supply") return VLMVision::GameState::TOWN_SUPPLY;
        if (state == "combat") return VLMVision::GameState::COMBAT;
        if (state == "waypoint") return VLMVision::GameState::WAYPOINT;
        return VLMVision::GameState::UNKNOWN;
    }
}

// ============================================================
// Init
// ============================================================
bool VLMVision::Init(const VLMConfig& config) {
    s_config = config;
    // 使用 Key Rotator，所以不需要 api_key
    s_inited = true;

    if (s_inited) {
        Log("VLM", "VLM Vision API initialized (using Key Rotator)");
        Logf("VLM", "Model: %s", config.model_name.c_str());
    } else {
        s_lastError = "Key Rotator init failed";
        Log("VLM", "VLM init failed");
    }
    return s_inited;
}

void VLMVision::SetConfig(const VLMConfig& config) {
    s_config = config;
}

VLMVision::VLMConfig VLMVision::GetConfig() {
    return s_config;
}

void VLMVision::Shutdown() {
    s_inited = false;
    Log("VLM", "VLM Vision API shutdown");
}

bool VLMVision::IsInited() {
    return s_inited;
}

// ============================================================
// State Classification
// ============================================================
VLMVision::VisionResult VLMVision::ClassifyState(const ImageData& image) {
    VisionResult result;

    if (!s_inited) {
        result.error = "VLM not initialized";
        return result;
    }

    if (image.pixels.empty()) {
        result.error = "Empty image data";
        return result;
    }

    DWORD startTime = GetTickCount();

    // Build multipart request with base64-encoded image
    // First, convert BGRA to JPEG-like base64 (simplified: use raw BGRA base64)
    std::string imageBase64;
    {
        const uint8_t* data = image.pixels.data();
        size_t size = image.pixels.size();
        // Simple base64 encoding
        static const char b64_chars[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string buf;
        int i = 0, j = 0;
        uint8_t char_array_3[3], char_array_4[4];
        while (size--) {
            char_array_3[i++] = *(data++);
            if (i == 3) {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;
                for (i = 0; i < 4; i++) buf += b64_chars[char_array_4[i]];
                i = 0;
            }
        }
        if (i) {
            for (j = i; j < 3; j++) char_array_3[j] = 0;
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            for (j = 0; j < i + 1; j++) buf += b64_chars[char_array_4[j]];
            while ((i++ < 3)) buf += '=';
        }
        imageBase64 = buf;
    }

    // Build JSON with vision message
    std::string json = "{";
    json += "\"model\":\"" + s_config.model_name + "\",";
    json += "\"messages\":[{";
    json += "\"role\":\"system\",\"content\":\"" + EscapeJSONString(s_config.system_prompt) + "\"";
    json += "},{";
    json += "\"role\":\"user\",\"content\":[{";
    json += "{\"type\":\"text\",\"text\":\"Analyze this game screenshot. The bot is STUCK and needs recovery. What is the blocking issue and what should it do?\"},";
    json += "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/png;base64," + imageBase64 + "\"}}";
    json += "}]}],\"temperature\":0.1,\"max_tokens\":256}";

    HTTPResponse resp;
    for (int retry = 0; retry <= s_config.max_retries; retry++) {
        if (retry > 0) {
            Sleep(s_config.retry_delay_ms);
            Logf("VLM", "Retry attempt %d", retry);
        }

        resp = PostJSON(s_config.api_endpoint.c_str(), json.c_str(),
                       s_config.connect_timeout_ms + s_config.read_timeout_ms);

        if (resp.status_code == 200) break;
    }

    int latency = (int)(GetTickCount() - startTime);

    if (resp.status_code != 200) {
        result.error = resp.error.empty() ? resp.body : resp.error;
        Logf("VLM", "API error: %s", result.error.c_str());
        return result;
    }

    std::string response_text = ExtractJSON(resp.body);

    // Parse new format: current_blocking_issue and suggested_action
    std::string issue = ParseJSONString(response_text, "current_blocking_issue");
    std::string action = ParseJSONString(response_text, "suggested_action");
    result.confidence = ParseJSONFloat(response_text, "confidence", 0.5f);

    // Also support legacy "state" field
    if (issue.empty()) {
        result.state = StringToGameState(ParseJSONString(response_text, "state"));
        if (result.state != VLMVision::GameState::UNKNOWN) {
            result.found = true;
        }
    } else {
        result.found = true;
        // Map issue to state for backward compatibility
        if (issue == "Dead") result.state = VLMVision::GameState::DEAD;
        else if (issue == "NpcDialog" || issue == "Popup") result.state = VLMVision::GameState::DIALOG;
        else if (issue == "ShopDialog") result.state = VLMVision::GameState::SHOP;
        else if (issue == "Loading") result.state = VLMVision::GameState::LOADING;
        else if (issue == "InventoryFull") result.state = VLMVision::GameState::TOWN_SUPPLY;
        else if (issue == "Terrain") result.state = VLMVision::GameState::HUNTING;
        else result.state = VLMVision::GameState::UNKNOWN;
    }

    Logf("VLM", "ClassifyState: issue=%s action=%s conf=%.2f latency=%dms",
         issue.c_str(), action.c_str(), result.confidence, latency);

    CallRecord& rec = s_history[s_historyIndex % 20];
    rec.timestamp_ms = GetTickCount();
    rec.image_size = (int)image.pixels.size();
    rec.result = result;
    rec.latency_ms = latency;
    s_historyIndex++;
    if (s_historyCount < 20) s_historyCount++;

    Logf("VLM", "ClassifyState: state=%d, conf=%.2f, latency=%dms",
         (int)result.state, result.confidence, latency);

    return result;
}

VLMVision::VisionResult VLMVision::ClassifyStateFromFile(const char* filepath) {
    VisionResult result;
    result.error = "Not implemented yet";
    return result;
}

VLMVision::VisionResult VLMVision::Query(const ImageData& image, const char* prompt) {
    VisionResult result;

    if (!s_inited) {
        result.error = "VLM not initialized";
        return result;
    }

    std::string json = R"({
  "model": ")";
    json += s_config.model_name;
    json += R"(
  "messages": [
    {"role": "user", "content": "}" + std::string(prompt) + R"("}
  ],
  "temperature": 0.1,
  "max_tokens": 256
})";

    HTTPResponse resp = PostJSON(s_config.api_endpoint.c_str(), json.c_str(),
                                s_config.connect_timeout_ms + s_config.read_timeout_ms);

    if (resp.status_code != 200) {
        result.error = resp.error.empty() ? resp.body : resp.error;
        return result;
    }

    std::string response_text = ExtractJSON(resp.body);
    result.state = StringToGameState(ParseJSONString(response_text, "state"));
    result.confidence = ParseJSONFloat(response_text, "confidence");
    result.found = (result.state != VLMVision::GameState::UNKNOWN);

    return result;
}

std::vector<VLMVision::VisionResult> VLMVision::BatchQuery(
    const std::vector<ImageData>& images, const char* prompt) {

    std::vector<VisionResult> results;
    for (const auto& img : images) {
        results.push_back(Query(img, prompt));
    }
    return results;
}

// ============================================================
// Yes/No Check
// ============================================================
VLMVision::YesNoResult VLMVision::CheckPresence(const ImageData& image, const char* question) {
    YesNoResult result;

    if (!s_inited) {
        return result;
    }

    std::string json = R"({
  "model": ")";
    json += s_config.model_name;
    json += R"(
  "messages": [
    {"role": "user", "content": "}" + std::string(question) + R"("}
  ],
  "temperature": 0.1,
  "max_tokens": 64
})";

    HTTPResponse resp = PostJSON(s_config.api_endpoint.c_str(), json.c_str(), 5000);

    if (resp.status_code == 200) {
        std::string answer = resp.body;
        std::transform(answer.begin(), answer.end(), answer.begin(), ::tolower);

        if (answer.find("yes") != std::string::npos ||
            answer.find("true") != std::string::npos ||
            answer.find("yes") != std::string::npos) {
            result.answer = true;
        }
    }

    return result;
}

// ============================================================
// Helpers
// ============================================================
const char* VLMVision::GetLastError() {
    return s_lastError.c_str();
}

void VLMVision::SetDebug(bool enable) {
    s_debug = enable;
}

bool VLMVision::TestConnection() {
    if (!s_inited) return false;

    std::string json = R"({
  "model": ")";
    json += s_config.model_name;
    json += R"(
  "messages": [{"role": "user", "content": "Hi"}],
  "max_tokens": 10
})";

    HTTPResponse resp = PostJSON(s_config.api_endpoint.c_str(), json.c_str(), 5000);

    return resp.status_code == 200;
}

const VLMVision::CallRecord* VLMVision::GetCallHistory(int index) {
    if (index < 0 || index >= s_historyCount) return nullptr;
    int actualIndex = (s_historyIndex - s_historyCount + index) % 20;
    return &s_history[actualIndex];
}

int VLMVision::GetCallHistoryCount() {
    return s_historyCount;
}
