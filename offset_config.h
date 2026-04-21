#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// ============================================================
// Runtime offset configuration
// Load priority: offsets.dat (AES encrypted) > offsets.ini > defaults
// Falls back to compiled defaults in offsets.h when neither exists
// ============================================================
namespace OffsetConfig {

// Load offsets from file. Returns true if any external config was loaded.
// Priority: offsets.dat (base) -> offsets.ini (base) -> explicit path ->
// corrected_offsets.ini overlay -> defaults.
// Pass nullptr to auto-detect files next to exe.
    bool LoadFromFile(const char* iniPath = nullptr);

    // Whether offsets were loaded from external file
    bool IsLoadedFromFile();

    // Reload configuration (hot-reload support)
    bool Reload();

    // Get current load source: "offsets.dat" / "offsets.ini" / "defaults"
    const char* GetLoadSource();

    // Get config version (file timestamp or 0 for defaults)
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
    DWORD InvInvPtr();
    DWORD InvInvCount();
    DWORD InvItemStride();
    int   InvMaxSlots();
    DWORD InvItemId();
    DWORD InvItemCount();

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
