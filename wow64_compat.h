#pragma once
// ============================================================
// WOW64 兼容性層
// 讓 x64 程式能讀取 32 位元遊戲進程
// ============================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// ============================================================
// WOW64 禁用/恢復檔案系統重定向
// 讓 x64 程式能訪問 SysWOW64 目錄
// ============================================================
class Wow64FsRedirection {
public:
    Wow64FsRedirection() : m_oldValue(FALSE), m_disabled(false) {}

    // 禁用重定向 (需要 SYSTEM 權限，通常用於讀取 32 位元系統目錄)
    bool Disable() {
        if (m_disabled) return true;

        // 使用 ntdll.dll 中的函式
        typedef BOOL(WINAPI* Wow64DisableWow64FsRedirectionFunc)(PVOID*);
        typedef BOOL(WINAPI* Wow64RevertWow64FsRedirectionFunc)(PVOID);

        static Wow64DisableWow64FsRedirectionFunc disableFunc =
            (Wow64DisableWow64FsRedirectionFunc)
            GetProcAddress(GetModuleHandleA("kernel32.dll"), "Wow64DisableWow64FsRedirection");

        static Wow64RevertWow64FsRedirectionFunc revertFunc =
            (Wow64RevertWow64FsRedirectionFunc)
            GetProcAddress(GetModuleHandleA("kernel32.dll"), "Wow64RevertWow64FsRedirection");

        if (disableFunc) {
            BOOL result = disableFunc(&m_oldValue);
            m_disabled = (result != 0);
            return m_disabled;
        }
        return false;
    }

    // 恢復重定向
    bool Revert() {
        if (!m_disabled) return true;

        typedef BOOL(WINAPI* Wow64RevertWow64FsRedirectionFunc)(PVOID);

        static Wow64RevertWow64FsRedirectionFunc revertFunc =
            (Wow64RevertWow64FsRedirectionFunc)
            GetProcAddress(GetModuleHandleA("kernel32.dll"), "Wow64RevertWow64FsRedirection");

        if (revertFunc && m_oldValue) {
            BOOL result = revertFunc(&m_oldValue);
            m_disabled = !(result != 0);
            return !m_disabled;
        }
        return false;
    }

    ~Wow64FsRedirection() {
        Revert();
    }

private:
    PVOID m_oldValue;
    bool m_disabled;
};

// ============================================================
// 檢測目標進程是否為 32 位元
// ============================================================
inline bool Is32BitProcess(HANDLE hProcess) {
    BOOL isWow64 = FALSE;
    typedef BOOL(WINAPI* IsWow64ProcessFunc)(HANDLE, PBOOL);

    static IsWow64ProcessFunc fn = (IsWow64ProcessFunc)
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsWow64Process");

    if (fn && fn(hProcess, &isWow64)) {
        return isWow64 != FALSE;
    }

    // 如果無法檢測，預設為 32 位元
    return true;
}

// ============================================================
//  WOW64 ReadProcessMemory 包裝
//  自動處理 x64 讀取 x86 進程的情況
// ============================================================
inline bool Wow64ReadProcessMemory(
    HANDLE hProcess,
    LPCVOID lpBaseAddress,
    LPVOID lpBuffer,
    SIZE_T nSize,
    SIZE_T* lpNumberOfBytesRead = NULL
) {
    // 檢查目標是否為 32 位元進程
    if (Is32BitProcess(hProcess)) {
        // 目標是 32 位元進程
        // 在 x64 系統上，需要使用 WOW64 API
        typedef BOOL(WINAPI* Wow64ReadVirtualMemory64Func)(
            HANDLE, PVOID64, PVOID, ULONGONG, PULONGONG);

        static Wow64ReadVirtualMemory64Func read64 = (Wow64ReadVirtualMemory64Func)
            GetProcAddress(GetModuleHandleA("ntdll.dll"), "Wow64ReadVirtualMemory64");

        if (read64) {
            ULONGONG bytesRead = 0;
            BOOL result = read64(hProcess, (PVOID64)lpBaseAddress, lpBuffer, (ULONGONG)nSize, &bytesRead);
            if (lpNumberOfBytesRead) {
                *lpNumberOfBytesRead = (SIZE_T)bytesRead;
            }
            return result != 0;
        }

        // 如果沒有 Wow64ReadVirtualMemory64，使用標準 RPM (在 32 位元環境下)
        return ReadProcessMemory(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesRead) != 0;
    } else {
        // 目標是 64 位元進程，直接讀取
        return ReadProcessMemory(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesRead) != 0;
    }
}

// ============================================================
// 取得模組基址 (支援 x64 讀取 x86)
// ============================================================
inline HMODULE Wow64GetModuleHandle(HANDLE hProcess, const char* moduleName) {
    // 列舉進程模組
    HMODULE hModules[1024];
    DWORD cbNeeded = 0;

    if (!EnumProcessModules(hProcess, hModules, sizeof(hModules), &cbNeeded)) {
        return NULL;
    }

    int moduleCount = cbNeeded / sizeof(HMODULE);

    for (int i = 0; i < moduleCount; i++) {
        char modName[MAX_PATH];
        if (GetModuleBaseNameA(hProcess, hModules[i], modName, sizeof(modName))) {
            if (_stricmp(modName, moduleName) == 0) {
                return hModules[i];
            }
        }
    }

    return NULL;
}

// ============================================================
// 取得模組大小 (支援 x64 讀取 x86)
// ============================================================
inline SIZE_T Wow64GetModuleSize(HANDLE hProcess, HMODULE hModule) {
    MODULEINFO modInfo;
    if (GetModuleInformation(hProcess, hModule, &modInfo, sizeof(modInfo))) {
        return modInfo.lpBaseOfDll ? (SIZE_T)modInfo.SizeOfImage : 0;
    }
    return 0;
}
