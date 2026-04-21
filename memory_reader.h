// memory_reader.h - NT API + SEH 全面保護版 (32bit Win7 高階)
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

// NT_SUCCESS 宏（某些 SDK 版本可能未定義）
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

// NT API 原型
typedef NTSTATUS(NTAPI* pNtReadVirtualMemory)(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, ULONG BufferSize, PULONG ReturnLength);
typedef NTSTATUS(NTAPI* pNtWriteVirtualMemory)(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, ULONG BufferSize, PULONG ReturnLength);

extern pNtReadVirtualMemory  fnNtReadVirtualMemory;
extern pNtWriteVirtualMemory fnNtWriteVirtualMemory;

// 初始化 NT API
bool InitNtMemoryFunctions();

// 安全讀取（NT + SEH 保護）
template<typename T>
T SafeNtRPM(HANDLE hProcess, DWORD addr, T def = T{}) {
    if (!addr || addr < 0x1000) return def;
    T val = def;
    ULONG bytesRead = 0;
    __try {
        if (fnNtReadVirtualMemory) {
            NTSTATUS status = fnNtReadVirtualMemory(hProcess, (PVOID)(DWORD_PTR)addr, &val, sizeof(T), &bytesRead);
            if (!NT_SUCCESS(status) || bytesRead != sizeof(T))
                return def;
        } else if (!ReadProcessMemory(hProcess, (LPCVOID)(DWORD_PTR)addr, &val, sizeof(T), (SIZE_T*)&bytesRead) ||
                   bytesRead != sizeof(T)) {
            return def;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return def;
    }
    return val;
}

// 安全寫入（NT + SEH 保護）
template<typename T>
bool SafeNtWPM(HANDLE hProcess, DWORD addr, T value) {
    if (!addr || addr < 0x1000) return false;
    ULONG bytesWritten = 0;
    __try {
        if (fnNtWriteVirtualMemory) {
            NTSTATUS status = fnNtWriteVirtualMemory(hProcess, (PVOID)(DWORD_PTR)addr, &value, sizeof(T), &bytesWritten);
            return NT_SUCCESS(status) && bytesWritten == sizeof(T);
        }
        return WriteProcessMemory(hProcess, (LPVOID)(DWORD_PTR)addr, &value, sizeof(T), (SIZE_T*)&bytesWritten) &&
               bytesWritten == sizeof(T);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 兼容舊版呼叫
template<typename T>
T SafeRPM(HANDLE hProcess, DWORD addr, T def = T{}) {
    return SafeNtRPM(hProcess, addr, def);
}
