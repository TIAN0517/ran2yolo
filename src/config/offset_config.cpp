#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include "offset_config.h"
#include "offsets.h"
#include "../core/bot_logic.h"
#include "../platform/win32_helpers.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ============================================================
// Static storage - initialized with compiled defaults from offsets.h
// ============================================================
static bool s_loadedFromFile = false;
static char s_loadSource[64] = "built-in";
static DWORD s_configVersion = 0;

// --- Identity ---
static DWORD s_GLCharacterObj  = Offsets::Identity::GLCharacter_Obj;
static DWORD s_GLGaeaServerObj = Offsets::Identity::GLGaeaServer_Obj;

// --- Player ---
static DWORD s_PlayerHP    = Offsets::Player::HP;
static DWORD s_PlayerMaxHP = Offsets::Player::MaxHP;
static DWORD s_PlayerMP    = Offsets::Player::MP;
static DWORD s_PlayerMaxMP = Offsets::Player::MaxMP;
static DWORD s_PlayerSP    = Offsets::Player::SP;
static DWORD s_PlayerMaxSP = Offsets::Player::MaxSP;
static DWORD s_PlayerGold  = Offsets::Player::Gold;
static DWORD s_PlayerLevel = Offsets::Player::Level;
static DWORD s_PlayerCombatPower = Offsets::Player::CombatPower;
static DWORD s_PlayerSTR = Offsets::Player::STR;
static DWORD s_PlayerVIT = Offsets::Player::VIT;
static DWORD s_PlayerSPR = Offsets::Player::SPR;
static DWORD s_PlayerDEX = Offsets::Player::DEX;
static DWORD s_PlayerEND = Offsets::Player::END;
static DWORD s_PlayerPhysAtkMin = Offsets::Player::PhysAtkMin;
static DWORD s_PlayerPhysAtkMax = Offsets::Player::PhysAtkMax;
static DWORD s_PlayerSprAtkMin = Offsets::Player::SprAtkMin;
static DWORD s_PlayerSprAtkMax = Offsets::Player::SprAtkMax;
static DWORD s_PlayerEXP = Offsets::Player::EXP;
static DWORD s_PlayerEXPMax = Offsets::Player::EXPMax;
static DWORD s_QuickSlotArrowCount = Offsets::QuickSlot::ArrowCount;
static DWORD s_QuickSlotTalismanCount = Offsets::QuickSlot::TalismanCount;
static DWORD s_PlayerName  = Offsets::Player::Name;
static DWORD s_PlayerMapID = Offsets::Player::MapID;
static DWORD s_PlayerPosX  = Offsets::Player::PosX;
static DWORD s_PlayerPosZ  = Offsets::Player::PosZ;
static DWORD s_PlayerPosY  = Offsets::Player::PosY;

// --- Target ---
static DWORD s_TargetHasTarget   = Offsets::Target::HasTarget;
static DWORD s_TargetID          = Offsets::Target::ID;
static DWORD s_TargetLockedState = Offsets::Target::LockedState;
static DWORD s_TargetObjectLocked = Offsets::Target::ObjectLocked;  // 0=無鎖定, 1+=已鎖定

// --- EntityPool ---
static DWORD s_EntityLandManPtr = 0x24;
static DWORD s_EntityCROWList   = Offsets::EntityPool::LandMan_CROWList;
static DWORD s_EntityPCList     = Offsets::EntityPool::LandMan_PCList;
static DWORD s_EntityPetList    = Offsets::EntityPool::LandMan_PetList;
static int   s_EntityMaxCrows   = Offsets::EntityPool::MAX_CROWS;

// --- CROWClient ---
static DWORD s_CrowNodeCrowPtr = Offsets::EntityPool::Node_CrowPtr;
static DWORD s_CrowNodePrev    = Offsets::EntityPool::Node_Prev;
static DWORD s_CrowNodeNext    = Offsets::EntityPool::Node_Next;
static DWORD s_CrowHP          = Offsets::EntityPool::Crow::HP;
static DWORD s_CrowMaxHP       = Offsets::EntityPool::Crow::MaxHP;
static DWORD s_CrowSkinPtr     = Offsets::EntityPool::Crow::SkinPtr;
static DWORD s_CrowPosX        = Offsets::EntityPool::Crow::PosX;   // ✅ INI覆寫
static DWORD s_CrowPosY        = Offsets::EntityPool::Crow::PosY;
static DWORD s_CrowPosZ        = Offsets::EntityPool::Crow::PosZ;
static DWORD s_CrowServerID    = Offsets::EntityPool::Crow::ServerID;
static DWORD s_CrowServerID2   = Offsets::EntityPool::Crow::ServerID2;
static DWORD s_CrowLandManPtr  = Offsets::EntityPool::Crow::LandManPtr;
static DWORD s_CrowAIState     = Offsets::EntityPool::Crow::AIState;
static DWORD s_CrowDataType    = Offsets::EntityPool::Crow::DataType;
static DWORD s_CrowDataPtr     = Offsets::EntityPool::Crow::CrowDataPtr;

// --- PCClient ---
static DWORD s_PCServerID = Offsets::EntityPool::PC::ServerID;
static DWORD s_PCHP       = Offsets::EntityPool::PC::HP;
static DWORD s_PCMaxHP    = Offsets::EntityPool::PC::MaxHP;
static DWORD s_PCPosX     = Offsets::EntityPool::PC::PosX;   // ✅ INI覆寫（與CROW同偏移）
static DWORD s_PCPosY     = Offsets::EntityPool::PC::PosY;
static DWORD s_PCPosZ     = Offsets::EntityPool::PC::PosZ;

// --- Inventory ---
static DWORD s_GLCharClientPtr = Offsets::Inventory::GLCharClient_Ptr;  // GLCharClient 指針
static DWORD s_InvArrayOffset   = Offsets::Inventory::InvArray_Offset;  // 庫存陣列偏移
static DWORD s_InvItemId        = Offsets::Inventory::ItemId_off;        // 物品ID偏移
static DWORD s_InvItemPtr       = Offsets::Inventory::ItemPtr_off;       // 物品指標偏移
static DWORD s_InvItemCount     = Offsets::Inventory::ItemCount_off;   // 物品數量偏移
static DWORD s_InvSlotStride    = Offsets::Inventory::SlotStride;       // 每槽大小
static int   s_InvMaxSlots      = Offsets::Inventory::MaxSlots;         // 最大槽數

// --- Network ---
static DWORD s_NetWS2_32_IAT    = Offsets::IAT::WS2_32;
static WORD  s_NetHeartbeat     = (WORD)Offsets::Packets::HEARTBEAT;
static WORD  s_NetUseItem       = (WORD)Offsets::Packets::USE_ITEM;
static WORD  s_NetMove          = (WORD)Offsets::Packets::MOVE;
static WORD  s_NetMoveStop      = (WORD)Offsets::Packets::MOVE_STOP;
static WORD  s_NetPickupItem    = (WORD)Offsets::Packets::PICKUP_ITEM;
static WORD  s_NetPickupGold    = (WORD)Offsets::Packets::PICKUP_GOLD;
static WORD  s_NetNpcTalk       = (WORD)Offsets::Packets::NPC_TALK;
static WORD  s_NetNpcBuy        = (WORD)Offsets::Packets::NPC_BUY;
static WORD  s_NetNpcSell       = (WORD)Offsets::Packets::NPC_SELL;
static WORD  s_NetNpcClose      = (WORD)Offsets::Packets::NPC_CLOSE;
static WORD  s_NetAttackSubtype = (WORD)Offsets::Packets::ATTACK_SUBTYPE;
static WORD  s_NetSkillSubtype  = (WORD)Offsets::Packets::SKILL_SUBTYPE;
static WORD  s_NetSkillMsgID    = (WORD)Offsets::Packets::SKILL_MSGID;

// --- Game Time ---
static DWORD s_GameTimeHour   = Offsets::GameTime::Hour;
static DWORD s_GameTimeMinute = Offsets::GameTime::Minute;

// --- NPC ---
static DWORD s_NPC_ID         = Offsets::NPC::NPC_ID;
static DWORD s_ShopItemCount  = Offsets::NPC::Shop_ItemCount;
static DWORD s_ShopItemID     = Offsets::NPC::Shop_ItemID;
static DWORD s_ShopPrice      = Offsets::NPC::Shop_Price;
static DWORD s_ShopName       = Offsets::NPC::Shop_Name;

// --- Functions (RVA) ---
static DWORD s_FuncMsgProcess_RVA   = Offsets::Functions::MsgProcess_RVA;
static DWORD s_FuncFrameMove_RVA    = Offsets::Functions::FrameMove_RVA;
static DWORD s_FuncNetClient_RVA    = Offsets::Functions::NetClient_RVA;
static DWORD s_FuncPacketSend_RVA   = Offsets::Functions::PacketSend_RVA;
static DWORD s_FuncNPCBuySell_RVA   = Offsets::Functions::NPCBuySell_RVA;
static DWORD s_FuncPickupPacket_RVA = Offsets::Functions::PickupPacket_RVA;

// ============================================================
// In-memory INI parser (no temp files)
// ============================================================

// Skip whitespace
static const char* SkipWS(const char* p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

// Parse hex value from in-memory INI content
static DWORD ParseIniHexValue(const char* iniContent, const char* section, const char* key, DWORD defaultVal) {
    if (!iniContent || !section || !key) return defaultVal;

    // Find [section]
    char sectionTag[128];
    _snprintf(sectionTag, sizeof(sectionTag), "[%s]", section);
    sectionTag[sizeof(sectionTag) - 1] = '\0';

    const char* secStart = iniContent;
    bool found = false;
    while (*secStart) {
        const char* line = SkipWS(secStart);
        if (_strnicmp(line, sectionTag, strlen(sectionTag)) == 0) {
            // Move past the section header line
            secStart = strchr(line, '\n');
            if (secStart) secStart++;
            found = true;
            break;
        }
        // Skip to next line
        const char* nl = strchr(secStart, '\n');
        if (!nl) break;
        secStart = nl + 1;
    }
    if (!found) return defaultVal;

    // Search for key=value within this section
    size_t keyLen = strlen(key);
    const char* p = secStart;
    while (*p) {
        const char* line = SkipWS(p);

        // Stop at next section
        if (*line == '[') break;

        // Skip comments and empty lines
        if (*line == ';' || *line == '#' || *line == '\r' || *line == '\n') {
            const char* nl = strchr(p, '\n');
            if (!nl) break;
            p = nl + 1;
            continue;
        }

        // Check if this line starts with our key
        if (_strnicmp(line, key, keyLen) == 0) {
            const char* eq = SkipWS(line + keyLen);
            if (*eq == '=') {
                eq = SkipWS(eq + 1);
                // Extract value (until newline or end)
                char valBuf[64] = {0};
                int vi = 0;
                while (*eq && *eq != '\r' && *eq != '\n' && vi < 63) {
                    valBuf[vi++] = *eq++;
                }
                valBuf[vi] = '\0';
                // Trim trailing whitespace
                while (vi > 0 && (valBuf[vi-1] == ' ' || valBuf[vi-1] == '\t')) {
                    valBuf[--vi] = '\0';
                }
                if (valBuf[0] == '\0') return defaultVal;
                return (DWORD)strtoul(valBuf, NULL, 16);
            }
        }

        // Next line
        const char* nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }

    return defaultVal;
}

static WORD ParseIniHexWord(const char* iniContent, const char* section, const char* key, WORD defaultVal) {
    return (WORD)ParseIniHexValue(iniContent, section, key, (DWORD)defaultVal);
}

static const char* GetBaseNameA(const char* path) {
    if (!path || !path[0]) return "";
    const char* slash1 = strrchr(path, '\\');
    const char* slash2 = strrchr(path, '/');
    const char* base = path;
    if (slash1 && slash1 + 1 > base) base = slash1 + 1;
    if (slash2 && slash2 + 1 > base) base = slash2 + 1;
    return base;
}

static void SetLoadSourceFromPath(const char* path) {
    const char* base = GetBaseNameA(path);
    if (!base || !base[0]) {
        strcpy_s(s_loadSource, sizeof(s_loadSource), "built-in");
        return;
    }
    strcpy_s(s_loadSource, sizeof(s_loadSource), base);
}

static DWORD ParseIniHexValueAlias(const char* iniContent, const char* section,
    const char* key1, const char* key2, DWORD defaultVal) {
    DWORD value = ParseIniHexValue(iniContent, section, key1, defaultVal);
    if (key2 && key2[0]) {
        DWORD alt = ParseIniHexValue(iniContent, section, key2, value);
        if (alt != value) value = alt;
    }
    return value;
}

static DWORD ParseIniHexValueSectionAlias(const char* iniContent,
    const char* section1, const char* key1,
    const char* section2, const char* key2,
    DWORD defaultVal) {
    DWORD value = ParseIniHexValue(iniContent, section1, key1, defaultVal);
    if (section2 && section2[0] && key2 && key2[0]) {
        DWORD alt = ParseIniHexValue(iniContent, section2, key2, value);
        if (alt != value) value = alt;
    }
    return value;
}

// ============================================================
// Apply offsets from INI content string
// ============================================================
static int ApplyOffsetsFromIniContent(const char* ini) {
    int count = 0;

    // --- Identity ---
    s_GLCharacterObj  = ParseIniHexValue(ini, "Identity", "GLCharacter_Obj",  s_GLCharacterObj);  count++;
    s_GLGaeaServerObj = ParseIniHexValue(ini, "Identity", "GLGaeaServer_Obj", s_GLGaeaServerObj); count++;

    // --- Player ---
    s_PlayerHP    = ParseIniHexValue(ini, "Player", "HP",    s_PlayerHP);    count++;
    s_PlayerMaxHP = ParseIniHexValue(ini, "Player", "MaxHP", s_PlayerMaxHP); count++;
    s_PlayerMP    = ParseIniHexValue(ini, "Player", "MP",    s_PlayerMP);    count++;
    s_PlayerMaxMP = ParseIniHexValue(ini, "Player", "MaxMP", s_PlayerMaxMP); count++;
    s_PlayerSP    = ParseIniHexValue(ini, "Player", "SP",    s_PlayerSP);    count++;
    s_PlayerMaxSP = ParseIniHexValue(ini, "Player", "MaxSP", s_PlayerMaxSP); count++;
    s_PlayerGold  = ParseIniHexValue(ini, "Player", "Gold",  s_PlayerGold);  count++;
    s_PlayerLevel = ParseIniHexValue(ini, "Player", "Level", s_PlayerLevel); count++;
    s_PlayerCombatPower = ParseIniHexValue(ini, "Player", "CombatPower", s_PlayerCombatPower); count++;
    s_PlayerSTR = ParseIniHexValue(ini, "Player", "STR", s_PlayerSTR); count++;
    s_PlayerVIT = ParseIniHexValue(ini, "Player", "VIT", s_PlayerVIT); count++;
    s_PlayerSPR = ParseIniHexValue(ini, "Player", "SPR", s_PlayerSPR); count++;
    s_PlayerDEX = ParseIniHexValue(ini, "Player", "DEX", s_PlayerDEX); count++;
    s_PlayerEND = ParseIniHexValue(ini, "Player", "END", s_PlayerEND); count++;
    s_PlayerPhysAtkMin = ParseIniHexValue(ini, "Player", "PhysAtkMin", s_PlayerPhysAtkMin); count++;
    s_PlayerPhysAtkMax = ParseIniHexValue(ini, "Player", "PhysAtkMax", s_PlayerPhysAtkMax); count++;
    s_PlayerSprAtkMin = ParseIniHexValue(ini, "Player", "SprAtkMin", s_PlayerSprAtkMin); count++;
    s_PlayerSprAtkMax = ParseIniHexValue(ini, "Player", "SprAtkMax", s_PlayerSprAtkMax); count++;
    s_PlayerEXP = ParseIniHexValue(ini, "Player", "EXP", s_PlayerEXP); count++;
    s_PlayerEXPMax = ParseIniHexValue(ini, "Player", "EXPMax", s_PlayerEXPMax); count++;
    s_QuickSlotArrowCount = ParseIniHexValue(ini, "QuickSlot", "ArrowCount", s_QuickSlotArrowCount); count++;
    s_QuickSlotTalismanCount = ParseIniHexValue(ini, "QuickSlot", "TalismanCount", s_QuickSlotTalismanCount); count++;
    s_PlayerMapID = ParseIniHexValueSectionAlias(ini, "Player", "MapID", "Position", "MapID", s_PlayerMapID); count++;
    s_PlayerPosX  = ParseIniHexValueSectionAlias(ini, "Player", "PosX",  "Position", "PosX",  s_PlayerPosX);  count++;
    s_PlayerPosZ  = ParseIniHexValueSectionAlias(ini, "Player", "PosZ",  "Position", "PosZ",  s_PlayerPosZ);  count++;
    s_PlayerPosY  = ParseIniHexValueSectionAlias(ini, "Player", "PosY",  "Position", "PosY",  s_PlayerPosY);  count++;

    // --- Target ---
    s_TargetHasTarget   = ParseIniHexValue(ini, "Target", "HasTarget",   s_TargetHasTarget);   count++;
    s_TargetID          = ParseIniHexValue(ini, "Target", "ID",          s_TargetID);          count++;
    s_TargetLockedState = ParseIniHexValue(ini, "Target", "LockedState", s_TargetLockedState); count++;
    s_TargetObjectLocked = ParseIniHexValue(ini, "Target", "ObjectLocked", s_TargetObjectLocked); count++;

    // --- EntityPool ---
    s_EntityLandManPtr = ParseIniHexValueAlias(ini, "EntityPool", "LandManPtr_Offset", "LandManOffset", s_EntityLandManPtr); count++;
    s_EntityCROWList   = ParseIniHexValueAlias(ini, "EntityPool", "CROWList_Offset",   "CROWListOffset", s_EntityCROWList);   count++;
    s_EntityPCList     = ParseIniHexValueAlias(ini, "EntityPool", "PCList_Offset",     "PCListOffset", s_EntityPCList);     count++;
    s_EntityPetList    = ParseIniHexValueAlias(ini, "EntityPool", "PetList_Offset",    "PetListOffset", s_EntityPetList);    count++;
    s_EntityMaxCrows   = (int)ParseIniHexValueAlias(ini, "EntityPool", "MAX_CROWS",    "MaxCrows", (DWORD)s_EntityMaxCrows); count++;

    // --- CROWClient ---
    s_CrowNodeCrowPtr = ParseIniHexValue(ini, "CROWClient", "CrowPtr",    s_CrowNodeCrowPtr); count++;
    s_CrowNodePrev    = ParseIniHexValue(ini, "CROWClient", "Prev",       s_CrowNodePrev);    count++;
    s_CrowNodeNext    = ParseIniHexValue(ini, "CROWClient", "Next",       s_CrowNodeNext);    count++;
    s_CrowHP          = ParseIniHexValue(ini, "CROWClient", "HP",         s_CrowHP);          count++;
    s_CrowMaxHP       = ParseIniHexValue(ini, "CROWClient", "MaxHP",      s_CrowMaxHP);       count++;
    s_CrowSkinPtr     = ParseIniHexValue(ini, "CROWClient", "SkinPtr",    s_CrowSkinPtr);     count++;
    s_CrowPosX        = ParseIniHexValue(ini, "CROWClient", "PosX",       s_CrowPosX);        count++;
    s_CrowPosY        = ParseIniHexValue(ini, "CROWClient", "PosY",       s_CrowPosY);        count++;
    s_CrowPosZ        = ParseIniHexValue(ini, "CROWClient", "PosZ",       s_CrowPosZ);        count++;
    s_CrowServerID    = ParseIniHexValue(ini, "CROWClient", "ServerID",   s_CrowServerID);    count++;
    s_CrowServerID2   = ParseIniHexValue(ini, "CROWClient", "ServerID2",  s_CrowServerID2);   count++;
    s_CrowLandManPtr  = ParseIniHexValue(ini, "CROWClient", "LandManPtr", s_CrowLandManPtr);  count++;
    s_CrowAIState     = ParseIniHexValue(ini, "CROWClient", "AIState",    s_CrowAIState);     count++;
    s_CrowDataType    = ParseIniHexValue(ini, "CROWClient", "DataType",   s_CrowDataType);    count++;
    s_CrowDataPtr     = ParseIniHexValue(ini, "CROWClient", "CrowDataPtr",s_CrowDataPtr);     count++;

    // --- PCClient ---
    s_PCServerID = ParseIniHexValue(ini, "PCClient", "ServerID", s_PCServerID); count++;
    s_PCHP       = ParseIniHexValue(ini, "PCClient", "HP",       s_PCHP);       count++;
    s_PCMaxHP    = ParseIniHexValue(ini, "PCClient", "MaxHP",    s_PCMaxHP);    count++;
    s_PCPosX     = ParseIniHexValue(ini, "PCClient", "PosX",     s_PCPosX);     count++;
    s_PCPosY     = ParseIniHexValue(ini, "PCClient", "PosY",     s_PCPosY);     count++;
    s_PCPosZ     = ParseIniHexValue(ini, "PCClient", "PosZ",     s_PCPosZ);     count++;

    // --- Inventory ---
    s_GLCharClientPtr = ParseIniHexValue(ini, "Inventory", "GLCharClientPtr", s_GLCharClientPtr); count++;
    s_InvArrayOffset = ParseIniHexValue(ini, "Inventory", "InvArrayOffset", s_InvArrayOffset); count++;
    s_InvItemId      = ParseIniHexValue(ini, "Inventory", "ItemId",      s_InvItemId);      count++;
    s_InvItemPtr     = ParseIniHexValue(ini, "Inventory", "ItemPtr",     s_InvItemPtr);     count++;
    s_InvItemCount   = ParseIniHexValue(ini, "Inventory", "ItemCount",   s_InvItemCount);   count++;
    s_InvSlotStride  = ParseIniHexValue(ini, "Inventory", "SlotStride",  s_InvSlotStride);  count++;
    s_InvMaxSlots    = (int)ParseIniHexValueAlias(ini, "Inventory", "MaxSlots", "InvMaxSlots", (DWORD)s_InvMaxSlots); count++;
    if (s_InvMaxSlots <= 0 || s_InvMaxSlots > 200) {
        s_InvMaxSlots = Offsets::Inventory::MaxSlots;
    }

    // --- Network ---
    s_NetWS2_32_IAT    = ParseIniHexValue(ini, "Network", "WS2_32_IAT",    s_NetWS2_32_IAT);    count++;
    s_NetHeartbeat     = ParseIniHexWord(ini, "Network", "HEARTBEAT",      s_NetHeartbeat);     count++;
    s_NetUseItem       = ParseIniHexWord(ini, "Network", "USE_ITEM",       s_NetUseItem);       count++;
    s_NetMove          = ParseIniHexWord(ini, "Network", "MOVE",           s_NetMove);          count++;
    s_NetMoveStop      = ParseIniHexWord(ini, "Network", "MOVE_STOP",      s_NetMoveStop);      count++;
    s_NetPickupItem    = ParseIniHexWord(ini, "Network", "PICKUP_ITEM",    s_NetPickupItem);    count++;
    s_NetPickupGold    = ParseIniHexWord(ini, "Network", "PICKUP_GOLD",    s_NetPickupGold);    count++;
    s_NetNpcTalk       = ParseIniHexWord(ini, "Network", "NPC_TALK",       s_NetNpcTalk);       count++;
    s_NetNpcBuy        = ParseIniHexWord(ini, "Network", "NPC_BUY",        s_NetNpcBuy);        count++;
    s_NetNpcSell       = ParseIniHexWord(ini, "Network", "NPC_SELL",       s_NetNpcSell);       count++;
    s_NetNpcClose      = ParseIniHexWord(ini, "Network", "NPC_CLOSE",      s_NetNpcClose);      count++;
    s_NetAttackSubtype = ParseIniHexWord(ini, "Network", "ATTACK_SUBTYPE", s_NetAttackSubtype); count++;
    s_NetSkillSubtype  = ParseIniHexWord(ini, "Network", "SKILL_SUBTYPE",  s_NetSkillSubtype);  count++;
    s_NetSkillMsgID    = ParseIniHexWord(ini, "Network", "SKILL_MSGID",    s_NetSkillMsgID);    count++;

    return count;
}

static void ResetToDefaults() {
    s_loadedFromFile = false;
    s_GLCharacterObj  = Offsets::Identity::GLCharacter_Obj;
    s_GLGaeaServerObj = Offsets::Identity::GLGaeaServer_Obj;
    s_PlayerHP = Offsets::Player::HP;
    s_PlayerMaxHP = Offsets::Player::MaxHP;
    s_PlayerMP = Offsets::Player::MP;
    s_PlayerMaxMP = Offsets::Player::MaxMP;
    s_PlayerSP = Offsets::Player::SP;
    s_PlayerMaxSP = Offsets::Player::MaxSP;
    s_PlayerGold = Offsets::Player::Gold;
    s_PlayerLevel = Offsets::Player::Level;
    s_PlayerCombatPower = Offsets::Player::CombatPower;
    s_PlayerSTR = Offsets::Player::STR;
    s_PlayerVIT = Offsets::Player::VIT;
    s_PlayerSPR = Offsets::Player::SPR;
    s_PlayerDEX = Offsets::Player::DEX;
    s_PlayerEND = Offsets::Player::END;
    s_PlayerPhysAtkMin = Offsets::Player::PhysAtkMin;
    s_PlayerPhysAtkMax = Offsets::Player::PhysAtkMax;
    s_PlayerSprAtkMin = Offsets::Player::SprAtkMin;
    s_PlayerSprAtkMax = Offsets::Player::SprAtkMax;
    s_PlayerEXP = Offsets::Player::EXP;
    s_PlayerEXPMax = Offsets::Player::EXPMax;
    s_QuickSlotArrowCount = Offsets::QuickSlot::ArrowCount;
    s_QuickSlotTalismanCount = Offsets::QuickSlot::TalismanCount;
    s_PlayerName = Offsets::Player::Name;
    s_PlayerMapID = Offsets::Player::MapID;
    s_PlayerPosX = Offsets::Player::PosX;
    s_PlayerPosZ = Offsets::Player::PosZ;
    s_PlayerPosY = Offsets::Player::PosY;
    s_TargetHasTarget = Offsets::Target::HasTarget;
    s_TargetID = Offsets::Target::ID;
    s_TargetLockedState = Offsets::Target::LockedState;
    s_TargetObjectLocked = Offsets::Target::ObjectLocked;
    s_EntityLandManPtr = 0x24;  // GLChar+0x24 → GLLandManClient
    s_EntityCROWList = Offsets::EntityPool::LandMan_CROWList;
    s_EntityPCList = Offsets::EntityPool::LandMan_PCList;
    s_EntityPetList = Offsets::EntityPool::LandMan_PetList;
    s_EntityMaxCrows = Offsets::EntityPool::MAX_CROWS;
    s_CrowNodeCrowPtr = Offsets::EntityPool::Node_CrowPtr;
    s_CrowNodePrev = Offsets::EntityPool::Node_Prev;
    s_CrowNodeNext = Offsets::EntityPool::Node_Next;
    s_CrowHP = Offsets::EntityPool::Crow::HP;
    s_CrowMaxHP = Offsets::EntityPool::Crow::MaxHP;
    s_CrowSkinPtr = Offsets::EntityPool::Crow::SkinPtr;
    s_CrowPosX = Offsets::EntityPool::Crow::PosX;
    s_CrowPosY = Offsets::EntityPool::Crow::PosY;
    s_CrowPosZ = Offsets::EntityPool::Crow::PosZ;
    s_CrowServerID = Offsets::EntityPool::Crow::ServerID;
    s_CrowServerID2 = Offsets::EntityPool::Crow::ServerID2;
    s_CrowLandManPtr = Offsets::EntityPool::Crow::LandManPtr;
    s_CrowAIState = Offsets::EntityPool::Crow::AIState;
    s_CrowDataType = Offsets::EntityPool::Crow::DataType;
    s_CrowDataPtr = Offsets::EntityPool::Crow::CrowDataPtr;
    s_PCServerID = Offsets::EntityPool::PC::ServerID;
    s_PCHP = Offsets::EntityPool::PC::HP;
    s_PCMaxHP = Offsets::EntityPool::PC::MaxHP;
    s_PCPosX = Offsets::EntityPool::PC::PosX;
    s_PCPosY = Offsets::EntityPool::PC::PosY;
    s_PCPosZ = Offsets::EntityPool::PC::PosZ;
    s_InvArrayOffset = Offsets::Inventory::InvArray_Offset;
    s_InvMaxSlots = Offsets::Inventory::MaxSlots;
    s_GLCharClientPtr = Offsets::Inventory::GLCharClient_Ptr;  // ✅ 補加缺失
    s_InvItemId = Offsets::Inventory::ItemId_off;
    s_InvItemPtr = Offsets::Inventory::ItemPtr_off;
    s_InvItemCount = Offsets::Inventory::ItemCount_off;
    s_InvSlotStride = Offsets::Inventory::SlotStride;
    s_NetWS2_32_IAT = Offsets::IAT::WS2_32;
    s_NetHeartbeat = (WORD)Offsets::Packets::HEARTBEAT;
    s_NetUseItem = (WORD)Offsets::Packets::USE_ITEM;
    s_NetMove = (WORD)Offsets::Packets::MOVE;
    s_NetMoveStop = (WORD)Offsets::Packets::MOVE_STOP;
    s_NetPickupItem = (WORD)Offsets::Packets::PICKUP_ITEM;
    s_NetPickupGold = (WORD)Offsets::Packets::PICKUP_GOLD;
    s_NetNpcTalk = (WORD)Offsets::Packets::NPC_TALK;
    s_NetNpcBuy = (WORD)Offsets::Packets::NPC_BUY;
    s_NetNpcSell = (WORD)Offsets::Packets::NPC_SELL;
    s_NetNpcClose = (WORD)Offsets::Packets::NPC_CLOSE;
    s_NetAttackSubtype = (WORD)Offsets::Packets::ATTACK_SUBTYPE;
    s_NetSkillSubtype = (WORD)Offsets::Packets::SKILL_SUBTYPE;
    s_NetSkillMsgID = (WORD)Offsets::Packets::SKILL_MSGID;
    s_GameTimeHour = Offsets::GameTime::Hour;
    s_GameTimeMinute = Offsets::GameTime::Minute;
    s_FuncMsgProcess_RVA = Offsets::Functions::MsgProcess_RVA;
    s_FuncFrameMove_RVA = Offsets::Functions::FrameMove_RVA;
    s_FuncNetClient_RVA = Offsets::Functions::NetClient_RVA;
    s_FuncPacketSend_RVA = Offsets::Functions::PacketSend_RVA;
    s_FuncNPCBuySell_RVA = Offsets::Functions::NPCBuySell_RVA;
    s_FuncPickupPacket_RVA = Offsets::Functions::PickupPacket_RVA;
    strcpy_s(s_loadSource, sizeof(s_loadSource), "built-in");
    s_configVersion = 0;
}

// ============================================================
// LoadFromFile - legacy API; external INI is disabled.
// ============================================================
bool OffsetConfig::LoadFromFile(const char* iniPath) {
    (void)iniPath;
    ResetToDefaults();
    printf("[OffsetConfig] external offset files disabled, using built-in offsets\n");
    return false;
}

bool OffsetConfig::IsLoadedFromFile() { return s_loadedFromFile; }

bool OffsetConfig::Reload() {
    ResetToDefaults();
    printf("[OffsetConfig] Reloaded built-in offsets\n");
    return false;
}

// ============================================================
const char* OffsetConfig::GetLoadSource() { return s_loadSource; }
DWORD OffsetConfig::GetConfigVersion() { return s_configVersion; }

// ============================================================
// Getters
// ============================================================
DWORD OffsetConfig::GLCharacterObj()    { return s_GLCharacterObj; }
DWORD OffsetConfig::GLGaeaServerObj()   { return s_GLGaeaServerObj; }

DWORD OffsetConfig::PlayerHP()    { return s_PlayerHP; }
DWORD OffsetConfig::PlayerMaxHP() { return s_PlayerMaxHP; }
DWORD OffsetConfig::PlayerMP()    { return s_PlayerMP; }
DWORD OffsetConfig::PlayerMaxMP() { return s_PlayerMaxMP; }
DWORD OffsetConfig::PlayerSP()    { return s_PlayerSP; }
DWORD OffsetConfig::PlayerMaxSP() { return s_PlayerMaxSP; }
DWORD OffsetConfig::PlayerGold()  { return s_PlayerGold; }
DWORD OffsetConfig::PlayerLevel() { return s_PlayerLevel; }
DWORD OffsetConfig::PlayerCombatPower() { return s_PlayerCombatPower; }
DWORD OffsetConfig::PlayerSTR() { return s_PlayerSTR; }
DWORD OffsetConfig::PlayerVIT() { return s_PlayerVIT; }
DWORD OffsetConfig::PlayerSPR() { return s_PlayerSPR; }
DWORD OffsetConfig::PlayerDEX() { return s_PlayerDEX; }
DWORD OffsetConfig::PlayerEND() { return s_PlayerEND; }
DWORD OffsetConfig::PlayerPhysAtkMin() { return s_PlayerPhysAtkMin; }
DWORD OffsetConfig::PlayerPhysAtkMax() { return s_PlayerPhysAtkMax; }
DWORD OffsetConfig::PlayerSprAtkMin() { return s_PlayerSprAtkMin; }
DWORD OffsetConfig::PlayerSprAtkMax() { return s_PlayerSprAtkMax; }
DWORD OffsetConfig::PlayerEXP() { return s_PlayerEXP; }
DWORD OffsetConfig::PlayerEXPMax() { return s_PlayerEXPMax; }
DWORD OffsetConfig::QuickSlotArrowCount() { return s_QuickSlotArrowCount; }
DWORD OffsetConfig::QuickSlotTalismanCount() { return s_QuickSlotTalismanCount; }
DWORD OffsetConfig::PlayerName()  { return s_PlayerName; }
DWORD OffsetConfig::PlayerMapID() { return s_PlayerMapID; }
DWORD OffsetConfig::PlayerPosX()  { return s_PlayerPosX; }
DWORD OffsetConfig::PlayerPosZ()  { return s_PlayerPosZ; }
DWORD OffsetConfig::PlayerPosY()  { return s_PlayerPosY; }

DWORD OffsetConfig::TargetHasTarget()   { return s_TargetHasTarget; }
DWORD OffsetConfig::TargetID()          { return s_TargetID; }
DWORD OffsetConfig::TargetLockedState() { return s_TargetLockedState; }
DWORD OffsetConfig::TargetObjectLocked() { return s_TargetObjectLocked; }

DWORD OffsetConfig::EntityLandManPtr() { return s_EntityLandManPtr; }
DWORD OffsetConfig::EntityCROWList()   { return s_EntityCROWList; }
DWORD OffsetConfig::EntityPCList()     { return s_EntityPCList; }
DWORD OffsetConfig::EntityPetList()    { return s_EntityPetList; }
int   OffsetConfig::EntityMaxCrows()   { return s_EntityMaxCrows; }

DWORD OffsetConfig::CrowNodeCrowPtr() { return s_CrowNodeCrowPtr; }
DWORD OffsetConfig::CrowNodePrev()    { return s_CrowNodePrev; }
DWORD OffsetConfig::CrowNodeNext()    { return s_CrowNodeNext; }
DWORD OffsetConfig::CrowHP()          { return s_CrowHP; }
DWORD OffsetConfig::CrowMaxHP()       { return s_CrowMaxHP; }
DWORD OffsetConfig::CrowSkinPtr()     { return s_CrowSkinPtr; }
DWORD OffsetConfig::CrowPosX()        { return s_CrowPosX; }
DWORD OffsetConfig::CrowPosY()        { return s_CrowPosY; }
DWORD OffsetConfig::CrowPosZ()        { return s_CrowPosZ; }
DWORD OffsetConfig::CrowServerID()    { return s_CrowServerID; }
DWORD OffsetConfig::CrowServerID2()   { return s_CrowServerID2; }
DWORD OffsetConfig::CrowLandManPtr()  { return s_CrowLandManPtr; }
DWORD OffsetConfig::CrowAIState()     { return s_CrowAIState; }
DWORD OffsetConfig::CrowDataType()    { return s_CrowDataType; }
DWORD OffsetConfig::CrowDataPtr()     { return s_CrowDataPtr; }

DWORD OffsetConfig::PCServerID() { return s_PCServerID; }
DWORD OffsetConfig::PCHP()       { return s_PCHP; }
DWORD OffsetConfig::PCMaxHP()    { return s_PCMaxHP; }
// ⚠️ 2026-04-17: 座標改用封包方案，返回 0（TLS 失效，外部無法讀取）
// ⚠️ 2026-04-29: 修復！PCPos 必須能讀取其他玩家座標，否則反 PK 完全失效
DWORD OffsetConfig::PCPosX()     { return s_PCPosX; }
DWORD OffsetConfig::PCPosY()     { return s_PCPosY; }
DWORD OffsetConfig::PCPosZ()     { return s_PCPosZ; }

DWORD OffsetConfig::GLCharClientPtr() { return s_GLCharClientPtr; }
DWORD OffsetConfig::InvArrayOffset() { return s_InvArrayOffset; }
int   OffsetConfig::InvMaxSlots()    { return s_InvMaxSlots; }
DWORD OffsetConfig::InvItemId()     { return s_InvItemId; }
DWORD OffsetConfig::InvItemPtr()    { return s_InvItemPtr; }
DWORD OffsetConfig::InvItemCount()  { return s_InvItemCount; }
DWORD OffsetConfig::InvSlotStride() { return s_InvSlotStride; }

DWORD OffsetConfig::NetWS2_32_IAT()    { return s_NetWS2_32_IAT; }
WORD  OffsetConfig::NetHeartbeat()     { return s_NetHeartbeat; }
WORD  OffsetConfig::NetUseItem()       { return s_NetUseItem; }
WORD  OffsetConfig::NetMove()          { return s_NetMove; }
WORD  OffsetConfig::NetMoveStop()      { return s_NetMoveStop; }
WORD  OffsetConfig::NetPickupItem()    { return s_NetPickupItem; }
WORD  OffsetConfig::NetPickupGold()    { return s_NetPickupGold; }
WORD  OffsetConfig::NetNpcTalk()       { return s_NetNpcTalk; }
WORD  OffsetConfig::NetNpcBuy()        { return s_NetNpcBuy; }
WORD  OffsetConfig::NetNpcSell()       { return s_NetNpcSell; }
WORD  OffsetConfig::NetNpcClose()      { return s_NetNpcClose; }
WORD  OffsetConfig::NetAttackSubtype() { return s_NetAttackSubtype; }
WORD  OffsetConfig::NetSkillSubtype()  { return s_NetSkillSubtype; }
WORD  OffsetConfig::NetSkillMsgID()    { return s_NetSkillMsgID; }

DWORD OffsetConfig::GameTimeHour()    { return s_GameTimeHour; }
DWORD OffsetConfig::GameTimeMinute()  { return s_GameTimeMinute; }

DWORD OffsetConfig::NPC_ID()         { return s_NPC_ID; }
DWORD OffsetConfig::ShopItemCount()  { return s_ShopItemCount; }
DWORD OffsetConfig::ShopItemID()     { return s_ShopItemID; }
DWORD OffsetConfig::ShopPrice()      { return s_ShopPrice; }
DWORD OffsetConfig::ShopName()       { return s_ShopName; }

DWORD OffsetConfig::FuncMsgProcess_RVA()   { return s_FuncMsgProcess_RVA; }
DWORD OffsetConfig::FuncFrameMove_RVA()    { return s_FuncFrameMove_RVA; }
DWORD OffsetConfig::FuncNetClient_RVA()    { return s_FuncNetClient_RVA; }
DWORD OffsetConfig::FuncPacketSend_RVA()   { return s_FuncPacketSend_RVA; }
DWORD OffsetConfig::FuncNPCBuySell_RVA()    { return s_FuncNPCBuySell_RVA; }
DWORD OffsetConfig::FuncPickupPacket_RVA()  { return s_FuncPickupPacket_RVA; }
