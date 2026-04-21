// ============================================================
// NetHook 共享記憶體客戶端實作
// ============================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include "nethook_shmem.h"
#include "memory_reader.h"
#include <cmath>

// ============================================================
// 內部狀態
// ============================================================
static HANDLE s_hMapFile = NULL;
static void*  s_pShmem = NULL;
static bool   s_connected = false;

// ============================================================
// 連接共享記憶體
// ============================================================
bool NetHookShmem_Connect(void) {
    if (s_connected && s_pShmem) return true;

    s_hMapFile = OpenFileMappingA(
        FILE_MAP_ALL_ACCESS,
        FALSE,
        NETHOOK_SHMEM_NAME
    );

    if (!s_hMapFile) {
        s_hMapFile = NULL;
        s_pShmem = NULL;
        s_connected = false;
        return false;
    }

    s_pShmem = MapViewOfFile(
        s_hMapFile,
        FILE_MAP_ALL_ACCESS,
        0, 0,
        NETHOOK_SHMEM_SIZE_V2
    );

    if (!s_pShmem) {
        CloseHandle(s_hMapFile);
        s_hMapFile = NULL;
        s_connected = false;
        return false;
    }

    s_connected = true;
    return true;
}

// ============================================================
// 斷開連接
// ============================================================
void NetHookShmem_Disconnect(void) {
    if (s_pShmem) {
        UnmapViewOfFile(s_pShmem);
        s_pShmem = NULL;
    }
    if (s_hMapFile) {
        CloseHandle(s_hMapFile);
        s_hMapFile = NULL;
    }
    s_connected = false;
}

// ============================================================
// 檢查連接
// ============================================================
bool NetHookShmem_IsConnected(void) {
    return s_connected && s_pShmem != NULL;
}

// ============================================================
// 驗證魔數
// ============================================================
static bool IsValidHeader(void) {
    if (!s_pShmem) return false;
    DWORD magic = *(DWORD*)s_pShmem;
    return magic == 0xDEADBEEF;
}

// ============================================================
// 取得實體數量
// ============================================================
int NetHookShmem_GetEntityCount(void) {
    if (!IsValidHeader()) return 0;
    ShmemHeader* hdr = (ShmemHeader*)s_pShmem;
    return (int)hdr->entityCount;
}

// ============================================================
// 遍歷所有實體
// ============================================================
int NetHookShmem_EnumerateEntities(ShmemEntity* out, int max) {
    if (!IsValidHeader() || !out || max <= 0) return 0;

    ShmemHeader* hdr = (ShmemHeader*)s_pShmem;
    int count = (int)hdr->entityCount;
    if (count > NETHOOK_MAX_ENTITIES) count = NETHOOK_MAX_ENTITIES;
    if (count > max) count = max;

    ShmemEntity* entities = (ShmemEntity*)((BYTE*)s_pShmem + 128);
    for (int i = 0; i < count; i++) {
        out[i] = entities[i];
    }
    return count;
}

// ============================================================
// 找最近怪物（type=2）
// ============================================================
bool NetHookShmem_GetNearestMonster(float px, float pz, ShmemEntity* out) {
    if (!IsValidHeader() || !out) return false;

    memset(out, 0, sizeof(*out));
    float bestDist = 99999.0f;

    ShmemHeader* hdr = (ShmemHeader*)s_pShmem;
    int count = (int)hdr->entityCount;
    if (count > NETHOOK_MAX_ENTITIES) count = NETHOOK_MAX_ENTITIES;

    ShmemEntity* entities = (ShmemEntity*)((BYTE*)s_pShmem + 128);
    bool found = false;

    for (int i = 0; i < count; i++) {
        ShmemEntity& e = entities[i];
        if (e.type != 2) continue;         // 只管怪物
        if (e.dead) continue;               // 死亡不算
        if (e.id == 0) continue;            // 無效 ID

        float dx = e.x - px;
        float dz = e.z - pz;
        float dist = (float)std::sqrt(dx*dx + dz*dz);
        if (dist < bestDist) {
            *out = e;
            bestDist = dist;
            found = true;
        }
    }
    return found;
}

// ============================================================
// 找最近 NPC（type=1）
// ============================================================
bool NetHookShmem_GetNearestNPC(float px, float pz, ShmemEntity* out) {
    if (!IsValidHeader() || !out) return false;

    memset(out, 0, sizeof(*out));
    float bestDist = 99999.0f;

    ShmemHeader* hdr = (ShmemHeader*)s_pShmem;
    int count = (int)hdr->entityCount;
    if (count > NETHOOK_MAX_ENTITIES) count = NETHOOK_MAX_ENTITIES;

    ShmemEntity* entities = (ShmemEntity*)((BYTE*)s_pShmem + 128);
    bool found = false;

    for (int i = 0; i < count; i++) {
        ShmemEntity& e = entities[i];
        if (e.type != 1) continue;         // 只管 NPC
        if (e.dead) continue;
        if (e.id == 0) continue;

        float dx = e.x - px;
        float dz = e.z - pz;
        float dist = (float)std::sqrt(dx*dx + dz*dz);
        if (dist < bestDist) {
            *out = e;
            bestDist = dist;
            found = true;
        }
    }
    return found;
}

// ============================================================
// 庫存讀取（V2: SRV_ITEM_LIST 封包）
// ============================================================
static ShmemInvHeader* GetInvHeader(void) {
    if (!s_pShmem) return NULL;
    ShmemHeader* hdr = (ShmemHeader*)s_pShmem;
    if (hdr->magic != 0xDEADBEEF) return NULL;
    if (hdr->version < 2) return NULL;  // V1 不含庫存
    return (ShmemInvHeader*)((BYTE*)s_pShmem + 128 + NETHOOK_MAX_ENTITIES * 32);
}

int NetHookShmem_GetInventoryCount(void) {
    ShmemInvHeader* ihdr = GetInvHeader();
    if (!ihdr) return 0;
    if (ihdr->lock == 1) return 0;  // 正在寫入
    return (int)ihdr->itemCount;
}

int NetHookShmem_EnumerateInventory(ShmemInvItem* out, int max) {
    if (!out || max <= 0) return 0;
    memset(out, 0, sizeof(ShmemInvItem) * max);

    ShmemInvHeader* ihdr = GetInvHeader();
    if (!ihdr) return 0;
    if (ihdr->lock == 1) return 0;

    int count = (int)ihdr->itemCount;
    if (count > NETHOOK_INVENTORY_MAX) count = NETHOOK_INVENTORY_MAX;
    if (count > max) count = max;

    // Items start right after the inventory header (header is 128 bytes)
    ShmemInvItem* items = (ShmemInvItem*)((BYTE*)ihdr + sizeof(ShmemInvHeader));
    for (int i = 0; i < count; i++) {
        out[i] = items[i];
    }
    return count;
}
