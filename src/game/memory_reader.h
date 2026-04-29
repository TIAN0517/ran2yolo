// ============================================================
// memory_reader.h
// NT API + SEH 全面保護版 (x86/x64 雙支援)
// 錯誤碼系統：杜絕回傳 0 或 NULL 造成狀態機誤判
// ============================================================
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winternl.h>
#include <cstring>

// ============================================================
// 自定義錯誤碼命名空間（杜絕回傳 0 或 NULL）
// ============================================================
namespace MemErrors {
    // 通用無效值
    constexpr int   MAPID_INVALID    = -1;           // 地圖ID無效
    constexpr int   HP_INVALID       = -2;            // HP 無效
    constexpr int   MP_INVALID       = -3;            // MP 無效
    constexpr int   SP_INVALID       = -4;            // SP 無效
    constexpr int   GOLD_INVALID     = -5;            // 金幣無效
    constexpr int   LEVEL_INVALID    = -6;            // 等級無效
    constexpr int   COUNT_INVALID    = -7;            // 計數無效
    constexpr int   COORD_INVALID    = -9999;         // 座標無效
    constexpr int   DISTANCE_INVALID = -8888;         // 距離無效

    // 指針/地址無效標記
    constexpr DWORD PTR_INVALID      = 0xBAADF00D;    // 指針無效標記
    constexpr DWORD ADDR_INVALID     = 0xDEAD0000;    // 地址無效標記
    constexpr DWORD ADDR_SCAN_FAIL  = 0xDEAD0001;    // Pattern掃描失敗
    constexpr DWORD ADDR_PROTECTED  = 0xDEAD0002;    // 保護區域
    constexpr DWORD ADDR_NOT_FOUND  = 0xDEAD0003;    // 數據未找到
    constexpr DWORD ADDR_TIMEOUT    = 0xDEAD0004;    // 讀取超時

    // 讀取狀態碼
    constexpr int   READ_FAILED      = -1000;        // 讀取失敗
    constexpr int   READ_TIMEOUT     = -1001;         // 讀取超時
    constexpr int   READ_PROTECTED   = -1002;         // 讀取保護區
    constexpr int   READ_VERIFY_FAIL = -1003;         // 讀取後驗證失敗
}

// ============================================================
// NT API 原型
// ============================================================
typedef NTSTATUS(NTAPI* pNtReadVirtualMemory)(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, ULONG BufferSize, PULONG ReturnLength);
typedef NTSTATUS(NTAPI* pNtWriteVirtualMemory)(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, ULONG BufferSize, PULONG ReturnLength);

extern pNtReadVirtualMemory  fnNtReadVirtualMemory;
extern pNtWriteVirtualMemory fnNtWriteVirtualMemory;

// 初始化 NT API
bool InitNtMemoryFunctions();

// ============================================================
// 統一地址類型 (x86/x64 兼容)
// ============================================================
#ifdef _WIN64
    typedef DWORD64 ADDR;
    #define ADDR_FORMAT "0x%016llX"
#else
    typedef DWORD ADDR;
    #define ADDR_FORMAT "0x%08X"
#endif

// ============================================================
// 讀取驗證結果
// ============================================================
struct ReadResult {
    bool success;
    union {
        ADDR addr;
        int intVal;
        float floatVal;
    };
    DWORD errorCode;  // 錯誤碼（失敗時填充）
};

// ============================================================
// 地址驗證
// ============================================================
inline bool IsValidReadAddr(ADDR addr) {
    return addr != 0 &&
           addr >= 0x1000 &&
           addr < 0x7FFFFFFF &&
           addr != MemErrors::ADDR_INVALID &&
           addr != MemErrors::ADDR_SCAN_FAIL &&
           addr != MemErrors::ADDR_PROTECTED &&
           addr != MemErrors::ADDR_NOT_FOUND;
}

// ============================================================
// 安全讀取（NT + SEH 保護）
// ============================================================
template<typename T>
T SafeNtRPM(HANDLE hProcess, ADDR addr, T def = T{}) {
    if (!addr || addr < 0x1000) return def;
    if (addr >= MemErrors::ADDR_INVALID) return def;  // 過濾錯誤碼

    T val = def;
    ULONG bytesRead = 0;
    __try {
        if (fnNtReadVirtualMemory) {
            NTSTATUS status = fnNtReadVirtualMemory(hProcess, (PVOID)(ULONG_PTR)addr,
                                                     &val, sizeof(T), &bytesRead);
            if (!NT_SUCCESS(status) || bytesRead != sizeof(T))
                return def;
        } else if (!ReadProcessMemory(hProcess, (LPCVOID)(ULONG_PTR)addr,
                                       &val, sizeof(T), (SIZE_T*)&bytesRead) ||
                   bytesRead != sizeof(T)) {
            return def;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return def;
    }
    return val;
}

// ============================================================
// 讀取後驗證
// ============================================================
template<typename T>
T ValidatedRead(HANDLE hProcess, ADDR addr, T def, bool* outOk = nullptr) {
    T val = SafeNtRPM<T>(hProcess, addr, def);
    if (outOk) {
        *outOk = (val != def);
    }
    return val;
}

// ============================================================
// 專用安全讀取函數（返回明確錯誤碼而非 0）
// ============================================================

// 讀取 MapID：0 表示未知/無效
inline int SafeReadMapID(HANDLE hProcess, ADDR addr) {
    if (!IsValidReadAddr(addr)) return MemErrors::MAPID_INVALID;

    int val = SafeNtRPM<int>(hProcess, addr, MemErrors::MAPID_INVALID);
    if (val <= 0 || val > 65535) {
        return MemErrors::MAPID_INVALID;
    }
    return val;
}

// 讀取座標：超出範圍返回 COORD_INVALID
inline float SafeReadPos(HANDLE hProcess, ADDR addr) {
    if (!IsValidReadAddr(addr)) return (float)MemErrors::COORD_INVALID;

    float val = SafeNtRPM<float>(hProcess, addr, (float)MemErrors::COORD_INVALID);
    if (val < -100000.0f || val > 100000.0f) {
        return (float)MemErrors::COORD_INVALID;
    }
    return val;
}

// 讀取 HP：無效返回 HP_INVALID
inline int SafeReadHP(HANDLE hProcess, ADDR addr) {
    if (!IsValidReadAddr(addr)) return MemErrors::HP_INVALID;

    int val = SafeNtRPM<int>(hProcess, addr, MemErrors::HP_INVALID);
    if (val < 0 || val > 10000000) {
        return MemErrors::HP_INVALID;
    }
    return val;
}

// 讀取 MP：無效返回 MP_INVALID
inline int SafeReadMP(HANDLE hProcess, ADDR addr) {
    if (!IsValidReadAddr(addr)) return MemErrors::MP_INVALID;

    int val = SafeNtRPM<int>(hProcess, addr, MemErrors::MP_INVALID);
    if (val < 0 || val > 1000000) {
        return MemErrors::MP_INVALID;
    }
    return val;
}

// 讀取 SP：無效返回 SP_INVALID
inline int SafeReadSP(HANDLE hProcess, ADDR addr) {
    if (!IsValidReadAddr(addr)) return MemErrors::SP_INVALID;

    int val = SafeNtRPM<int>(hProcess, addr, MemErrors::SP_INVALID);
    if (val < 0 || val > 100000) {
        return MemErrors::SP_INVALID;
    }
    return val;
}

// 讀取金幣：無效返回 GOLD_INVALID
inline int SafeReadGold(HANDLE hProcess, ADDR addr) {
    if (!IsValidReadAddr(addr)) return MemErrors::GOLD_INVALID;

    int val = SafeNtRPM<int>(hProcess, addr, MemErrors::GOLD_INVALID);
    if (val < 0) {
        return MemErrors::GOLD_INVALID;
    }
    return val;
}

// ============================================================
// 向後兼容版本
// ============================================================
template<typename T>
T SafeRPM(HANDLE hProcess, DWORD addr, T def = T{}) {
    return SafeNtRPM(hProcess, (ADDR)addr, def);
}

// ============================================================
// 安全寫入
// ============================================================
template<typename T>
bool SafeNtWPM(HANDLE hProcess, ADDR addr, T value) {
    if (!addr || addr < 0x1000) return false;
    if (addr >= MemErrors::ADDR_INVALID) return false;

    ULONG bytesWritten = 0;
    __try {
        if (fnNtWriteVirtualMemory) {
            NTSTATUS status = fnNtWriteVirtualMemory(hProcess, (PVOID)(ULONG_PTR)addr,
                                                     &value, sizeof(T), &bytesWritten);
            return NT_SUCCESS(status) && bytesWritten == sizeof(T);
        }
        return WriteProcessMemory(hProcess, (LPVOID)(ULONG_PTR)addr,
                                  &value, sizeof(T), (SIZE_T*)&bytesWritten) &&
               bytesWritten == sizeof(T);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template<typename T>
bool SafeWPM(HANDLE hProcess, DWORD addr, T value) {
    return SafeNtWPM(hProcess, (ADDR)addr, value);
}

// ============================================================
// 指針讀取（自動處理 x86/x64）
// ============================================================
template<typename T>
T SafeRPM_Ptr(HANDLE hProcess, ADDR ptrAddr, T def = T{}) {
    if (!IsValidReadAddr(ptrAddr)) return def;

    ADDR ptrValue = SafeNtRPM<ADDR>(hProcess, ptrAddr, 0);
    if (!ptrValue) return def;

    return SafeNtRPM<T>(hProcess, ptrValue, def);
}

// ============================================================
// 指針有效性檢查
// ============================================================
inline bool IsGoodPtr(DWORD addr) {
    return addr != 0 &&
           addr < 0x7FFFFFFF &&
           addr != MemErrors::PTR_INVALID;
}

inline bool IsGoodPtr64(ADDR addr) {
    return addr != 0 &&
           addr < 0x7FFFFFFFFFFF &&
           addr != MemErrors::ADDR_INVALID &&
           addr != MemErrors::ADDR_SCAN_FAIL &&
           addr != MemErrors::ADDR_PROTECTED;
}
