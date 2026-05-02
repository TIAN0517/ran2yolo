#pragma once

#include <cctype>
#include <cstring>
#include <windows.h>

template <typename FnT>
inline FnT LoadProcAddressT(HMODULE module, const char* name) {
    FARPROC raw = GetProcAddress(module, name);
    FnT fn = nullptr;
    if (raw) {
        std::memcpy(&fn, &raw, sizeof(fn));
    }
    return fn;
}

inline void GetExeDirectoryA(char* dir, size_t dirSize) {
    if (!dir || dirSize == 0) return;
    GetModuleFileNameA(NULL, dir, (DWORD)dirSize);
    char* lastSlash = strrchr(dir, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
}

inline bool IsAbsolutePathA(const char* path) {
    if (!path || !path[0]) return false;
    size_t len = std::strlen(path);
    if (len >= 2 && ((path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/'))) {
        return true;
    }
    if (len >= 3 && std::isalpha((unsigned char)path[0]) && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/')) {
        return true;
    }
    return false;
}
