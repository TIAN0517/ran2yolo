// ============================================================
// pattern_scanner.cpp
// AOB Pattern Scanner Implementation
// 動態特徵碼掃描，取代舊的 legacy/fallback 靜態偏移
// ============================================================
#include "pattern_scanner.h"
#include "../game/memory_reader.h"
#include <TlHelp32.h>
#include <Psapi.h>
#include <algorithm>
#include <cctype>
#include <cstring>

// ============================================================
// 內部狀態
// ============================================================
namespace {
    HANDLE s_hProcess = NULL;
    DWORD s_moduleBase = 0;
    DWORD s_moduleSize = 0;
    bool s_inited = false;
    DWORD s_lastScanTime = 0;
    DWORD s_scanCount = 0;

    inline DWORD GetNowMs() { return GetTickCount(); }

    void LogScan(const char* fmt, ...) {
        static DWORD s_lastLog = 0;
        DWORD now = GetNowMs();
        if (now - s_lastLog < 2000) return;
        s_lastLog = now;

        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        printf("[PatternScan] %s\n", buf);
    }
}

// ============================================================
// 錯誤描述映射
// ============================================================
namespace PatternScanner {

const char* GetLastErrorDesc(DWORD errorCode) {
    switch (errorCode) {
        case ScanErrors::NOT_INITIALIZED:  return "Scanner not initialized";
        case ScanErrors::PROCESS_INVALID:  return "Process handle invalid";
        case ScanErrors::MODULE_NOT_FOUND: return "Module not found";
        case ScanErrors::PATTERN_INVALID:  return "Pattern string invalid";
        case ScanErrors::PATTERN_NOT_FOUND: return "Pattern not found in memory";
        case ScanErrors::POINTER_INVALID:  return "Pointer dereference failed";
        case ScanErrors::MEMORY_READ_FAIL: return "Memory read failed";
        case ScanErrors::SCAN_TIMEOUT:     return "Scan timeout";
        case ScanErrors::INVALID_PARAM:    return "Invalid parameter";
        default: return "Unknown error";
    }
}

// ============================================================
// Pattern 解析
// ============================================================
Pattern ParsePattern(const char* patternStr) {
    Pattern pattern;

    if (!patternStr || !*patternStr) {
        pattern.errorCode = ScanErrors::PATTERN_INVALID;
        pattern.name = "Empty pattern string";
        return pattern;
    }

    pattern.name = patternStr;

    const char* p = patternStr;
    while (*p) {
        // 跳過空白
        while (*p && isspace(*p)) p++;
        if (!*p) break;

        if (*p == '?') {
            // 萬用字元
            if (*(p + 1) == '?') {
                pattern.bytes.push_back(0);
                pattern.mask.push_back(false);
                p += 2;
            } else {
                pattern.bytes.push_back(0);
                pattern.mask.push_back(false);
                p++;
            }
        } else if (isxdigit(*p)) {
            // 讀取兩個十六進制位元組
            char byteStr[3] = {p[0], p[1], 0};
            uint8_t byte = (uint8_t)strtoul(byteStr, NULL, 16);
            pattern.bytes.push_back(byte);
            pattern.mask.push_back(true);
            p += 2;
        } else {
            p++;
        }
    }

    if (!pattern.IsValid()) {
        pattern.errorCode = ScanErrors::PATTERN_INVALID;
        pattern.name = "Pattern parse failed";
    }

    return pattern;
}

// ============================================================
// 記憶體操作（安全版）
// ============================================================

// 檢查記憶體分頁是否可讀取
static bool IsMemoryReadable(DWORD addr) {
    if (!s_hProcess || s_hProcess == INVALID_HANDLE_VALUE) return false;

    MEMORY_BASIC_INFORMATION mbi = {0};
    if (VirtualQueryEx(s_hProcess, (LPCVOID)addr, &mbi, sizeof(mbi)) == 0) {
        return false;
    }

    // 檢查分頁狀態：不在 committed 狀態 → 不可讀
    if ((mbi.State & MEM_COMMIT) == 0) {
        return false;
    }

    // 檢查保護屬性：包含可讀權限
    if ((mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                        PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
        return false;
    }

    return true;
}

static bool ReadMemory(DWORD addr, void* buffer, SIZE_T size) {
    if (!s_hProcess || s_hProcess == INVALID_HANDLE_VALUE) return false;

    // 前置檢查：確認記憶體分頁可讀（避免觸發 Guard Page 例外）
    if (!IsMemoryReadable(addr)) {
        return false;
    }

    SIZE_T bytesRead = 0;
    __try {
        return ReadProcessMemory(s_hProcess, (LPCVOID)addr, buffer, size, &bytesRead)
               && bytesRead == size;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 安全讀取：帶邊界檢查，自動處理跨分頁情況
static bool SafeReadMemory(DWORD addr, void* buffer, SIZE_T size) {
    if (!s_hProcess || s_hProcess == INVALID_HANDLE_VALUE) return false;
    if (addr == 0 || buffer == nullptr || size == 0) return false;

    MEMORY_BASIC_INFORMATION mbi = {0};
    if (VirtualQueryEx(s_hProcess, (LPCVOID)addr, &mbi, sizeof(mbi)) == 0) {
        return false;
    }

    // 計算從當前位置到分頁結尾的剩餘空間
    DWORD pageEnd = (DWORD)mbi.BaseAddress + (DWORD)mbi.RegionSize;
    SIZE_T safeSize = std::min<SIZE_T>(size, pageEnd - addr);

    SIZE_T bytesRead = 0;
    __try {
        BOOL ok = ReadProcessMemory(s_hProcess, (LPCVOID)addr, buffer, safeSize, &bytesRead);
        if (!ok) return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    return bytesRead == safeSize;
}

// ============================================================
// 核心掃描實現（安全分頁版）
// ============================================================
ScanResult FindPattern(const Pattern& pattern, DWORD startAddr, DWORD endAddr) {
    ScanResult result = {};

    if (!s_inited || !s_hProcess) {
        result.errorCode = ScanErrors::NOT_INITIALIZED;
        result.description = "Scanner not initialized";
        return result;
    }

    if (!pattern.IsValid()) {
        result.errorCode = ScanErrors::PATTERN_INVALID;
        result.description = pattern.name.empty() ? "Invalid pattern" : pattern.name;
        return result;
    }

    if (startAddr >= endAddr || startAddr < 0x1000) {
        result.errorCode = ScanErrors::INVALID_PARAM;
        result.description = "Invalid scan range";
        return result;
    }

    const SIZE_T scanChunkSize = 4096;  // 4KB 分頁大小
    std::vector<uint8_t> scanBuffer(scanChunkSize);

    DWORD scanStart = GetNowMs();
    const DWORD scanTimeout = 5000;  // 5 秒超時

    // 使用 VirtualQueryEx 迭代記憶體分頁
    MEMORY_BASIC_INFORMATION mbi = {0};
    DWORD currentAddr = startAddr;

    while (currentAddr < endAddr) {
        // 超時檢查
        if (GetNowMs() - scanStart > scanTimeout) {
            result.errorCode = ScanErrors::SCAN_TIMEOUT;
            result.description = "Scan timeout - pattern not found";
            LogScan("WARNING: Scan timeout for '%s'", pattern.name.c_str());
            return result;
        }

        // 查詢當前分頁資訊
        if (VirtualQueryEx(s_hProcess, (LPCVOID)currentAddr, &mbi, sizeof(mbi)) == 0) {
            // 無法查詢，跳到下一個可能的分頁起始點
            currentAddr += scanChunkSize;
            continue;
        }

        DWORD regionBase = (DWORD)mbi.BaseAddress;
        DWORD regionEnd = regionBase + (DWORD)mbi.RegionSize;
        DWORD regionScanStart = std::max<DWORD>(currentAddr, regionBase);
        DWORD regionScanEnd = std::min<DWORD>(endAddr, regionEnd);

        // 檢查分頁是否可讀取
        bool pageReadable =
            (mbi.State & MEM_COMMIT) &&
            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                             PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));

        if (pageReadable) {
            // 在此分頁內掃描
            for (DWORD addr = regionScanStart; addr <= regionScanEnd - pattern.bytes.size(); addr += 64) {
                DWORD remaining = regionScanEnd - addr;
                SIZE_T toRead = std::min<SIZE_T>(remaining, scanChunkSize);

                // 使用 SafeReadMemory 避免跨分頁讀取導致 Access Violation
                // SafeReadMemory 會計算分頁邊界，確保只讀取可訪問的記憶體範圍
                if (!SafeReadMemory(addr, scanBuffer.data(), toRead)) {
                    continue;
                }

                for (DWORD offset = 0; offset <= (DWORD)toRead - (DWORD)pattern.bytes.size(); offset++) {
                    bool match = true;
                    for (SIZE_T i = 0; i < pattern.bytes.size(); i++) {
                        if (pattern.mask[i]) {
                            if (scanBuffer[offset + i] != pattern.bytes[i]) {
                                match = false;
                                break;
                            }
                        }
                    }

                    if (match) {
                        result.found = true;
                        result.address = addr + offset;
                        result.confidence = 1.0f;
                        result.description = "Pattern matched: " + pattern.name;
                        result.errorCode = 0;
                        s_scanCount++;
                        s_lastScanTime = GetNowMs();
                        LogScan("Found '%s' at 0x%08X", pattern.name.c_str(), result.address);
                        return result;
                    }
                }
            }
        }

        // 移動到下一個分頁
        currentAddr = regionEnd;
    }

    result.errorCode = ScanErrors::PATTERN_NOT_FOUND;
    result.description = "Pattern not found: " + pattern.name;
    return result;
}

ScanResult FindPatternWithOffset(const Pattern& pattern, int pointerOffset,
                                  int derefCount, DWORD startAddr, DWORD endAddr) {
    ScanResult result = FindPattern(pattern, startAddr, endAddr);

    if (!result.found) return result;

    DWORD addr = result.address + pointerOffset;
    DWORD ptr = 0;

    for (int i = 0; i < derefCount; i++) {
        if (!ReadMemory(addr, &ptr, sizeof(ptr))) {
            result.found = false;
            result.errorCode = ScanErrors::MEMORY_READ_FAIL;
            result.description = "Failed to dereference pointer at 0x" + std::to_string(addr);
            return result;
        }

        if (ptr == 0 || ptr >= 0x80000000) {
            result.found = false;
            result.errorCode = ScanErrors::POINTER_INVALID;
            result.description = "Null or invalid pointer in dereference chain";
            return result;
        }

        addr = ptr;
    }

    result.address = addr;
    result.confidence = 0.9f;
    result.errorCode = 0;
    result.description = pattern.name + " + offset " + std::to_string(pointerOffset)
                        + " with " + std::to_string(derefCount) + " derefs";

    return result;
}

MultiScanResult FindMultiplePatterns(const Pattern* patterns, int count,
                                      DWORD startAddr, DWORD endAddr) {
    MultiScanResult multiResult;

    if (!patterns || count <= 0) {
        return multiResult;
    }

    for (int i = 0; i < count; i++) {
        ScanResult r = FindPattern(patterns[i], startAddr, endAddr);
        multiResult.results.push_back(r);
        if (r.found) {
            multiResult.successCount++;
        } else {
            multiResult.failCount++;
        }
    }

    return multiResult;
}

// ============================================================
// 初始化
// ============================================================
bool Init(HANDLE hProcess) {
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) {
        LogScan("ERROR: Invalid process handle");
        return false;
    }

    s_hProcess = hProcess;
    s_inited = true;
    s_lastScanTime = GetNowMs();
    s_scanCount = 0;
    LogScan("Pattern Scanner initialized");
    return true;
}

bool InitWithModule(HANDLE hProcess, DWORD moduleBase, DWORD moduleSize) {
    if (!Init(hProcess)) return false;

    s_moduleBase = moduleBase;
    s_moduleSize = moduleSize;

    LogScan("Module set: 0x%08X - 0x%08X (size: %u bytes)",
            moduleBase, moduleBase + moduleSize, moduleSize);
    return true;
}

void Shutdown() {
    s_inited = false;
    s_hProcess = NULL;
    s_moduleBase = 0;
    s_moduleSize = 0;
    s_lastScanTime = 0;
    s_scanCount = 0;
    LogScan("Pattern Scanner shutdown");
}

bool IsInitialized() {
    return s_inited && s_hProcess != NULL && s_hProcess != INVALID_HANDLE_VALUE;
}

DWORD GetLastScanTime() {
    return s_lastScanTime;
}

// ============================================================
// 快速範圍掃描
// ============================================================
ScanResult ScanInModule(const Pattern& pattern) {
    if (!s_inited || s_moduleBase == 0 || s_moduleSize == 0) {
        ScanResult r = {};
        r.errorCode = ScanErrors::NOT_INITIALIZED;
        r.description = "Scanner not initialized with module";
        return r;
    }

    return FindPattern(pattern, s_moduleBase, s_moduleBase + s_moduleSize);
}

bool ValidateAddress(DWORD address, DWORD size) {
    if (!s_hProcess || address == 0) return false;

    std::vector<uint8_t> buffer(size);
    return ReadMemory(address, buffer.data(), size);
}

// ============================================================
// 預定義 Pattern
// ============================================================
const char* GetMapIDPattern() {
    // movzx ecx, word ptr [rax+??] - 需要根據實際遊戲更新
    return "0F B7 48 ?? 89 4D ??";
}

const char* GetPlayerHPMPPattern() {
    // mov [rbp+??], eax - 需要根據實際遊戲更新
    return "89 45 ?? 89 4D ??";
}

const char* GetMonsterHPBasePattern() {
    // 需要根據實際遊戲二進制更新
    return "8B 86 ?? ?? 00 00 85 C0";
}

// ============================================================
// 完整偏移掃描
// ============================================================
bool ScanAllOffsets(HANDLE hProcess, DWORD moduleBase, DWORD moduleSize) {
    if (!hProcess || moduleBase == 0 || moduleSize == 0) {
        LogScan("ERROR: Invalid parameters for ScanAllOffsets");
        return false;
    }

    if (!InitWithModule(hProcess, moduleBase, moduleSize)) {
        return false;
    }

    LogScan("========================================");
    LogScan("Starting AOB Scan for offsets...");
    LogScan("Module: 0x%08X - 0x%08X (size: %u)",
            moduleBase, moduleBase + moduleSize, moduleSize);
    LogScan("========================================");

    bool anyFound = false;

    // 掃描 MapID Pattern
    {
        Pattern p = ParsePattern(GetMapIDPattern());
        ScanResult r = ScanInModule(p);
        if (r.found) {
            LogScan("MapID Pattern: 0x%08X", r.address);
            anyFound = true;
        } else {
            LogScan("MapID Pattern: NOT FOUND (error: 0x%X)", r.errorCode);
        }
    }

    // 掃描 HP/MP Pattern
    {
        Pattern p = ParsePattern(GetPlayerHPMPPattern());
        ScanResult r = ScanInModule(p);
        if (r.found) {
            LogScan("PlayerHPMP Pattern: 0x%08X", r.address);
            anyFound = true;
        } else {
            LogScan("PlayerHPMP Pattern: NOT FOUND (error: 0x%X)", r.errorCode);
        }
    }

    // 掃描怪物 HP Pattern
    {
        Pattern p = ParsePattern(GetMonsterHPBasePattern());
        ScanResult r = ScanInModule(p);
        if (r.found) {
            LogScan("MonsterHP Pattern: 0x%08X", r.address);
            anyFound = true;
        } else {
            LogScan("MonsterHP Pattern: NOT FOUND (error: 0x%X)", r.errorCode);
        }
    }

    if (!anyFound) {
        LogScan("WARNING: No patterns found!");
        LogScan("The game may have been updated.");
        LogScan("Please use Cheat Engine to find new AOB patterns.");
    }

    return anyFound;
}

} // namespace PatternScanner
