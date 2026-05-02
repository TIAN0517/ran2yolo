// memory_reader.cpp - NT API + SEH 全面保護版
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include "memory_reader.h"
#include "game_process.h"
#include "../platform/win32_helpers.h"
#include <winternl.h>
#include <cstring>

pNtReadVirtualMemory  fnNtReadVirtualMemory = nullptr;
pNtWriteVirtualMemory fnNtWriteVirtualMemory = nullptr;

bool InitNtMemoryFunctions()
{
    static bool inited = false;
    if (inited) return true;
    inited = true;

    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) return false;

    fnNtReadVirtualMemory  = LoadProcAddressT<pNtReadVirtualMemory>(hNtdll, "NtReadVirtualMemory");
    fnNtWriteVirtualMemory = LoadProcAddressT<pNtWriteVirtualMemory>(hNtdll, "NtWriteVirtualMemory");

    return (fnNtReadVirtualMemory != nullptr && fnNtWriteVirtualMemory != nullptr);
}
