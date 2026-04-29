#pragma once
// ============================================================
// 內嵌資源定義（已停用，資源未定義）
// ============================================================
#include <windows.h>

// 取得資源指標
extern HRSRC FindEmbeddedResource(int id);
extern HGLOBAL LoadEmbeddedResource(int id);
extern DWORD GetEmbeddedResourceSize(int id);
