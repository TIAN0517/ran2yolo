#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <string>
#include <vector>

struct OfflineLicenseInfo {
    bool success;
    bool valid;
    int days_left;
    std::string message;
    std::string license_id;
    std::string hwid;
    std::string expires_at;
    std::vector<std::string> permissions;
};

bool OfflineLicenseLooksLikeToken(const char* text);
bool OfflineLicensePublicKeyExists();
bool OfflineLicenseVerifyToken(const char* token, const char* hwid, OfflineLicenseInfo* outInfo);

bool OfflineLicenseGenerateKeyPair(const char* privateKeyPath, const char* publicKeyPath,
    int bits, char* err, size_t errSize);
bool OfflineLicenseIssueToken(const char* privateKeyPath, const char* hwid, int days,
    const char* permissionsCsv, char* outToken, size_t outTokenSize,
    char* err, size_t errSize);

// 獲取機器 HWID（一卡一機硬體綁定）
const char* GetMachineHWID();

// 載入本地緩存的卡密（啟動時自動驗證）
bool OfflineLicenseLoadCached(char* outToken, size_t outSize);

// 保存卡密到本地緩存（驗證成功後自動保存）
bool OfflineLicenseSaveCached(const char* token);
