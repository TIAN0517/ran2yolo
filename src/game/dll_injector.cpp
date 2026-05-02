// ============================================================
// DLL 注入器 - 簡化版 (CreateRemoteThread + LoadLibraryA)
// ============================================================
#include "dll_injector.h"
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

// ============================================================
// 內部狀態
// ============================================================
static InjectionMethod s_method = InjectionMethod::CREATE_REMOTE_THREAD;
static char s_processName[64] = {0};
static char s_dllPath[MAX_PATH] = {0};
static DWORD s_targetPid = 0;
static HANDLE s_hProcess = NULL;

// ============================================================
// SeDebugPrivilege
// ============================================================
static bool EnableDebugPrivilegeInternal() {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tp.Privileges[0].Luid)) {
        CloseHandle(hToken);
        return false;
    }

    BOOL ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
    CloseHandle(hToken);
    return ok != FALSE;
}

// ============================================================
// 查找進程
// ============================================================
static DWORD FindProcessByNameInternal(const char* processName) {
    if (!processName) return 0;

    PROCESSENTRY32W pe = {sizeof(PROCESSENTRY32W)};
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    DWORD pid = 0;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            char exeName[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, pe.szExeFile, -1, exeName, sizeof(exeName), NULL, NULL);
            if (strcmp(exeName, processName) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return pid;
}

// ============================================================
// 注入 DLL
// ============================================================
static struct InjectionResult InjectDllInternal(DWORD pid, const char* dllPath) {
    struct InjectionResult result = {0};
    result.success = false;
    result.injectedPid = pid;

    if (!pid || !dllPath || !*dllPath) {
        strncpy_s(result.errorMsg, sizeof(result.errorMsg), "參數錯誤", _TRUNCATE);
        return result;
    }

    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProcess) {
        result.errorCode = GetLastError();
        snprintf(result.errorMsg, sizeof(result.errorMsg), "OpenProcess 失敗: %lu", result.errorCode);
        return result;
    }

    size_t pathLen = strlen(dllPath);
    LPVOID remotePath = VirtualAllocEx(hProcess, NULL, pathLen + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath) {
        result.errorCode = GetLastError();
        CloseHandle(hProcess);
        snprintf(result.errorMsg, sizeof(result.errorMsg), "VirtualAllocEx 失敗: %lu", result.errorCode);
        return result;
    }

    if (!WriteProcessMemory(hProcess, remotePath, dllPath, pathLen + 1, NULL)) {
        result.errorCode = GetLastError();
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        snprintf(result.errorMsg, sizeof(result.errorMsg), "WriteProcessMemory 失敗: %lu", result.errorCode);
        return result;
    }

    LPVOID loadLibAddr = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    if (!loadLibAddr) {
        result.errorCode = GetLastError();
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        strncpy_s(result.errorMsg, sizeof(result.errorMsg), "GetProcAddress 失敗", _TRUNCATE);
        return result;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)loadLibAddr, remotePath, 0, NULL);
    if (!hThread) {
        result.errorCode = GetLastError();
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        snprintf(result.errorMsg, sizeof(result.errorMsg), "CreateRemoteThread 失敗: %lu", result.errorCode);
        return result;
    }

    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    result.success = true;
    strncpy_s(result.errorMsg, sizeof(result.errorMsg), "注入成功", _TRUNCATE);
    return result;
}

// ============================================================
// 公開 API (extern "C")
// ============================================================
EXTERN_C void DllInjector_SetMethod(InjectionMethod method) {
    s_method = method;
}

EXTERN_C void DllInjector_SetTargetProcess(const char* processName) {
    if (processName) {
        strncpy_s(s_processName, sizeof(s_processName), processName, _TRUNCATE);
        s_targetPid = 0;
    }
}

EXTERN_C void DllInjector_SetTargetPid(DWORD pid) {
    s_targetPid = pid;
}

EXTERN_C void DllInjector_SetDllPath(const char* dllPath) {
    if (dllPath) {
        strncpy_s(s_dllPath, sizeof(s_dllPath), dllPath, _TRUNCATE);
    }
}

EXTERN_C BOOL DllInjector_EnableDebugPrivilege(void) {
    return EnableDebugPrivilegeInternal() ? TRUE : FALSE;
}

EXTERN_C DWORD DllInjector_FindProcessByName(const char* processName) {
    return FindProcessByNameInternal(processName);
}

EXTERN_C struct InjectionResult DllInjector_Inject(void) {
    struct InjectionResult result = {0};

    EnableDebugPrivilegeInternal();

    DWORD pid = s_targetPid;
    if (pid == 0 && s_processName[0]) {
        pid = FindProcessByNameInternal(s_processName);
    }

    if (pid == 0) {
        strncpy_s(result.errorMsg, sizeof(result.errorMsg), "找不到目標進程", _TRUNCATE);
        return result;
    }

    char dllPath[MAX_PATH] = {0};
    if (s_dllPath[0]) {
        strncpy_s(dllPath, sizeof(dllPath), s_dllPath, _TRUNCATE);
    } else {
        GetModuleFileNameA(NULL, dllPath, MAX_PATH);
        char* lastSlash = strrchr(dllPath, '\\');
        if (lastSlash) {
            strncpy_s(lastSlash + 1, 32, "NetHook.dll", _TRUNCATE);
        }
    }

    return InjectDllInternal(pid, dllPath);
}

EXTERN_C BOOL DllInjector_IsProcessRunning(const char* processName) {
    return FindProcessByNameInternal(processName) != 0 ? TRUE : FALSE;
}
