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
#include "game_process.h"
#include <psapi.h>
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

// ============================================================
// 共享內存日誌讀取
// ============================================================
#define NETHOOK_LOG_ENTRY_SZ  256
#define NETHOOK_LOG_BUF_COUNT 160

// 日誌偏移: Header + Entities + InvHeader + Items + PlayerCoords + LogHeader
// 128 + 500*32 + 128 + 78*16 + 32 = 17536
#define NETHOOK_LOG_HEADER_OFFSET (128 + NETHOOK_MAX_ENTITIES * 32 + 128 + NETHOOK_INVENTORY_MAX * NETHOOK_INVENTORY_STRIDE + 32)

static ShmemLogHeader* GetLogHeader(void) {
    if (!s_pShmem) return NULL;
    ShmemHeader* hdr = (ShmemHeader*)s_pShmem;
    if (hdr->magic != 0xDEADBEEF) return NULL;
    return (ShmemLogHeader*)((BYTE*)s_pShmem + NETHOOK_LOG_HEADER_OFFSET);
}

int NetHookShmem_ReadLogs(ShmemLogEntry* out, int max) {
    if (!out || max <= 0 || !s_pShmem) return 0;
    memset(out, 0, sizeof(ShmemLogEntry) * max);

    ShmemLogHeader* lhdr = GetLogHeader();
    if (!lhdr) return 0;
    if (lhdr->magic != 0xDEAD5517) return 0;

    ShmemLogEntry* logBuf = (ShmemLogEntry*)((BYTE*)lhdr + sizeof(ShmemLogHeader));

    LONG writeIdx = lhdr->writeIndex;
    LONG readIdx = lhdr->readIndex;

    if (writeIdx == readIdx) return 0;  // 沒有新日誌

    int count = 0;
    while (readIdx != writeIdx && count < max) {
        LONG idx = readIdx % NETHOOK_LOG_BUF_COUNT;
        out[count] = logBuf[idx];
        count++;
        readIdx++;
    }

    // 更新讀取位置
    lhdr->readIndex = readIdx;
    return count;
}

// ============================================================
// Python Packet Listener 座標讀取
// ============================================================
static HANDLE s_hCoordsMap = NULL;
static void*  s_pCoordsShmem = NULL;

bool ReadCoordsFromPython(float* outX, float* outZ, float* outY) {
    if (!outX || !outZ || !outY) return false;
    *outX = 0.0f;
    *outZ = 0.0f;
    *outY = 0.0f;

    // 開啟共享記憶體
    if (!s_hCoordsMap) {
        s_hCoordsMap = OpenFileMappingA(
            FILE_MAP_ALL_ACCESS,
            FALSE,
            COORDS_SHMEM_NAME
        );
        if (!s_hCoordsMap) return false;

        s_pCoordsShmem = MapViewOfFile(
            s_hCoordsMap,
            FILE_MAP_ALL_ACCESS,
            0, 0,
            COORDS_SHMEM_SIZE
        );
        if (!s_pCoordsShmem) {
            CloseHandle(s_hCoordsMap);
            s_hCoordsMap = NULL;
            return false;
        }
    }

    if (!s_pCoordsShmem) return false;

    CoordsData* data = (CoordsData*)s_pCoordsShmem;

    // 檢查魔數
    if (data->magic != 0xCAFEBABE) return false;
    if (data->valid != 1) return false;

    *outX = data->x;
    *outZ = data->z;
    *outY = data->y;
    return true;
}

bool IsPythonCoordsValid(void) {
    if (!s_pCoordsShmem) {
        // 嘗試連接
        ReadCoordsFromPython(NULL, NULL, NULL);
        if (!s_pCoordsShmem) return false;
    }

    CoordsData* data = (CoordsData*)s_pCoordsShmem;
    return data->magic == 0xCAFEBABE && data->valid == 1;
}

// ============================================================
// NetHook DLL 玩家座標讀取
// 偏移: 128 + 500*32 + 128 + 78*16 + 32 = 17536
// ============================================================
static bool IsNetHookCoordsConnected(void) {
    return s_connected && s_pShmem != NULL;
}

bool ReadPlayerCoordsFromNetHook(float* outX, float* outZ, float* outY) {
    if (!outX || !outZ || !outY) return false;
    *outX = 0.0f;
    *outZ = 0.0f;
    *outY = 0.0f;

    if (!IsNetHookCoordsConnected()) {
        // 嘗試連接共享記憶體
        if (!NetHookShmem_Connect()) return false;
    }

    if (!s_pShmem) return false;

    // 讀取玩家座標結構
    NetHookPlayerCoords* coords = (NetHookPlayerCoords*)((BYTE*)s_pShmem + NETHOOK_PLAYER_COORDS_OFFSET);
    if (!coords) return false;

    // 檢查魔數
    if (coords->magic != 0xCAFEB00B) return false;
    if (coords->valid != 1) return false;

    *outX = coords->x;
    *outZ = coords->z;
    *outY = coords->y;
    return true;
}

bool IsNetHookCoordsValid(void) {
    if (!IsNetHookCoordsConnected()) {
        if (!NetHookShmem_Connect()) return false;
    }

    if (!s_pShmem) return false;

    NetHookPlayerCoords* coords = (NetHookPlayerCoords*)((BYTE*)s_pShmem + NETHOOK_PLAYER_COORDS_OFFSET);
    if (!coords) return false;

    return coords->magic == 0xCAFEB00B && coords->valid == 1;
}

// ============================================================
// 讀取 DLL 日誌檔案
// 優先順序：遊戲目錄 > Temp
// ============================================================
int ReadDllLogFile(char* out, int maxLen) {
    if (!out || maxLen <= 0) return 0;

    char path[MAX_PATH] = {0};
    bool found = false;

    // 嘗試讀取遊戲目錄下的日誌
    GameHandle gh = GetGameHandle();
    if (gh.hProcess) {
        char gamePath[MAX_PATH] = {0};
        DWORD len = GetModuleFileNameExA(gh.hProcess, NULL, gamePath, MAX_PATH);
        if (len > 0) {
            char* lastSlash = strrchr(gamePath, '\\');
            if (lastSlash) {
                strcpy_s(lastSlash + 1, MAX_PATH - (lastSlash - gamePath) - 1, "JyNetHook.log");
                strncpy_s(path, gamePath, MAX_PATH - 1);
                found = true;
            }
        }
    }

    // 如果遊戲目錄找不到，嘗試 Temp
    if (!found) {
        DWORD len = GetTempPathA(MAX_PATH, path);
        if (len == 0 || len >= MAX_PATH) return 0;
        strcat_s(path, MAX_PATH, "JyNetHook.log");
    }

    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE) return 0;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize >= (DWORD)maxLen - 1) {
        CloseHandle(hFile);
        return 0;
    }

    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, out, maxLen - 1, &bytesRead, NULL);
    CloseHandle(hFile);

    if (!ok || bytesRead == 0) return 0;

    out[bytesRead] = '\0';
    return (int)bytesRead;
}
