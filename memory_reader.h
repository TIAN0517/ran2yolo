// memory_reader.h - NT API + SEH 全面保護版 (x86/x64 雙支援)
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winternl.h>

#ifndef _MSC_VER
#ifndef __try
#define __try try
#endif
#ifndef __except
#define __except(filter) catch (...)
#endif
#ifndef EXCEPTION_EXECUTE_HANDLER
#define EXCEPTION_EXECUTE_HANDLER 1
#endif
#endif

// NT_SUCCESS 宏
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

// 統一地址類型 (x86/x64 兼容)
#ifdef _WIN64
    typedef DWORD64 ADDR;
    #define ADDR_FORMAT "0x%016llX"
#else
    typedef DWORD ADDR;
    #define ADDR_FORMAT "0x%08X"
#endif

// NT API 原型
typedef NTSTATUS(NTAPI* pNtReadVirtualMemory)(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, ULONG BufferSize, PULONG ReturnLength);
typedef NTSTATUS(NTAPI* pNtWriteVirtualMemory)(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, ULONG BufferSize, PULONG ReturnLength);

extern pNtReadVirtualMemory  fnNtReadVirtualMemory;
extern pNtWriteVirtualMemory fnNtWriteVirtualMemory;

// 初始化 NT API
bool InitNtMemoryFunctions();

// 安全讀取（NT + SEH 保護）- 使用 ADDR 類型
template<typename T>
T SafeNtRPM(HANDLE hProcess, ADDR addr, T def = T{}) {
    if (!addr || addr < 0x1000) return def;
    T val = def;
    ULONG bytesRead = 0;
    __try {
        if (fnNtReadVirtualMemory) {
            NTSTATUS status = fnNtReadVirtualMemory(hProcess, (PVOID)(ULONG_PTR)addr, &val, sizeof(T), &bytesRead);
            if (!NT_SUCCESS(status) || bytesRead != sizeof(T))
                return def;
        } else if (!ReadProcessMemory(hProcess, (LPCVOID)(ULONG_PTR)addr, &val, sizeof(T), (SIZE_T*)&bytesRead) ||
                   bytesRead != sizeof(T)) {
            return def;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return def;
    }
    return val;
}

// 安全讀取（DWORD 版本，兼容舊代碼）
template<typename T>
T SafeRPM(HANDLE hProcess, DWORD addr, T def = T{}) {
    return SafeNtRPM(hProcess, (ADDR)addr, def);
}

// 安全寫入（NT + SEH 保護）
template<typename T>
bool SafeNtWPM(HANDLE hProcess, ADDR addr, T value) {
    if (!addr || addr < 0x1000) return false;
    ULONG bytesWritten = 0;
    __try {
        if (fnNtWriteVirtualMemory) {
            NTSTATUS status = fnNtWriteVirtualMemory(hProcess, (PVOID)(ULONG_PTR)addr, &value, sizeof(T), &bytesWritten);
            return NT_SUCCESS(status) && bytesWritten == sizeof(T);
        }
        return WriteProcessMemory(hProcess, (LPVOID)(ULONG_PTR)addr, &value, sizeof(T), (SIZE_T*)&bytesWritten) &&
               bytesWritten == sizeof(T);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 安全寫入（DWORD 版本，兼容舊代碼）
template<typename T>
bool SafeWPM(HANDLE hProcess, DWORD addr, T value) {
    return SafeNtWPM(hProcess, (ADDR)addr, value);
}

// 讀取指標（自動處理 x86/x64）
template<typename T>
T SafeRPM_Ptr(HANDLE hProcess, ADDR ptrAddr, T def = T{}) {
    // 先讀取指標值
    ADDR ptrValue = SafeNtRPM<ADDR>(hProcess, ptrAddr, 0);
    if (!ptrValue) return def;
    // 再讀取指標指向的內容
    return SafeNtRPM<T>(hProcess, ptrValue, def);
}
