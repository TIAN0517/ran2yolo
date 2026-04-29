#pragma once
// ============================================================
// RAN2 Online 攻擊/技能封包發送模組
// 根據 CE Lua 腳本 + IDA 逆向結果修復 (2026-04-18)
//
// 封包格式：
//   [4B key][2B type][2B size][payload...]
//
// 訊息類型：
//   3819 = MCROW_SKILLUSED (技能使用)
//   3634 = MCROW_ATTACK (普通攻擊)
//   3627 = MCROW_DIE (怪物死亡)
//   3820 = MCROW_ACTOR_Spawn (怪物生成)
//
// Magic Key: 0xAE01
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>

// ============================================================
// RAN2 封包常量
// ============================================================
namespace RAN2Packet {
    // 封包魔數
    constexpr DWORD MAGIC = 0xAE01;

    // 訊息類型
    constexpr WORD TYPE_SKILL = 3819;      // MCROW_SKILLUSED
    constexpr WORD TYPE_ATTACK = 3634;      // MCROW_ATTACK
    constexpr WORD TYPE_NPCATTACK = 3623;  // MCROW_NPCATTACK
    constexpr WORD TYPE_DIE = 3627;         // MCROW_DIE
    constexpr WORD TYPE_SPAWN = 3820;       // MCROW_ACTOR_Spawn
}

// ============================================================
// 技能封包結構（CE Lua 驗證）
//
// 格式：[4B key][2B type][2B size][SkillID][TargetCount][MID][SID]...
// 大小：8 (header) + 16 (payload) = 24 bytes
// ============================================================
#pragma pack(push, 1)
struct RAN2SkillPacket {
    DWORD  dwKey;        // 0xAE01 (魔數)
    WORD   wType;        // 3819 (MCROW_SKILLUSED)
    WORD   wSize;        // payload 大小 = 16
    DWORD  dwSkillID;    // 技能 ID
    DWORD  dwTargetCount; // 目標數量 (1 = 單體)
    DWORD  dwTargetMID;  // 目標 MID (怪物 ID 高位)
    DWORD  dwTargetSID;  // 目標 SID (怪物 ID 低位)
};
#pragma pack(pop)
static_assert(sizeof(RAN2SkillPacket) == 24, "RAN2SkillPacket must be 24 bytes");

// ============================================================
// 攻擊封包結構（CE Lua 驗證）
//
// 格式：[4B key][2B type][2B size][AttackerMID][AttackerSID][TargetMID][TargetSID]
// 大小：8 (header) + 16 (payload) = 24 bytes
// ============================================================
#pragma pack(push, 1)
struct RAN2AttackPacket {
    DWORD  dwKey;         // 0xAE01 (魔數)
    WORD   wType;        // 3634 (MCROW_ATTACK)
    WORD   wSize;        // payload 大小 = 16
    DWORD  dwAttackerMID; // 攻擊者 MID
    DWORD  dwAttackerSID; // 攻擊者 SID
    DWORD  dwTargetMID;   // 目標 MID
    DWORD  dwTargetSID;   // 目標 SID
};
#pragma pack(pop)
static_assert(sizeof(RAN2AttackPacket) == 24, "RAN2AttackPacket must be 24 bytes");

// ============================================================
// IDA 反編譯：v7.6 原始攻擊封包（70 bytes）
// IDA: sub_682DF0 構造攻擊封包，dwSize = 70
// ⚠️ 這是 IDA 分析結果，可能與實際使用的格式不同
// ============================================================
#pragma pack(push, 1)
struct V76AttackPacket {
    DWORD  dwSize;        // 70 (封包總大小)
    DWORD  dwSession;     // session ID
    DWORD  dwOpcode;      // 1 (攻擊/技能)
    DWORD  dwSubType;     // 0x3808
    DWORD  targetId;      // 攻擊目標 ID
    DWORD  attackCount;   // 攻擊計數（遞增）
    BYTE   padding[46];   // 填充 (70 - 24 = 46)
};
#pragma pack(pop)
static_assert(sizeof(V76AttackPacket) == 70, "V76AttackPacket must be 70 bytes");

// ============================================================
// 範圍技能封包（多目標）
//
// 格式：[header][SkillID][TargetCount][MID1][SID1][MID2][SID2]...
// ============================================================
#pragma pack(push, 1)
struct RAN2AreaSkillPacket {
    DWORD  dwKey;         // 0xAE01
    WORD   wType;        // 3819
    WORD   wSize;         // 8 + targetCount * 8
    DWORD  dwSkillID;     // 技能 ID
    DWORD  dwTargetCount; // 目標數量
    // 後面跟隨 [MID][SID] * targetCount
};
#pragma pack(pop)

// ============================================================
// 初始化
// ============================================================
extern bool InitAttackSender(const char* serverIp, int serverPort);
extern void ShutdownAttackSender();
extern bool TryAttackSenderConnect(int timeoutMs);

// ============================================================
// Session ID / Player ID 管理
// ============================================================

// 設置玩家 MID/SID（用於攻擊封包）
extern void SetPlayerID(DWORD mid, DWORD sid);

// 取得玩家 MID/SID
extern void GetPlayerID(DWORD* outMid, DWORD* outSid);

// 設置目標 MID/SID（用於技能封包）
extern void SetTargetID(DWORD mid, DWORD sid);

// 從封包更新 Session ID
extern void UpdateSessionIdFromPacket(const BYTE* data, int len);
extern void SetSessionId(DWORD sessionId);
extern DWORD GetSessionId();
extern bool HasValidSessionId();

// ============================================================
// 封包發送
// ============================================================

// 發送技能封包（單體）
// skillId: 技能 ID
// targetMid: 目標 MID
// targetSid: 目標 SID
extern bool SendSkillPacket(DWORD skillId, DWORD targetMid, DWORD targetSid);

// 發送技能封包（帶位置）
extern bool SendSkillPacketPos(DWORD skillId, DWORD targetMid, DWORD targetSid,
                              float posX, float posY, float posZ);

// 發送普通攻擊封包
// attackerMid/Sid: 攻擊者 ID（通常是自己）
// targetMid/Sid: 目標怪物 ID
extern bool SendAttackPacket(DWORD attackerMid, DWORD attackerSid,
                            DWORD targetMid, DWORD targetSid);

// 發送範圍技能封包
// skillId: 技能 ID
// targets: 目標數組，格式 {{mid1,sid1}, {mid2,sid2}, ...}
// count: 目標數量
extern bool SendAreaSkillPacket(DWORD skillId, const DWORD* targets, int count);

// ============================================================
// 狀態查詢
// ============================================================
extern bool IsAttackSenderConnected();
extern const char* GetAttackSenderError();
extern DWORD GetAttackPacketCount();

// ============================================================
// 調試模式
// ============================================================
extern void SetAttackSenderDebugMode(bool enable);
extern int BuildSkillPacket(BYTE* outBuf, int bufSize, DWORD skillId, DWORD targetMid, DWORD targetSid);
extern int BuildAttackPacket(BYTE* outBuf, int bufSize, DWORD attackerMid, DWORD attackerSid,
                            DWORD targetMid, DWORD targetSid);
