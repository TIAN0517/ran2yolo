// ============================================================================
// 封包偏移 - CE + MCP + PCAP 驗證 (2026-04-30)
// ============================================================================
#ifndef PACKET_OFFSETS_H
#define PACKET_OFFSETS_H

// 封包函數地址
#define PACKET_SEND_ADDR       0x545F50  // send 封包主函數
#define PACKET_WRAPPER_ADDR    0x535F40  // 封包包裝器
#define PACKET_BUILD_ADDR      0x48B6D0  // 封包建構函數 (RVA)

// vtable 偏移
#define VTABLE_SEND_OFFSET     0x10      // vtable[4] = Send

// NetClient 偏移
#define NETCLIENT_VTABLE_OFFSET 0x2F98   // NetClient->vtable

// ============================================================================
// 封包 MsgID (PCAP 分析 2026-04-30)
// ============================================================================

// Client → Server
#define MSGID_ATTACK           0x3A22    // 攻擊 (14882) - 14 bytes
#define MSGID_MOVE             0x3424    // 移動 (13348) ✅ IDA
#define MSGID_NPC_TALK        0x34D2    // NPC 對話 (13522) ✅ IDA+CE
#define MSGID_NPC_BUY         0x34F7    // NPC 購買 (13559) - 32-35 bytes
#define MSGID_NPC_SELL        0x34F8    // NPC 出售 (13560) - 22-25 bytes
#define MSGID_PET_FEED        0x37BB    // 寵物餵食 (14267) - 22 bytes

// Server → Client
#define MSGID_POSITION_SYNC    0x27C0    // 位置同步 (10176) - 68 bytes
#define MSGID_CHAR_DATA       0x223A    // 角色資料 (8754) - 14-17 bytes
#define MSGID_MOVE_DATA       0x2434    // 移動資料 (9268) - 42-45 bytes
#define MSGID_ITEM_LIST       0x0B00    // 物品列表 (庫存)

// ============================================================================
// 封包結構
// ============================================================================

// NPC 對話封包 (0x34D6) - 12 bytes
// Offset 0x00: DWORD size = 12
// Offset 0x04: DWORD msgId = 0x34D6
// Offset 0x08: DWORD npcId

// NPC 購買封包 (0x34F7) - 32-35 bytes
// Offset 0x00: DWORD size
// Offset 0x04: DWORD msgId = 0x34F7
// Offset 0x08: CHAR[9] npcName
// Offset 0x11: WORD tabIndex (分頁)
// Offset 0x13: WORD slotIndex (格子)
// Offset 0x15: DWORD quantity (數量)
// Offset 0x19: DWORD flag (標記)
struct NpcBuyPacket {
    DWORD size;        // 0x00: 大小
    DWORD msgId;      // 0x04: 0x34F7
    char npcName[9];  // 0x08: NPC 名稱 (8 chars + null)
    WORD tabIndex;    // 0x11: 分頁 (Tab)
    WORD slotIndex;   // 0x13: 格子 (Slot)
    DWORD quantity;   // 0x15: 數量
    DWORD flag;       // 0x19: 標記
};

// NPC 出售封包 (0x34F8) - 22-25 bytes
// Offset 0x00: DWORD size
// Offset 0x04: DWORD msgId = 0x34F8
// Offset 0x08: CHAR[6] npcName
// Offset 0x0E: WORD slotIndex (背包格子)
// Offset 0x10: DWORD itemId
// Offset 0x14: DWORD quantity
struct NpcSellPacket {
    DWORD size;        // 0x00: 大小
    DWORD msgId;      // 0x04: 0x34F8
    char npcName[6];  // 0x08: NPC 名稱 (6 chars)
    WORD slotIndex;   // 0x0E: 背包格子 (0-77)
    DWORD itemId;     // 0x10: 物品ID
    DWORD quantity;   // 0x14: 數量
};

// ============================================================================
// 物品 ID
// ============================================================================
#define ITEM_WOOD_ARROW          51   // 木箭矢
#define ITEM_ENHANCED_WOOD_ARROW 103  // 強化木箭矢
#define ITEM_SILVER_ARROW        52   // 銀箭矢
#define ITEM_ENHANCED_SILVER_ARROW 104 // 強化銀箭矢

// ============================================================================
// NPC 商店 Tab/Slot 位置 (2026-04-30)
// ============================================================================
// Tab 0: 箭矢 (分頁1)
#define SHOP_TAB_ARROW          0
#define SHOP_SLOT_WOOD_ARROW     0    // 木箭矢 (第一個)
#define SHOP_SLOT_ENH_WOOD_ARROW 1    // 強化木箭矢
#define SHOP_SLOT_SILVER_ARROW   2    // 銀箭矢
#define SHOP_SLOT_ENH_SILVER_ARROW 3  // 強化銀箭矢

// Tab 1: 符咒 (分頁2)
#define SHOP_TAB_TALISMAN       1
#define SHOP_SLOT_TALISMAN_1    0    // 符咒1 (第一個)
#define SHOP_SLOT_TALISMAN_2    1    // 符咒2

// Tab 2: 消耗品 (分頁3) - HP/MP/SP
#define SHOP_TAB_CONSUMABLE     2
#define SHOP_SLOT_MANGO_ICE     0    // 芒果巨無霸冰淇淋
// HP強化 (第一排): Slot 0-4
#define SHOP_SLOT_HP_POTION_1   0    // HP強化1
#define SHOP_SLOT_HP_POTION_2   1    // HP強化2
#define SHOP_SLOT_HP_POTION_3   2    // HP強化3
#define SHOP_SLOT_HP_POTION_4   3    // HP強化4
#define SHOP_SLOT_HP_POTION_5   4    // HP強化5
// MP強化 (第二排左邊): Slot 5-7
#define SHOP_SLOT_MP_POTION_1   5    // MP強化1
#define SHOP_SLOT_MP_POTION_2   6    // MP強化2
#define SHOP_SLOT_MP_POTION_3   7    // MP強化3
// SP強化 (第二排): Slot 1-3
#define SHOP_SLOT_SP_POTION_1   1    // SP強化1
#define SHOP_SLOT_SP_POTION_2   2    // SP強化2
#define SHOP_SLOT_SP_POTION_3   3    // SP強化3

// ============================================================================
// 寵物餵食封包 (0x37BB) - 22 bytes
// 提起飼料 + 拖到寵物卡 + 右鍵餵食
// Offset 0x00: DWORD size = 22
// Offset 0x04: DWORD msgId = 0x37BB
// Offset 0x08: CHAR[6] petName (或 NPC 名)
// Offset 0x0E: DWORD slot (背包格子)
// Offset 0x12: DWORD quantity (數量)
struct PetFeedPacket {
    DWORD size;        // 0x00: 22
    DWORD msgId;      // 0x04: 0x37BB
    char name[6];    // 0x08: 名稱
    DWORD slot;       // 0x0E: 背包格子
    DWORD quantity;   // 0x12: 數量
};

// ============================================================================
// 網路拦截系統
// ============================================================================
// RanBot_NetHook.dll - 注入遊戲的 WS2_32 Hook
//   - Hook recv() 解析伺服器封包
//   - 通過 Local\RanBot_NetHook 共享記憶體與 JyTrainer 通信
//
// 共享記憶體格式:
//   Offset 0x00: ShmemHeader (128 bytes) - magic=0xDEADBEEF
//   Offset 0x80: ShmemEntity[500] (32 bytes each) - 實體池
//   Offset 0x28xx: ShmemInvHeader (128 bytes) - 庫存 header
//   Offset 0x29xx: ShmemInvItem[78] (16 bytes each) - 背包物品
//
// 實體結構 (32 bytes):
//   +0x00: DWORD id      - 實體 ID
//   +0x04: BYTE type     - 1=NPC, 2=Monster
//   +0x05: BYTE dead     - 死亡標記
//   +0x08: float x       - X 座標
//   +0x0C: float y       - Y 座標
//   +0x10: float z       - Z 座標
//   +0x14: DWORD hp      - HP
//   +0x18: DWORD maxHp   - 最大 HP
//   +0x1C: DWORD lastSeen - 上次見到時間

#endif // PACKET_OFFSETS_H
