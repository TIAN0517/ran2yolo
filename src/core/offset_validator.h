// offset_validator.h
// 自動偏移驗證系統

#pragma once

#include "../game/game_process.h"

namespace OffsetValidator {

// 驗證所有偏移
bool ValidateAllOffsets(GameHandle* gh);

// 重置驗證狀態
void ResetValidation();

// 獲取驗證結果結構
struct ValidationResult {
    const char* name;
    DWORD offset;
    DWORD value;
    DWORD expectedMin;
    DWORD expectedMax;
    bool valid;
    DWORD actualAddress;
};

const ValidationResult* GetValidationResults(int* outCount);

} // namespace OffsetValidator
