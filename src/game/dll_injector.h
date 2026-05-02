#pragma once
// ============================================================
// DLL 注入器 - 簡化版
// ============================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// ============================================================
// 注入方法
// ============================================================
enum class InjectionMethod {
    CREATE_REMOTE_THREAD,   // 經典方法
    SET_WINDOWS_HOOK_EX,    // SetWindowsHookEx
    NTMAPCURRENTTHREAD,     // NtMapCodeSection
    USERMODE_CALLBACK,      // APC
};

// ============================================================
// 注入結果 (C struct - 可用於 extern "C")
// ============================================================
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif

struct InjectionResult {
    bool success;
    DWORD errorCode;
    char errorMsg[256];
    DWORD injectedPid;
    HANDLE hProcess;
};

// ============================================================
// C API
// ============================================================
EXTERN_C void DllInjector_SetMethod(InjectionMethod method);
EXTERN_C void DllInjector_SetTargetProcess(const char* processName);
EXTERN_C void DllInjector_SetTargetPid(DWORD pid);
EXTERN_C void DllInjector_SetDllPath(const char* dllPath);
EXTERN_C BOOL DllInjector_EnableDebugPrivilege(void);
EXTERN_C DWORD DllInjector_FindProcessByName(const char* processName);
EXTERN_C struct InjectionResult DllInjector_Inject(void);
EXTERN_C BOOL DllInjector_IsProcessRunning(const char* processName);
