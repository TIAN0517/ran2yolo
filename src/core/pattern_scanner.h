// ============================================================
// pattern_scanner.h
// AOB (Array of Bytes) Pattern Scanner for Dynamic Offset Resolution
// 動態特徵碼掃描，取代舊的 legacy/fallback 靜態偏移
// ============================================================
#pragma once
#ifndef _PATTERN_SCANNER_H_
#define _PATTERN_SCANNER_H_

#include <windows.h>
#include <string>
#include <vector>

// ============================================================
// 自定義錯誤碼（杜絕回傳 0 或 NULL 造成狀態機誤判）
// ============================================================
namespace ScanErrors {
    constexpr DWORD NOT_INITIALIZED = 0xDEAD0001;  // Scanner 未初始化
    constexpr DWORD PROCESS_INVALID = 0xDEAD0002;  // 進程句柄無效
    constexpr DWORD MODULE_NOT_FOUND = 0xDEAD0003;   // 模組未找到
    constexpr DWORD PATTERN_INVALID  = 0xDEAD0004;  // Pattern 字串無效
    constexpr DWORD PATTERN_NOT_FOUND = 0xDEAD0005;  // Pattern 未匹配
    constexpr DWORD POINTER_INVALID  = 0xDEAD0006;  // 指標解引用失敗
    constexpr DWORD MEMORY_READ_FAIL = 0xDEAD0007;   // 記憶體讀取失敗
    constexpr DWORD SCAN_TIMEOUT     = 0xDEAD0008;   // 掃描超時
    constexpr DWORD INVALID_PARAM    = 0xDEAD0009;   // 參數無效
}

// Pattern Scanner API
namespace PatternScanner {

// ============================================================
// 掃描結果結構
// ============================================================
struct ScanResult {
    bool found = false;                    // 是否找到
    DWORD address = 0;                     // 找到的地址
    DWORD errorCode = 0;                   // 錯誤碼（失敗時填充）
    float confidence = 0.0f;               // 匹配置信度 [0.0-1.0]
    std::string description;                // 描述

    bool IsValid() const { return found && address != 0 && address < 0x80000000; }
    bool IsError() const { return errorCode != 0; }
};

// ============================================================
// Pattern 結構
// ============================================================
struct Pattern {
    std::vector<uint8_t> bytes;           // 匹配的位元組
    std::vector<bool> mask;                // true=精確匹配, false=萬用字元
    std::string name;                       // Pattern 名稱
    DWORD errorCode = 0;                    // 錯誤碼

    bool IsValid() const { return !bytes.empty() && bytes.size() == mask.size(); }
};

// ============================================================
// 初始化
// ============================================================

// 初始化掃描器（必須先調用）
bool Init(HANDLE hProcess);

// 初始化帶模組資訊
bool InitWithModule(HANDLE hProcess, DWORD moduleBase, DWORD moduleSize);

// 清理資源
void Shutdown();

// 是否已初始化
bool IsInitialized();

// 獲取上次掃描時間
DWORD GetLastScanTime();

// ============================================================
// Pattern 解析
// ============================================================

// 從字串解析 Pattern
// 格式: "48 8B 05 ?? ?? ?? ?? 48 85 C0"
// ?? = 萬用字元
Pattern ParsePattern(const char* patternStr);

// ============================================================
// 記憶體掃描
// ============================================================

// 掃描單個 Pattern，返回第一個匹配
ScanResult FindPattern(const Pattern& pattern, DWORD startAddr, DWORD endAddr);

// 掃描並跟隨指標偏移
// pattern + pointerOffset + derefCount  deref 解引用次數
ScanResult FindPatternWithOffset(const Pattern& pattern, int pointerOffset,
                                 int derefCount, DWORD startAddr, DWORD endAddr);

// 批量掃描多個 Pattern
struct MultiScanResult {
    std::vector<ScanResult> results;
    int successCount = 0;
    int failCount = 0;
};
MultiScanResult FindMultiplePatterns(const Pattern* patterns, int count,
                                      DWORD startAddr, DWORD endAddr);

// ============================================================
// 快速範圍掃描（用於定時更新）
// ============================================================

// 在已設置的模組範圍內掃描
ScanResult ScanInModule(const Pattern& pattern);

// 驗證已掃描到的地址是否仍然有效
bool ValidateAddress(DWORD address, DWORD size);

// ============================================================
// 預定義 Ran2 Pattern（需根據遊戲版本更新）
// ============================================================

// MapID Pattern: movzx ecx, word ptr [rax+??]
// 實際 Pattern 需要用 CE 擷取
extern const char* GetMapIDPattern();
extern const char* GetPlayerHPMPPattern();
extern const char* GetMonsterHPBasePattern();

// 執行完整掃描並更新偏移
bool ScanAllOffsets(HANDLE hProcess, DWORD moduleBase, DWORD moduleSize);

// 獲取掃描狀態描述
const char* GetLastErrorDesc(DWORD errorCode);

} // namespace PatternScanner

#endif // _PATTERN_SCANNER_H_
