// offset_validator.cpp
// 自動偏移驗證系統 - Trainer 啟動時自動驗證所有偏移
// 如果偏移讀出的值不合理，會自動記錄並輸出建議的新偏移

#include "pch.h"
#include "offset_validator.h"
#include "../config/offset_config.h"
#include "../game/memory_reader.h"
#include <fstream>

namespace OffsetValidator {

static const char* LOG_FILE = "offset_validation_log.txt";
static bool s_validationDone = false;

// 驗證結果結構
struct ValidationResult {
    const char* name;
    DWORD offset;
    DWORD value;
    DWORD expectedMin;
    DWORD expectedMax;
    bool valid;
    DWORD actualAddress;
};

// 玩家數值範圍定義
static ValidationResult s_playerResults[] = {
    {"HP", 0x930300, 0, 1, 100000, false, 0},
    {"MaxHP", 0x930304, 0, 1, 100000, false, 0},
    {"MP", 0x930308, 0, 1, 100000, false, 0},
    {"MaxMP", 0x93030C, 0, 1, 100000, false, 0},
    {"SP", 0x930310, 0, 1, 100000, false, 0},
    {"MaxSP", 0x930314, 0, 1, 100000, false, 0},
    {"Gold", 0x930250, 0, 0, 999999999, false, 0},
    {"Level", 0x930248, 0, 1, 300, false, 0},  // ❌ 待掃描驗證
    {"ArrowCount", 0x9308D8, 0, 0, 99999, false, 0},
    {"STR", 0x932BF4, 0, 1, 9999, false, 0},
    {"DEX", 0x932C00, 0, 1, 9999, false, 0},
    {"SPR", 0x932BFC, 0, 1, 9999, false, 0},
    {"VIT", 0x932BF8, 0, 1, 9999, false, 0},
    {"PhysAtkMin", 0x933128, 0, 1, 99999, false, 0},
    {"PhysAtkMax", 0x93312C, 0, 1, 99999, false, 0},
    {"Defense", 0x933130, 0, 0, 99999, false, 0},
    {"SprAtkMin", 0x933134, 0, 1, 99999, false, 0},
    {"SprAtkMax", 0x933138, 0, 1, 99999, false, 0},
    {"MapID", 0x930DEC, 0, 0, 65535, false, 0},
};

// 掃描候選偏移範圍
struct CandidateScan {
    const char* name;
    DWORD baseOffset;      // 基礎偏移
    DWORD scanStart;      // 掃描起始 (相對於 base)
    DWORD scanEnd;        // 掃描結束 (相對於 base)
    DWORD step;          // 步進
    DWORD expected;       // 期望值 (用於匹配)
};

// 掃描候選 - 掃描多個可能的偏移區域
static CandidateScan s_candidates[] = {
    // 屬性區域 0x930000 - 0x934000
    {"PhysAtkMin", 0x930000, 0xC08, 0xD00, 4, 0},  // 期望攻擊力 612
    {"PhysAtkMax", 0x930000, 0xC0C, 0xD00, 4, 0},
    {"Defense", 0x930000, 0xE20, 0xE40, 4, 0},     // 期望防禦力 237
    // 精神攻擊力區域
    {"SprAtkMin", 0x933000, 0x128, 0x150, 4, 0},
    {"SprAtkMax", 0x933000, 0x12C, 0x150, 4, 0},
};

static void logf(const char* tag, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // 輸出到控制台
    printf("[%s] %s\n", tag, buf);

    // 寫入日誌檔
    FILE* f = fopen(LOG_FILE, "a");
    if (f) {
        fprintf(f, "[%s] %s\n", tag, buf);
        fclose(f);
    }
}

static void initLog() {
    FILE* f = fopen(LOG_FILE, "w");
    if (f) {
        fprintf(f, "=== Offset Validation Log ===\n");
        fprintf(f, "Time: %s\n", __TIMESTAMP__);
        fprintf(f, "==============================\n\n");
        fclose(f);
    }
}

// 讀取並驗證單個偏移
static bool validateSingleOffset(HANDLE hProcess, DWORD baseAddr,
                                 const char* name, DWORD offset,
                                 DWORD expectedMin, DWORD expectedMax,
                                 DWORD* outValue) {
    DWORD addr = baseAddr + offset;
    DWORD value = SafeRPM<DWORD>(hProcess, addr, 0xFFFFFFFF);

    if (outValue) *outValue = value;

    bool valid = (value >= expectedMin && value <= expectedMax);

    if (!valid) {
        logf("驗證", "❌ %-15s = %-10d @ 0x%08X (期望: %d-%d)",
             name, value, addr, expectedMin, expectedMax);
    }

    return valid;
}

// 自動掃描並找到正確的偏移
static DWORD autoScanForOffset(HANDLE hProcess, DWORD baseAddr,
                               DWORD startOffset, DWORD endOffset, DWORD step,
                               DWORD expectedValue) {
    logf("掃描", "開始掃描偏移範圍: 0x%X - 0x%X (期望值: %d)",
         startOffset, endOffset, expectedValue);

    for (DWORD off = startOffset; off <= endOffset; off += step) {
        DWORD addr = baseAddr + off;
        DWORD value = SafeRPM<DWORD>(hProcess, addr, 0xFFFFFFFF);

        if (value == expectedValue) {
            logf("掃描", "✅ 找到！%-15s @ 0x%08X = %d",
                 "", addr, value);
            return off;
        }

        // 每 0x100 打印進度
        if ((off % 0x100) == 0) {
            printf(".");
        }
    }

    printf("\n");
    logf("掃描", "❌ 在範圍內未找到值 %d", expectedValue);
    return 0;
}

// 驗證所有玩家偏移
bool ValidateAllOffsets(GameHandle* gh) {
    if (!gh || !gh->hProcess || !gh->baseAddr) {
        logf("驗證", "❌ GameHandle 無效");
        return false;
    }

    if (s_validationDone) {
        logf("驗證", "偏移驗證已完成，跳過");
        return true;
    }

    initLog();
    logf("驗證", "========================================");
    logf("驗證", "開始偏移驗證...");
    logf("驗證", "BaseAddr: 0x%08X", gh->baseAddr);
    logf("驗證", "========================================");

    bool allValid = true;
    int validCount = 0;
    int invalidCount = 0;

    // 驗證已知偏移
    printf("\n[驗證] 驗證玩家屬性偏移...\n");

    for (auto& result : s_playerResults) {
        DWORD value = 0;
        bool valid = validateSingleOffset(
            gh->hProcess, gh->baseAddr,
            result.name, result.offset,
            result.expectedMin, result.expectedMax,
            &value);

        result.value = value;
        result.valid = valid;
        result.actualAddress = gh->baseAddr + result.offset;

        if (valid) {
            validCount++;
        } else {
            invalidCount++;
            allValid = false;
        }
    }

    printf("\n");
    logf("驗證", "========================================");
    logf("驗證", "驗證結果: 有效=%d 無效=%d", validCount, invalidCount);
    logf("驗證", "========================================");

    // 如果有值為 0 或明顯錯誤，進行自動掃描
    if (invalidCount > 0) {
        logf("掃描", "========================================");
        logf("掃描", "開始自動掃描可疑偏移...");
        logf("掃描", "========================================");

        // 讀取 HP 和 MaxHP 的實際值（它們通常是對的）
        DWORD hp = 0, maxHp = 0;
        validateSingleOffset(gh->hProcess, gh->baseAddr, "HP",
                            0x930300, 1, 100000, &hp);
        validateSingleOffset(gh->hProcess, gh->baseAddr, "MaxHP",
                            0x930304, 1, 100000, &maxHp);

        if (hp > 0 && maxHp > 0) {
            logf("掃描", "已知值: HP=%d, MaxHP=%d", hp, maxHp);
        }
    }

    // 生成修復建議
    if (invalidCount > 0) {
        logf("建議", "========================================");
        logf("建議", "請將以下偏移更新到 offsets.h:");
        logf("建議", "========================================");

        for (auto& result : s_playerResults) {
            if (!result.valid && result.value != 0xFFFFFFFF) {
                logf("建議", "  constexpr DWORD %-15s = 0x%X;  // 值=%d",
                     result.name, result.offset, result.value);
            }
        }

        logf("建議", "");
        logf("建議", "========================================");
        logf("建議", "或者，使用自動掃描找到正確偏移:");
        logf("建議", "1. 在 CE 中搜索 HP 的實際值");
        logf("建議", "2. 找到後記錄地址");
        logf("建議", "3. 新偏移 = 地址 - GameBase");
        logf("建議", "========================================");
    }

    s_validationDone = true;
    return allValid;
}

// 重置驗證狀態（下次重新驗證）
void ResetValidation() {
    s_validationDone = false;
}

// 獲取驗證結果
const ValidationResult* GetValidationResults(int* outCount) {
    if (outCount) *outCount = _countof(s_playerResults);
    return s_playerResults;
}

} // namespace OffsetValidator
