// ============================================================
// 內嵌資源實現（ONNX Runtime DLL）
// ============================================================
#include "embedded_resources.h"
#include <winternl.h>

HRSRC FindEmbeddedResource(int id) {
    return FindResourceW(NULL, MAKEINTRESOURCE(id), MAKEINTRESOURCE(10));
}

HGLOBAL LoadEmbeddedResource(int id) {
    HRSRC hr = FindEmbeddedResource(id);
    if (!hr) return NULL;
    return LoadResource(NULL, hr);
}

DWORD GetEmbeddedResourceSize(int id) {
    HRSRC hr = FindEmbeddedResource(id);
    if (!hr) return 0;
    return SizeofResource(NULL, hr);
}
