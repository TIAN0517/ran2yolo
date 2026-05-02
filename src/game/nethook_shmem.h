#pragma once
// ============================================================
// NetHook 共享記憶體客戶端（ JyTrainer 用）
// 通過 OpenFileMapping + MapViewOfFile 打開共享記憶體
// 繞過失效的 TLS/Entity Pool，讀取 recv() hook 維護的 entity cache
//
// V2: 增加庫存讀取（SRV_ITEM_LIST 0x0B00 封包）
// ============================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// 共享記憶體格式（與產生端相同）
#define NETHOOK_SHMEM_NAME     "Local\\RanBot_NetHook"
#define NETHOOK_MAX_ENTITIES   500
#define NETHOOK_INVENTORY_MAX   78
#define NETHOOK_INVENTORY_STRIDE 16
#define NETHOOK_SHMEM_SIZE_V2   (128 + NETHOOK_MAX_ENTITIES * 32 + 128 + NETHOOK_INVENTORY_MAX * NETHOOK_INVENTORY_STRIDE)

// Entity 結構（每個 32 bytes）
struct ShmemEntity {
    DWORD id;      // +0x00
    BYTE  type;    // +0x04: 1=NPC, 2=Monster
    BYTE  dead;     // +0x05
    WORD  padding; // +0x06
    float x;       // +0x08
    float y;       // +0x0C
    float z;       // +0x10
    DWORD hp;      // +0x14
    DWORD maxHp;   // +0x18
    DWORD lastSeen; // +0x1C
};

// Header (128 bytes)
struct ShmemHeader {
    DWORD magic;       // 0xDEADBEEF
    DWORD version;     // 2 (V2: 含庫存)
    DWORD entityCount; // 有效實體數
    DWORD lock;        // InterlockedExchange
};

// 庫存 Header (128 bytes)
struct ShmemInvHeader {
    DWORD magic;       // 0xDEADBEE2
    DWORD version;     // 1
    DWORD itemCount;   // 有效物品數
    DWORD lock;        // InterlockedExchange
};

// 庫存物品（每個 16 bytes）
struct ShmemInvItem {
    WORD  slot;    // +0x00: 格位索引 0~77
    WORD  pad;     // +0x02
    DWORD itemId;  // +0x04: 物品ID
    DWORD count;   // +0x08: 數量
    DWORD _unused; // +0x0C: 填充至 16 bytes
};

// 初始化共享記憶體連接
// 回傳: true=成功, false=失敗
bool NetHookShmem_Connect(void);

// 斷開連接
void NetHookShmem_Disconnect(void);

// 檢查是否已連接
bool NetHookShmem_IsConnected(void);

// 取得實體數量
int NetHookShmem_GetEntityCount(void);

// 遍歷所有實體，回傳數量
// out: 輸出緩衝區
// max: 緩衝區大小
int NetHookShmem_EnumerateEntities(ShmemEntity* out, int max);

// 找最近怪物（type=2）
// px/pz: 玩家座標
// 回傳: true=找到
bool NetHookShmem_GetNearestMonster(float px, float pz, ShmemEntity* out);

// 找最近 NPC（type=1）
bool NetHookShmem_GetNearestNPC(float px, float pz, ShmemEntity* out);

// ============================================================
// 庫存讀取（V2: 從 SRV_ITEM_LIST 封包維護的庫存緩存）
// ============================================================

// 取得庫存物品數量
int NetHookShmem_GetInventoryCount(void);

// 遍歷庫存物品
// out: 輸出緩衝區
// max: 緩衝區大小（應 >= 78）
// 回傳: 實際物品數量
int NetHookShmem_EnumerateInventory(ShmemInvItem* out, int max);

// ============================================================
// 共享內存日誌讀取
// ============================================================

// 日誌頭結構（必須 32 bytes）
struct ShmemLogHeader {
    DWORD magic;
    DWORD version;
    volatile LONG writeIndex;
    volatile LONG readIndex;
    DWORD _pad[4];  // 填充至 32 bytes
};

// 日誌條目結構
struct ShmemLogEntry {
    DWORD timestamp;
    WORD  level;    // 0=INFO, 1=WARN, 2=ERROR
    WORD  length;
    char  message[248];
};

// 讀取新的日誌條目
// 回傳: 讀到的日誌數量（0 表示沒有新日誌）
int NetHookShmem_ReadLogs(ShmemLogEntry* out, int max);

// 讀取 DLL 日誌檔案（Temp\JyNetHook.log）
// 回傳: 讀到的行數
int ReadDllLogFile(char* out, int maxLen);

// 巨集（避免與 Windows 巨集衝突）
#define NETHOOK_LOG_ENTRY_SZ  256
#define NETHOOK_LOG_BUF_COUNT 160

// ============================================================
// Python Packet Listener 座標共享記憶體
// ============================================================
#define COORDS_SHMEM_NAME     "Local\\RanBot_Coords"
#define COORDS_SHMEM_SIZE    128

// Python 座標資料結構
struct CoordsData {
    DWORD magic;       // 0xCAFEBABE
    DWORD version;     // 1
    DWORD valid;       // 1=有效
    float x;          // X 座標
    float z;          // Z 座標
    float y;          // Y 高度
    float heading;    // 朝向
    DWORD mapId;       // 地圖 ID
    DWORD timestamp;  // 時間戳
    DWORD _pad[24];   // 填充至 128 bytes
};

// ============================================================
// NetHook DLL 玩家座標共享記憶體
// 由 Jy.Dll/nethook.cpp 的 SRV_PLAYER_MOVE (0x2100) 更新
// 偏移計算: 128 + 500*32 + 128 + 78*16 = 17504
// ============================================================
#define NETHOOK_PLAYER_COORDS_OFFSET 17504

// NetHook 玩家座標結構
struct NetHookPlayerCoords {
    DWORD magic;       // 0xCAFEB00B
    DWORD version;     // 1
    DWORD valid;       // 1=有效
    float x;          // X 座標
    float z;          // Z 座標
    float y;          // Y 高度
    DWORD timestamp;  // 時間戳
    DWORD _pad[1];   // 填充至 32 bytes (4+4+4+4+4+4+4+4=32)
};

// 讀取 Python Packet Listener 座標
// 回傳: true=成功讀取
bool ReadCoordsFromPython(float* outX, float* outZ, float* outY);

// 檢查 Python 座標是否有效
bool IsPythonCoordsValid(void);

// 讀取 NetHook DLL 玩家座標（從同一個共享記憶體讀取）
// 回傳: true=成功讀取
bool ReadPlayerCoordsFromNetHook(float* outX, float* outZ, float* outY);

// 檢查 NetHook 座標是否有效
bool IsNetHookCoordsValid(void);
