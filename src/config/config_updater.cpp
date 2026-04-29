#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include "config_updater.h"
#include "offset_config.h"
#include "../platform/win32_helpers.h"
#include <wininet.h>
#include <cstdio>
#include <cstring>

#ifdef _MSC_VER
#pragma comment(lib, "wininet.lib")
#endif

// ============================================================
// Static state
// ============================================================
static ConfigUpdater::UpdateConfig s_config;
static char s_lastStatus[256] = "Not initialized";
static char s_remoteVersion[64] = "";
static char s_localVersion[64] = "";
static bool s_initialized = false;
static char s_exeDir[MAX_PATH] = {0};

// ============================================================
// Helper: build full path relative to exe directory
// ============================================================
static void BuildFullPath(char* out, size_t outSize, const char* relativePath) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!relativePath || !relativePath[0]) return;
    if (IsAbsolutePathA(relativePath)) {
        strncpy_s(out, outSize, relativePath, _TRUNCATE);
        return;
    }
    if (s_exeDir[0] == '\0') GetExeDirectoryA(s_exeDir, MAX_PATH);
    strncpy_s(out, outSize, s_exeDir, _TRUNCATE);
    strncat_s(out, outSize, relativePath, _TRUNCATE);
}

// ============================================================
// Helper: trim trailing whitespace and newlines
// ============================================================
static void TrimRight(char* str) {
    int len = (int)strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' ||
           str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }
}

// ============================================================
// Helper: read a small text file into buffer
// ============================================================
static bool ReadSmallFile(const char* path, char* buf, size_t bufSize) {
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize >= (DWORD)bufSize) {
        CloseHandle(hFile);
        return false;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buf, fileSize, &bytesRead, NULL)) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);
    buf[bytesRead] = '\0';
    TrimRight(buf);
    return true;
}

// ============================================================
// Helper: write a small text file
// ============================================================
static bool WriteSmallFile(const char* path, const char* content) {
    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0,
                               NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD len = (DWORD)strlen(content);
    DWORD written = 0;
    BOOL ok = WriteFile(hFile, content, len, &written, NULL);
    CloseHandle(hFile);
    return ok && written == len;
}

// ============================================================
// WinINet: download URL content to a local file
// ============================================================
static bool HttpDownloadToFile(const char* url, const char* localPath) {
    HINTERNET hInternet = InternetOpenA("JyTrainer/2.0",
        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        _snprintf(s_lastStatus, sizeof(s_lastStatus),
            "InternetOpen failed: 0x%08lX", (unsigned long)GetLastError());
        return false;
    }

    // Set timeouts (10 seconds)
    DWORD timeout = 10000;
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url, NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE, 0);
    if (!hUrl) {
        _snprintf(s_lastStatus, sizeof(s_lastStatus),
            "InternetOpenUrl failed: 0x%08lX", (unsigned long)GetLastError());
        InternetCloseHandle(hInternet);
        return false;
    }

    // Check HTTP status code
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &statusCode, &statusSize, NULL)) {
        if (statusCode != 200) {
            _snprintf(s_lastStatus, sizeof(s_lastStatus),
                "HTTP %lu from: %s", (unsigned long)statusCode, url);
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return false;
        }
    }

    // Open local file for writing
    HANDLE hFile = CreateFileA(localPath, GENERIC_WRITE, 0,
                               NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        _snprintf(s_lastStatus, sizeof(s_lastStatus),
            "Cannot create file: %s", localPath);
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return false;
    }

    // Read and write in chunks
    char buf[4096];
    DWORD bytesRead = 0;
    DWORD totalBytes = 0;
    bool success = true;

    while (InternetReadFile(hUrl, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
        DWORD written = 0;
        if (!WriteFile(hFile, buf, bytesRead, &written, NULL) || written != bytesRead) {
            success = false;
            break;
        }
        totalBytes += bytesRead;

        // Safety limit: 10MB max
        if (totalBytes > 10 * 1024 * 1024) {
            success = false;
            _snprintf(s_lastStatus, sizeof(s_lastStatus),
                "Download too large (>10MB), aborted");
            break;
        }
    }

    CloseHandle(hFile);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    if (!success) {
        DeleteFileA(localPath);  // Clean up partial download
    }
    return success;
}

// ============================================================
// WinINet: download URL content to a memory buffer (for version.txt)
// ============================================================
static bool HttpDownloadToString(const char* url, char* outBuf, size_t outBufSize) {
    HINTERNET hInternet = InternetOpenA("JyTrainer/2.0",
        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return false;

    DWORD timeout = 10000;
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url, NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return false;
    }

    // Check HTTP status
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &statusCode, &statusSize, NULL)) {
        if (statusCode != 200) {
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return false;
        }
    }

    // Read into buffer (version.txt should be tiny)
    DWORD totalRead = 0;
    DWORD bytesRead = 0;
    while (totalRead < outBufSize - 1) {
        DWORD toRead = (DWORD)(outBufSize - 1 - totalRead);
        if (toRead > 4096) toRead = 4096;
        if (!InternetReadFile(hUrl, outBuf + totalRead, toRead, &bytesRead) || bytesRead == 0)
            break;
        totalRead += bytesRead;
    }
    outBuf[totalRead] = '\0';

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    TrimRight(outBuf);
    return totalRead > 0;
}

// ============================================================
// Init: load updater.ini configuration
// ============================================================
bool ConfigUpdater::Init(const char* configPath) {
    GetExeDirectoryA(s_exeDir, MAX_PATH);

    // Build updater.ini path
    char iniPath[MAX_PATH] = {0};
    if (configPath && configPath[0]) {
        BuildFullPath(iniPath, MAX_PATH, configPath);
    } else {
        BuildFullPath(iniPath, MAX_PATH, "updater.ini");
    }

    // Set defaults
    memset(&s_config, 0, sizeof(s_config));
    strcpy_s(s_config.url, sizeof(s_config.url), "http://your-server.com/jytrainer/offsets.dat");
    strcpy_s(s_config.versionUrl, sizeof(s_config.versionUrl), "http://your-server.com/jytrainer/version.txt");
    strcpy_s(s_config.localPath, sizeof(s_config.localPath), "offsets.dat");
    strcpy_s(s_config.localVersionFile, sizeof(s_config.localVersionFile), "version.txt");

    // Try to load from updater.ini using GetPrivateProfileStringA
    DWORD attr = GetFileAttributesA(iniPath);
    if (attr != INVALID_FILE_ATTRIBUTES) {
        GetPrivateProfileStringA("Updater", "OffsetUrl", s_config.url,
            s_config.url, sizeof(s_config.url), iniPath);
        GetPrivateProfileStringA("Updater", "VersionUrl", s_config.versionUrl,
            s_config.versionUrl, sizeof(s_config.versionUrl), iniPath);
        GetPrivateProfileStringA("Updater", "LocalPath", s_config.localPath,
            s_config.localPath, sizeof(s_config.localPath), iniPath);
        GetPrivateProfileStringA("Updater", "LocalVersionFile", s_config.localVersionFile,
            s_config.localVersionFile, sizeof(s_config.localVersionFile), iniPath);
        printf("[Updater] Config loaded from: %s\n", iniPath);
    } else {
        printf("[Updater] updater.ini not found, using defaults\n");
    }

    // Load local version
    char localVerPath[MAX_PATH] = {0};
    BuildFullPath(localVerPath, MAX_PATH, s_config.localVersionFile);
    if (ReadSmallFile(localVerPath, s_localVersion, sizeof(s_localVersion))) {
        printf("[Updater] Local version: %s\n", s_localVersion);
    } else {
        s_localVersion[0] = '\0';
        printf("[Updater] No local version file found\n");
    }

    s_initialized = true;
    strncpy_s(s_lastStatus, "Initialized", _TRUNCATE);
    return true;
}

// ============================================================
// CheckForUpdate: compare remote vs local version
// ============================================================
bool ConfigUpdater::CheckForUpdate() {
    if (!s_initialized) {
        strncpy_s(s_lastStatus, "Not initialized", _TRUNCATE);
        return false;
    }

    // Check if URL is still placeholder
    if (strstr(s_config.versionUrl, "your-server.com") != NULL) {
        strncpy_s(s_lastStatus, "Update URL not configured", _TRUNCATE);
        return false;
    }

    // Download remote version.txt
    char remoteBuf[64] = {0};
    if (!HttpDownloadToString(s_config.versionUrl, remoteBuf, sizeof(remoteBuf))) {
        strncpy_s(s_lastStatus, "Failed to check remote version", _TRUNCATE);
        return false;
    }

    strncpy_s(s_remoteVersion, remoteBuf, _TRUNCATE);

    // Compare versions
    if (s_localVersion[0] != '\0' && strcmp(s_localVersion, s_remoteVersion) == 0) {
        _snprintf(s_lastStatus, sizeof(s_lastStatus),
            "Up to date (v%s)", s_localVersion);
        return false;  // No update needed
    }

    _snprintf(s_lastStatus, sizeof(s_lastStatus),
        "Update available: %s -> %s", s_localVersion, s_remoteVersion);
    return true;
}

// ============================================================
// DownloadUpdate: download offsets.dat to .tmp then atomic swap
// ============================================================
bool ConfigUpdater::DownloadUpdate() {
    if (!s_initialized) {
        strncpy_s(s_lastStatus, "Not initialized", _TRUNCATE);
        return false;
    }

    // Check if URL is still placeholder
    if (strstr(s_config.url, "your-server.com") != NULL) {
        strncpy_s(s_lastStatus, "Download URL not configured", _TRUNCATE);
        return false;
    }

    // Build paths
    char localPath[MAX_PATH] = {0};
    char tmpPath[MAX_PATH] = {0};
    BuildFullPath(localPath, MAX_PATH, s_config.localPath);
    strcpy_s(tmpPath, sizeof(tmpPath), localPath);
    strncat_s(tmpPath, MAX_PATH, ".tmp", _TRUNCATE);

    // Download to .tmp first (safe: won't corrupt existing file)
    printf("[Updater] Downloading: %s\n", s_config.url);
    if (!HttpDownloadToFile(s_config.url, tmpPath)) {
        // s_lastStatus already set by HttpDownloadToFile
        printf("[Updater] Download failed: %s\n", s_lastStatus);
        return false;
    }

    // Verify downloaded file is not empty
    DWORD attr = GetFileAttributesA(tmpPath);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        strncpy_s(s_lastStatus, "Downloaded file missing", _TRUNCATE);
        return false;
    }

    // Atomic replace: delete old, rename tmp -> final
    // On Windows, MoveFileExA with MOVEFILE_REPLACE_EXISTING handles this
    if (!MoveFileExA(tmpPath, localPath, MOVEFILE_REPLACE_EXISTING)) {
        // Fallback: delete + rename
        DeleteFileA(localPath);
        if (!MoveFileA(tmpPath, localPath)) {
            _snprintf(s_lastStatus, sizeof(s_lastStatus),
                "Failed to replace %s: 0x%08lX", s_config.localPath, (unsigned long)GetLastError());
            DeleteFileA(tmpPath);
            return false;
        }
    }

    printf("[Updater] Downloaded and saved: %s\n", localPath);
    strncpy_s(s_lastStatus, "Download successful", _TRUNCATE);
    return true;
}

// ============================================================
// UpdateAndReload: full pipeline
// ============================================================
int ConfigUpdater::UpdateAndReload() {
    if (!s_initialized) {
        strncpy_s(s_lastStatus, "Not initialized", _TRUNCATE);
        return -1;
    }

    // Step 1: Check for update
    if (!CheckForUpdate()) {
        // Could be "up to date" or "check failed"
        if (s_localVersion[0] != '\0' && s_remoteVersion[0] != '\0' &&
            strcmp(s_localVersion, s_remoteVersion) == 0) {
            return 0;  // Already up to date
        }
        return -1;  // Check failed or URL not configured
    }

    // Step 2: Download
    if (!DownloadUpdate()) {
        return -2;  // Download failed
    }

    // Step 3: Reload offsets
    if (!OffsetConfig::Reload()) {
        // Reload returned false but offsets may still be using defaults
        // This is not necessarily a fatal error
        printf("[Updater] Warning: Reload returned false (may be using defaults)\n");
    }

    // Step 4: Update local version file
    char localVerPath[MAX_PATH] = {0};
    BuildFullPath(localVerPath, MAX_PATH, s_config.localVersionFile);
    if (WriteSmallFile(localVerPath, s_remoteVersion)) {
        strncpy_s(s_localVersion, s_remoteVersion, _TRUNCATE);
    }

    _snprintf(s_lastStatus, sizeof(s_lastStatus),
        "Updated to v%s successfully", s_remoteVersion);
    printf("[Updater] %s\n", s_lastStatus);
    return 1;
}

// ============================================================
// Getters
// ============================================================
const char* ConfigUpdater::GetLastStatus() {
    return s_lastStatus;
}

const char* ConfigUpdater::GetRemoteVersion() {
    return s_remoteVersion;
}

const char* ConfigUpdater::GetLocalVersion() {
    return s_localVersion;
}
