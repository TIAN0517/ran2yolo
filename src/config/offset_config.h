#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// ============================================================
// Runtime offset configuration
// Uses the single compiled offset table in offsets.h.
// External INI loading is intentionally disabled to avoid stale overrides.
// ============================================================
namespace OffsetConfig {

// Keep the legacy API name, but it only resets to built-in offsets.
// Returns false because no external file is loaded.
    bool LoadFromFile(const char* iniPath = nullptr);

    // Whether offsets were loaded from external file
    bool IsLoadedFromFile();

    // Reload configuration (hot-reload support)
    bool Reload();

    // Get current load source: "built-in"
    const char* GetLoadSource();

    // Get config version (0 for built-in defaults)
    DWORD GetConfigVersion();

    // --- Identity ---
    DWORD GLCharacterObj();
    DWORD GLGaeaServerObj();

    // --- Player RVA ---
    DWORD PlayerName();
    DWORD PlayerHP();
    DWORD PlayerMaxHP();
    DWORD PlayerMP();
    DWORD PlayerMaxMP();
    DWORD PlayerSP();
    DWORD PlayerMaxSP();
    DWORD PlayerGold();
    DWORD PlayerLevel();
    DWORD PlayerCombatPower();
    DWORD PlayerSTR();
    DWORD PlayerVIT();
    DWORD PlayerSPR();
    DWORD PlayerDEX();
    DWORD PlayerEND();
    DWORD PlayerPhysAtkMin();
    DWORD PlayerPhysAtkMax();
    DWORD PlayerSprAtkMin();
    DWORD PlayerSprAtkMax();
    DWORD PlayerEXP();
    DWORD PlayerEXPMax();
    DWORD QuickSlotArrowCount();
    DWORD QuickSlotTalismanCount();
    DWORD PlayerMapID();
    DWORD PlayerPosX();
    DWORD PlayerPosZ();
    DWORD PlayerPosY();

    // --- Target RVA ---
    DWORD TargetHasTarget();
    DWORD TargetID();
    DWORD TargetLockedState();
    DWORD TargetObjectLocked();  // 0=無鎖定, 1+=已鎖定目標

    // --- EntityPool (relative offsets, not RVA) ---
    DWORD EntityLandManPtr();
    DWORD EntityCROWList();
    DWORD EntityPCList();
    DWORD EntityPetList();
    int   EntityMaxCrows();

    // --- CROWClient internal offsets ---
    DWORD CrowNodeCrowPtr();
    DWORD CrowNodePrev();
    DWORD CrowNodeNext();
    DWORD CrowHP();
    DWORD CrowMaxHP();
    DWORD CrowSkinPtr();
    DWORD CrowPosX();     // ⚠️ 2026-04-17: 改用封包方案，返回 0
    DWORD CrowPosY();     // ⚠️ 2026-04-17: 改用封包方案，返回 0
    DWORD CrowPosZ();     // ⚠️ 2026-04-17: 改用封包方案，返回 0
    DWORD CrowServerID();
    DWORD CrowServerID2();
    DWORD CrowLandManPtr();
    DWORD CrowAIState();
    DWORD CrowDataType();
    DWORD CrowDataPtr();

    // --- PCClient internal offsets ---
    DWORD PCServerID();
    DWORD PCHP();
    DWORD PCMaxHP();
    DWORD PCPosX();
    DWORD PCPosY();
    DWORD PCPosZ();

    // --- Inventory ---
    DWORD GLCharClientPtr();  // GLCharClient 指針
    DWORD InvArrayOffset();   // 庫存陣列相對於 GLCharClient 的偏移
    int   InvMaxSlots();     // 最大槽數
    DWORD InvItemId();       // 物品ID偏移
    DWORD InvItemPtr();      // 物品指標偏移
    DWORD InvItemCount();     // 物品數量偏移
    DWORD InvSlotStride();    // 每槽大小

    // --- NPC ---
    DWORD NPC_ID();
    DWORD ShopItemCount();
    DWORD ShopItemID();
    DWORD ShopPrice();
    DWORD ShopName();

    // --- Network / Packets ---
    DWORD NetWS2_32_IAT();
    WORD  NetHeartbeat();
    WORD  NetUseItem();
    WORD  NetMove();
    WORD  NetMoveStop();
    WORD  NetPickupItem();
    WORD  NetPickupGold();
    WORD  NetNpcTalk();
    WORD  NetNpcBuy();
    WORD  NetNpcSell();
    WORD  NetNpcClose();
    WORD  NetAttackSubtype();
    WORD  NetSkillSubtype();
    WORD  NetSkillMsgID();

    // --- Game Time ---
    DWORD GameTimeHour();
    DWORD GameTimeMinute();

    // --- Functions (RVA - call with GetModuleHandle + RVA) ---
    DWORD FuncMsgProcess_RVA();     // GLCharMsg::MsgProcess
    DWORD FuncFrameMove_RVA();      // GLCharacter::FrameMove
    DWORD FuncNetClient_RVA();      // Get NetClient
    DWORD FuncPacketSend_RVA();     // Packet send
    DWORD FuncNPCBuySell_RVA();     // NPC buy/sell
    DWORD FuncPickupPacket_RVA();   // Pickup packet build
}
