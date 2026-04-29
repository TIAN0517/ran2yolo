// ============================================================================
// 封包偏移 - CE + MCP 驗證 (2026-04-25)
// ============================================================================
#ifndef PACKET_OFFSETS_H
#define PACKET_OFFSETS_H

// 封包函數地址
#define PACKET_SEND_ADDR       0x545F50  // send 封包主函數
#define PACKET_WRAPPER_ADDR     0x535F40  // 封包包裝器
#define PACKET_BUILD_ADDR       0x48B6D0  // 封包建構函數 (RVA)

// vtable 偏移
#define VTABLE_SEND_OFFSET      0x10      // vtable[4] = Send

// NetClient 偏移
#define NETCLIENT_VTABLE_OFFSET  0x2F98    // NetClient->vtable

// 封包結構偏移 (相對於封包緩衝區)
#define PACKET_HEADER_SIZE     4         // 標頭大小 (MsgID)
#define PACKET_MSGID_OFFSET    0x00      // MsgID 偏移
#define PACKET_DATA_OFFSET     0x04      // 數據開始偏移
#define PACKET_SIZE_OFFSET     0x0C      // 封包大小偏移

// 常見封包 MsgID
#define MSGID_NPC_TALK         0x34D6    // NPC 對話 (13526)
#define MSGID_NPC_BUY          0x34F7    // NPC 購買 (13559)
#define MSGID_NPC_SELL         0x34F8    // NPC 出售 (13560)
#define MSGID_ATTACK           0x3A22    // 攻擊 (14882)
#define MSGID_MOVE             0x3427    // 移動 (13351)
#define MSGID_PICKUP           0x1330    // 撿物 (4912)

// send 函數分析 (0x545F50):
//   push edi
//   push 03
//   mov ecx,esi
//   call [eax+10]        ; vtable 調用
//   ...
//   mov [ebp-0C],0000000C ; 封包大小 = 12
//   mov [ebp-08],000034D6 ; MsgID = 0x34D6 (NPC_TALK)
//   call Game.exe+18B6D0 ; 封包處理

// 封包發送流程:
//   1. NetClient = GameBase + 0xD11556
//   2. vtable = [NetClient + 0x2F98]
//   3. SendFunc = [vtable + 0x10]
//   4. call SendFunc(packet, size)

#endif // PACKET_OFFSETS_H