#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

// __cpuid 需要 intirin.h
#include <intrin.h>

#include "offline_license.h"

#include <bcrypt.h>
#include <iphlpapi.h>
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "iphlpapi.lib")
#endif

static const char* kOfflineLicensePrefix = "JYLIC1|";
static const char* kDefaultPublicKeyFile = "license_public.blob";
static const char* kDefaultPrivateKeyFile = "license_private.blob";

static void SetErr(char* err, size_t errSize, const char* fmt, ...) {
    if (!err || errSize == 0) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(err, errSize, fmt, args);
    va_end(args);
    err[errSize - 1] = '\0';
}

static std::string StripWhitespace(const char* text) {
    std::string out;
    if (!text) return out;
    while (*text) {
        unsigned char ch = (unsigned char)(*text++);
        if (!isspace(ch)) out.push_back((char)ch);
    }
    return out;
}

bool OfflineLicenseLooksLikeToken(const char* text) {
    std::string compact = StripWhitespace(text);
    return compact.rfind(kOfflineLicensePrefix, 0) == 0;
}

static bool GetModuleDir(char* outDir, size_t outSize) {
    if (!outDir || outSize < MAX_PATH) return false;
    DWORD len = GetModuleFileNameA(NULL, outDir, (DWORD)outSize);
    if (len == 0 || len >= outSize) return false;
    for (DWORD i = len; i > 0; --i) {
        if (outDir[i - 1] == '\\' || outDir[i - 1] == '/') {
            outDir[i - 1] = '\0';
            return true;
        }
    }
    return false;
}

static bool GetDefaultKeyPath(const char* fileName, char* outPath, size_t outSize) {
    char dir[MAX_PATH] = {0};
    if (!GetModuleDir(dir, sizeof(dir))) return false;
    if (sprintf_s(outPath, outSize, "%s\\%s", dir, fileName) <= 0) return false;
    return true;
}

static bool FileExistsA2(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool OfflineLicensePublicKeyExists() {
    char path[MAX_PATH] = {0};
    return GetDefaultKeyPath(kDefaultPublicKeyFile, path, sizeof(path)) && FileExistsA2(path);
}

static bool ReadFileBytes(const char* path, std::vector<BYTE>* out, char* err, size_t errSize) {
    if (!path || !out) return false;
    FILE* f = NULL;
    if (fopen_s(&f, path, "rb") != 0 || !f) {
        SetErr(err, errSize, "無法開啟檔案: %s", path ? path : "(null)");
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        SetErr(err, errSize, "讀取檔案大小失敗");
        return false;
    }
    long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        SetErr(err, errSize, "檔案為空");
        return false;
    }
    rewind(f);
    out->resize((size_t)size);
    size_t read = fread(out->data(), 1, out->size(), f);
    fclose(f);
    if (read != out->size()) {
        SetErr(err, errSize, "檔案讀取不完整");
        return false;
    }
    return true;
}

static bool WriteFileBytes(const char* path, const BYTE* data, size_t size, char* err, size_t errSize) {
    if (!path || !data || size == 0) {
        SetErr(err, errSize, "寫檔參數無效");
        return false;
    }
    FILE* f = NULL;
    if (fopen_s(&f, path, "wb") != 0 || !f) {
        SetErr(err, errSize, "無法寫入檔案: %s", path);
        return false;
    }
    size_t wrote = fwrite(data, 1, size, f);
    fclose(f);
    if (wrote != size) {
        SetErr(err, errSize, "檔案寫入不完整");
        return false;
    }
    return true;
}

static const char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const BYTE* data, size_t size) {
    std::string out;
    if (!data || size == 0) return out;
    out.reserve(((size + 2) / 3) * 4);
    for (size_t i = 0; i < size; i += 3) {
        unsigned int octetA = data[i];
        unsigned int octetB = (i + 1 < size) ? data[i + 1] : 0;
        unsigned int octetC = (i + 2 < size) ? data[i + 2] : 0;
        unsigned int triple = (octetA << 16) | (octetB << 8) | octetC;
        out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
        out.push_back((i + 1 < size) ? kBase64Alphabet[(triple >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < size) ? kBase64Alphabet[triple & 0x3F] : '=');
    }
    return out;
}

static int Base64Value(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

static bool Base64Decode(const std::string& text, std::vector<BYTE>* out) {
    if (!out) return false;
    out->clear();
    if (text.empty() || (text.size() % 4) != 0) return false;
    out->reserve((text.size() / 4) * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        int v0 = Base64Value(text[i]);
        int v1 = Base64Value(text[i + 1]);
        int v2 = (text[i + 2] == '=') ? -2 : Base64Value(text[i + 2]);
        int v3 = (text[i + 3] == '=') ? -2 : Base64Value(text[i + 3]);
        if (v0 < 0 || v1 < 0 || v2 == -1 || v3 == -1) return false;

        unsigned int triple = ((unsigned int)v0 << 18) | ((unsigned int)v1 << 12) |
            ((unsigned int)((v2 < 0) ? 0 : v2) << 6) |
            (unsigned int)((v3 < 0) ? 0 : v3);

        out->push_back((BYTE)((triple >> 16) & 0xFF));
        if (v2 >= 0) out->push_back((BYTE)((triple >> 8) & 0xFF));
        if (v3 >= 0) out->push_back((BYTE)(triple & 0xFF));
    }
    return true;
}

static bool HashSha256(const BYTE* data, size_t dataSize, std::vector<BYTE>* outHash, char* err, size_t errSize) {
    if (!data || !outHash) return false;

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    DWORD objLen = 0;
    DWORD cbData = 0;
    DWORD hashLen = 0;
    std::vector<BYTE> obj;
    std::vector<BYTE> hash;
    NTSTATUS status;

    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (status < 0) {
        SetErr(err, errSize, "開啟 SHA256 provider 失敗: 0x%08X", (unsigned int)status);
        return false;
    }

    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &cbData, 0);
    if (status < 0) {
        SetErr(err, errSize, "取得 SHA256 object length 失敗: 0x%08X", (unsigned int)status);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }
    status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(hashLen), &cbData, 0);
    if (status < 0) {
        SetErr(err, errSize, "取得 SHA256 hash length 失敗: 0x%08X", (unsigned int)status);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    obj.resize(objLen);
    hash.resize(hashLen);
    status = BCryptCreateHash(hAlg, &hHash, obj.data(), objLen, NULL, 0, 0);
    if (status < 0) {
        SetErr(err, errSize, "建立 SHA256 hash 失敗: 0x%08X", (unsigned int)status);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    status = BCryptHashData(hHash, (PUCHAR)data, (ULONG)dataSize, 0);
    if (status >= 0) status = BCryptFinishHash(hHash, hash.data(), hashLen, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (status < 0) {
        SetErr(err, errSize, "SHA256 計算失敗: 0x%08X", (unsigned int)status);
        return false;
    }

    *outHash = hash;
    return true;
}

static bool ImportRsaKeyPair(const char* path, LPCWSTR blobType, BCRYPT_KEY_HANDLE* outKey,
    char* err, size_t errSize) {
    if (!outKey) return false;
    *outKey = NULL;

    std::vector<BYTE> blob;
    if (!ReadFileBytes(path, &blob, err, errSize)) return false;

    BCRYPT_ALG_HANDLE hAlg = NULL;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RSA_ALGORITHM, NULL, 0);
    if (status < 0) {
        SetErr(err, errSize, "開啟 RSA provider 失敗: 0x%08X", (unsigned int)status);
        return false;
    }

    status = BCryptImportKeyPair(hAlg, NULL, blobType, outKey, blob.data(), (ULONG)blob.size(), 0);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (status < 0) {
        SetErr(err, errSize, "匯入金鑰失敗: 0x%08X", (unsigned int)status);
        return false;
    }
    return true;
}

static std::string NormalizePermissionsCsv(const char* permissionsCsv) {
    std::string s = permissionsCsv ? permissionsCsv : "";
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char ch) {
        return ch == '|' || ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ';
    }), s.end());
    if (s.empty()) s = "basic";
    return s;
}

static std::vector<std::string> SplitCsv(const std::string& csv) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= csv.size()) {
        size_t pos = csv.find(',', start);
        std::string item = (pos == std::string::npos) ? csv.substr(start) : csv.substr(start, pos - start);
        if (!item.empty()) out.push_back(item);
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return out;
}

static bool SplitTokenPreserveEmpty(const std::string& token, std::vector<std::string>* parts) {
    if (!parts) return false;
    parts->clear();
    size_t start = 0;
    while (start <= token.size()) {
        size_t pos = token.find('|', start);
        parts->push_back((pos == std::string::npos) ? token.substr(start) : token.substr(start, pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return true;
}

static std::string FormatUtcTimeULongLong(ULONGLONG secUtc) {
    time_t t = (time_t)secUtc;
    struct tm tmUtc;
#ifdef _WIN32
    gmtime_s(&tmUtc, &t);
#else
    gmtime_r(&t, &tmUtc);
#endif
    char buf[32];
    sprintf_s(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d UTC",
        tmUtc.tm_year + 1900, tmUtc.tm_mon + 1, tmUtc.tm_mday,
        tmUtc.tm_hour, tmUtc.tm_min, tmUtc.tm_sec);
    return buf;
}

static std::string RandomTokenPart(size_t len) {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    BYTE randomBytes[64] = {0};
    if (len > sizeof(randomBytes)) len = sizeof(randomBytes);
    BCryptGenRandom(NULL, randomBytes, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(alphabet[randomBytes[i] % (sizeof(alphabet) - 1)]);
    }
    return out;
}

bool OfflineLicenseGenerateKeyPair(const char* privateKeyPath, const char* publicKeyPath,
    int bits, char* err, size_t errSize) {
    if (!privateKeyPath || !publicKeyPath) {
        SetErr(err, errSize, "金鑰路徑不可為空");
        return false;
    }
    if (bits < 1024) bits = 1024;

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    DWORD cbPriv = 0;
    DWORD cbPub = 0;
    std::vector<BYTE> privBlob;
    std::vector<BYTE> pubBlob;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RSA_ALGORITHM, NULL, 0);
    if (status < 0) {
        SetErr(err, errSize, "開啟 RSA provider 失敗: 0x%08X", (unsigned int)status);
        return false;
    }

    status = BCryptGenerateKeyPair(hAlg, &hKey, (ULONG)bits, 0);
    if (status >= 0) status = BCryptFinalizeKeyPair(hKey, 0);
    if (status < 0) {
        SetErr(err, errSize, "建立 RSA 金鑰對失敗: 0x%08X", (unsigned int)status);
        if (hKey) BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    status = BCryptExportKey(hKey, NULL, BCRYPT_RSAPRIVATE_BLOB, NULL, 0, &cbPriv, 0);
    if (status >= 0) status = BCryptExportKey(hKey, NULL, BCRYPT_RSAPUBLIC_BLOB, NULL, 0, &cbPub, 0);
    if (status < 0) {
        SetErr(err, errSize, "查詢金鑰 blob 大小失敗: 0x%08X", (unsigned int)status);
        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    privBlob.resize(cbPriv);
    pubBlob.resize(cbPub);
    status = BCryptExportKey(hKey, NULL, BCRYPT_RSAPRIVATE_BLOB, privBlob.data(), cbPriv, &cbPriv, 0);
    if (status >= 0) status = BCryptExportKey(hKey, NULL, BCRYPT_RSAPUBLIC_BLOB, pubBlob.data(), cbPub, &cbPub, 0);
    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (status < 0) {
        SetErr(err, errSize, "匯出金鑰失敗: 0x%08X", (unsigned int)status);
        return false;
    }

    if (!WriteFileBytes(privateKeyPath, privBlob.data(), privBlob.size(), err, errSize)) return false;
    if (!WriteFileBytes(publicKeyPath, pubBlob.data(), pubBlob.size(), err, errSize)) return false;
    return true;
}

bool OfflineLicenseIssueToken(const char* privateKeyPath, const char* hwid, int days,
    const char* permissionsCsv, char* outToken, size_t outTokenSize,
    char* err, size_t errSize) {
    if (!privateKeyPath || !hwid || !outToken || outTokenSize < 64) {
        SetErr(err, errSize, "發卡參數無效");
        return false;
    }
    if (strlen(hwid) < 32) {
        SetErr(err, errSize, "HWID 長度不正確");
        return false;
    }
    if (days < 1) days = 30;

    BCRYPT_KEY_HANDLE hKey = NULL;
    if (!ImportRsaKeyPair(privateKeyPath, BCRYPT_RSAPRIVATE_BLOB, &hKey, err, errSize)) return false;

    ULONGLONG issued = (ULONGLONG)time(NULL);
    ULONGLONG expires = issued + ((ULONGLONG)days * 24ULL * 60ULL * 60ULL);
    std::string licenseId = RandomTokenPart(16);
    std::string nonce = RandomTokenPart(12);
    std::string permCsv = NormalizePermissionsCsv(permissionsCsv);

    char payload[512];
    sprintf_s(payload, sizeof(payload), "JYLIC1|%s|%s|%llu|%llu|%s|%s",
        licenseId.c_str(), hwid, issued, expires, permCsv.c_str(), nonce.c_str());

    std::vector<BYTE> hash;
    if (!HashSha256((const BYTE*)payload, strlen(payload), &hash, err, errSize)) {
        BCryptDestroyKey(hKey);
        return false;
    }

    BCRYPT_PKCS1_PADDING_INFO paddingInfo = { BCRYPT_SHA256_ALGORITHM };
    ULONG sigLen = 0;
    NTSTATUS status = BCryptSignHash(hKey, &paddingInfo, hash.data(), (ULONG)hash.size(),
        NULL, 0, &sigLen, BCRYPT_PAD_PKCS1);
    if (status < 0) {
        SetErr(err, errSize, "簽章大小計算失敗: 0x%08X", (unsigned int)status);
        BCryptDestroyKey(hKey);
        return false;
    }

    std::vector<BYTE> sig(sigLen);
    status = BCryptSignHash(hKey, &paddingInfo, hash.data(), (ULONG)hash.size(),
        sig.data(), sigLen, &sigLen, BCRYPT_PAD_PKCS1);
    BCryptDestroyKey(hKey);
    if (status < 0) {
        SetErr(err, errSize, "簽章失敗: 0x%08X", (unsigned int)status);
        return false;
    }

    std::string sigB64 = Base64Encode(sig.data(), sigLen);
    std::string token = std::string(payload) + "|" + sigB64;
    if (token.size() + 1 > outTokenSize) {
        SetErr(err, errSize, "輸出緩衝不足，需要至少 %u bytes", (unsigned int)(token.size() + 1));
        return false;
    }
    strcpy_s(outToken, outTokenSize, token.c_str());
    return true;
}

bool OfflineLicenseVerifyToken(const char* token, const char* hwid, OfflineLicenseInfo* outInfo) {
    if (outInfo) {
        outInfo->success = false;
        outInfo->valid = false;
        outInfo->days_left = -1;
        outInfo->message.clear();
        outInfo->license_id.clear();
        outInfo->hwid.clear();
        outInfo->expires_at.clear();
        outInfo->permissions.clear();
    }

    std::string compact = StripWhitespace(token);
    if (compact.rfind(kOfflineLicensePrefix, 0) != 0) {
        if (outInfo) outInfo->message = "授權格式錯誤";
        return false;
    }

    std::vector<std::string> parts;
    SplitTokenPreserveEmpty(compact, &parts);
    if (parts.size() != 8) {
        if (outInfo) outInfo->message = "授權欄位數量錯誤";
        return false;
    }

    std::string payload = parts[0];
    for (size_t i = 1; i < 7; ++i) {
        payload += "|";
        payload += parts[i];
    }

    std::vector<BYTE> sig;
    if (!Base64Decode(parts[7], &sig) || sig.empty()) {
        if (outInfo) outInfo->message = "授權簽章格式錯誤";
        return false;
    }

    char pubPath[MAX_PATH] = {0};
    if (!GetDefaultKeyPath(kDefaultPublicKeyFile, pubPath, sizeof(pubPath)) || !FileExistsA2(pubPath)) {
        if (outInfo) outInfo->message = "缺少 license_public.blob 公鑰檔";
        return false;
    }

    char err[256] = {0};
    BCRYPT_KEY_HANDLE hKey = NULL;
    if (!ImportRsaKeyPair(pubPath, BCRYPT_RSAPUBLIC_BLOB, &hKey, err, sizeof(err))) {
        if (outInfo) outInfo->message = err;
        return false;
    }

    std::vector<BYTE> hash;
    if (!HashSha256((const BYTE*)payload.data(), payload.size(), &hash, err, sizeof(err))) {
        BCryptDestroyKey(hKey);
        if (outInfo) outInfo->message = err;
        return false;
    }

    BCRYPT_PKCS1_PADDING_INFO paddingInfo = { BCRYPT_SHA256_ALGORITHM };
    NTSTATUS status = BCryptVerifySignature(hKey, &paddingInfo, hash.data(), (ULONG)hash.size(),
        sig.data(), (ULONG)sig.size(), BCRYPT_PAD_PKCS1);
    BCryptDestroyKey(hKey);
    if (status < 0) {
        if (outInfo) outInfo->message = "授權簽章驗證失敗";
        return false;
    }

    ULONGLONG issued = _strtoui64(parts[3].c_str(), NULL, 10);
    ULONGLONG expires = _strtoui64(parts[4].c_str(), NULL, 10);
    ULONGLONG now = (ULONGLONG)time(NULL);

    if (hwid && hwid[0] && _stricmp(parts[2].c_str(), hwid) != 0) {
        if (outInfo) {
            outInfo->message = "HWID 不匹配";
            outInfo->hwid = parts[2];
            outInfo->license_id = parts[1];
        }
        return false;
    }

    if (expires <= issued || expires <= now) {
        if (outInfo) {
            outInfo->message = "授權已過期";
            outInfo->hwid = parts[2];
            outInfo->license_id = parts[1];
            outInfo->expires_at = FormatUtcTimeULongLong(expires);
            outInfo->days_left = 0;
        }
        return false;
    }

    if (outInfo) {
        outInfo->success = true;
        outInfo->valid = true;
        outInfo->license_id = parts[1];
        outInfo->hwid = parts[2];
        outInfo->expires_at = FormatUtcTimeULongLong(expires);
        outInfo->permissions = SplitCsv(parts[5]);
        outInfo->message = "離線授權驗證成功";
        ULONGLONG remainSec = expires - now;
        outInfo->days_left = (int)((remainSec + 86399ULL) / 86400ULL);
        if (outInfo->days_left < 0) outInfo->days_left = 0;
    }
    return true;
}

// ============================================================
// 機器 HWID 獲取（一卡一機）
// ============================================================
static char s_cachedHwid[128] = {0};

static bool GetCpuId(char* out, size_t size) {
    if (!out || size < 32) return false;
    int info[4] = {0};
    __cpuid(info, 1);
    sprintf_s(out, size, "%08X-%08X", info[0], info[3]);
    return true;
}

static bool GetMacAddress(char* out, size_t size) {
    if (!out || size < 32) return false;

    ULONG bufLen = 15000;
    PIP_ADAPTER_INFO pAdapterInfo = (PIP_ADAPTER_INFO)malloc(bufLen);
    if (!pAdapterInfo) {
        strcpy_s(out, size, "UNKNOWN");
        return false;
    }

    DWORD dwStatus = GetAdaptersInfo(pAdapterInfo, &bufLen);
    if (dwStatus == ERROR_SUCCESS) {
        PIP_ADAPTER_INFO pAdapter = pAdapterInfo;
        while (pAdapter) {
            if (pAdapter->Type == MIB_IF_TYPE_ETHERNET && pAdapter->AddressLength == 6) {
                sprintf_s(out, size, "%02X%02X%02X%02X%02X%02X",
                    (int)pAdapter->Address[0], (int)pAdapter->Address[1],
                    (int)pAdapter->Address[2], (int)pAdapter->Address[3],
                    (int)pAdapter->Address[4], (int)pAdapter->Address[5]);
                free(pAdapterInfo);
                return true;
            }
            pAdapter = pAdapter->Next;
        }
    }

    free(pAdapterInfo);
    strcpy_s(out, size, "UNKNOWN");
    return false;
}

static bool GetMachineIdHash(char* out, size_t size) {
    if (!out || size < 64) return false;

    char cpuId[32] = {0};
    char macAddr[32] = {0};

    GetCpuId(cpuId, sizeof(cpuId));
    GetMacAddress(macAddr, sizeof(macAddr));

    // 拼接硬體資訊
    char combined[128] = {0};
    sprintf_s(combined, sizeof(combined), "%s|%s|JyTrainer_v1", cpuId, macAddr);

    // SHA256 計算
    std::vector<BYTE> hash;
    if (!HashSha256((const BYTE*)combined, strlen(combined), &hash, NULL, 0)) {
        strcpy_s(out, size, "ERROR");
        return false;
    }

    // 轉為 16 進制字串
    char* p = out;
    for (size_t i = 0; i < hash.size() && (p - out + 3) < (int)size; i++) {
        sprintf_s(p, 4, "%02x", hash[i]);
        p += 2;
    }

    return true;
}

const char* GetMachineHWID() {
    if (s_cachedHwid[0] == '\0') {
        GetMachineIdHash(s_cachedHwid, sizeof(s_cachedHwid));
    }
    return s_cachedHwid;
}

// 載入本地緩存的卡密（從 license_token.dat）
static const char* kCachedTokenFile = "license_token.dat";

bool OfflineLicenseLoadCached(char* outToken, size_t outSize) {
    if (!outToken || outSize < 64) return false;
    outToken[0] = '\0';

    char path[MAX_PATH] = {0};
    if (!GetModuleDir(path, sizeof(path))) return false;
    if (sprintf_s(path + strlen(path), sizeof(path) - strlen(path), "\\%s", kCachedTokenFile) <= 0) return false;

    FILE* f = NULL;
    if (fopen_s(&f, path, "r") != 0 || !f) return false;

    char buf[8192] = {0};
    size_t pos = 0;
    int ch;
    while ((ch = fgetc(f)) != EOF && pos + 1 < sizeof(buf)) {
        if (ch == '\r' || ch == '\n') break;
        buf[pos++] = (char)ch;
    }
    fclose(f);

    if (pos == 0) return false;

    // 去除空白
    char* end = buf + pos;
    while (end > buf && (end[-1] == ' ' || end[-1] == '\t')) --end;
    *end = '\0';

    // 去除開頭空白
    char* start = buf;
    while (*start == ' ' || *start == '\t') ++start;

    if (!OfflineLicenseLooksLikeToken(start)) return false;

    strncpy_s(outToken, outSize, start, _TRUNCATE);
    return true;
}

// 保存卡密到本地緩存
bool OfflineLicenseSaveCached(const char* token) {
    if (!token || !OfflineLicenseLooksLikeToken(token)) return false;

    char path[MAX_PATH] = {0};
    if (!GetModuleDir(path, sizeof(path))) return false;
    if (sprintf_s(path + strlen(path), sizeof(path) - strlen(path), "\\%s", kCachedTokenFile) <= 0) return false;

    FILE* f = NULL;
    if (fopen_s(&f, path, "w") != 0 || !f) return false;
    fprintf(f, "%s\n", token);
    fclose(f);
    return true;
}
