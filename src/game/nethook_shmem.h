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
