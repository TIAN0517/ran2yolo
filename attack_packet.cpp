#include "attack_packet.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include "attack_packet.h"
#include "offset_config.h"
#include "gui_ranbot.h"
#include <cstring>
#include <cstdio>

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

// ============================================================
// RAN2 攻擊/技能封包發送實作 (2026-04-18)
// 根據 CE Lua 腳本修復
// ============================================================

// 配置
static const char* s_serverIp = "210.64.10.55";
static int s_serverPort = 6870;

// 連接狀態
static SOCKET s_socket = INVALID_SOCKET;
static bool s_debugMode = false;
static char s_error[256] = {0};

// Session ID
static std::atomic<DWORD> s_sessionId{0};
static bool s_sessionIdValid = false;

// 玩家 MID/SID
static DWORD s_playerMid = 0;
static DWORD s_playerSid = 0;

// 目標 MID/SID
static DWORD s_targetMid = 0;
static DWORD s_targetSid = 0;

// 攻擊計數
static std::atomic<DWORD> s_attackCount{0};

// 鎖
static CRITICAL_SECTION s_cs;

// ============================================================
// Winsock 初始化
// ============================================================
static bool InitWinsock() {
    static bool s_wsaInitialized = false;
    static bool s_wsaResult = false;
    if (!s_wsaInitialized) {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        s_wsaResult = (result == 0);
        s_wsaInitialized = true;
        if (!s_wsaResult) {
            sprintf_s(s_error, "WSAStartup failed: %d", result);
            return false;
        }
    }
    return s_wsaResult;
}

// ============================================================
// 建立 TCP 連接
// ============================================================
static bool ConnectToServer() {
    if (s_socket != INVALID_SOCKET) {
        return true;  // 已經連接
    }

    if (!InitWinsock()) {
        return false;
    }

    s_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_socket == INVALID_SOCKET) {
        int err = WSAGetLastError();
        sprintf_s(s_error, "socket() failed: %d", err);
        UIAddLog("[Attack] TCP socket 建立失敗: %s", s_error);
        return false;
    }

    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = NULL;
    char portStr[16];
    sprintf_s(portStr, "%d", s_serverPort);

    int ret = getaddrinfo(s_serverIp, portStr, &hints, &result);
    if (ret != 0) {
        sprintf_s(s_error, "getaddrinfo failed: %d", ret);
        UIAddLog("[Attack] 地址解析失敗: %s", s_error);
        closesocket(s_socket);
        s_socket = INVALID_SOCKET;
        return false;
    }

    ret = connect(s_socket, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);

    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEISCONN) {
            sprintf_s(s_error, "connect failed: %d", err);
            UIAddLog("[Attack] TCP 連接失敗: %s:%d - %s",
                s_serverIp, s_serverPort, s_error);
            closesocket(s_socket);
            s_socket = INVALID_SOCKET;
            return false;
        }
    }

    UIAddLog("[Attack] TCP 連接成功: %s:%d", s_serverIp, s_serverPort);
    return true;
}

// ============================================================
// 初始化
// ============================================================
bool InitAttackSender(const char* serverIp, int serverPort) {
    if (serverIp) s_serverIp = serverIp;
    if (serverPort > 0) s_serverPort = serverPort;

    InitializeCriticalSection(&s_cs);
    s_sessionId.store(0);
    s_sessionIdValid = false;
    s_attackCount.store(0);
    s_playerMid = 0;
    s_playerSid = 0;
    s_targetMid = 0;
    s_targetSid = 0;

    bool connected = ConnectToServer();
    if (connected) {
        UIAddLog("[Attack] 攻擊發送器已初始化 (Server: %s:%d)", s_serverIp, s_serverPort);
    } else {
        UIAddLog("[Attack] 攻擊發送器初始化（TCP 連接將在發送時重試）");
    }

    return true;
}

void ShutdownAttackSender() {
    EnterCriticalSection(&s_cs);
    if (s_socket != INVALID_SOCKET) {
        shutdown(s_socket, SD_BOTH);
        closesocket(s_socket);
        s_socket = INVALID_SOCKET;
        UIAddLog("[Attack] TCP 連接已關閉");
    }
    LeaveCriticalSection(&s_cs);
    DeleteCriticalSection(&s_cs);
    WSACleanup();
}

// ============================================================
// Session ID / Player ID 管理
// ============================================================
void SetPlayerID(DWORD mid, DWORD sid) {
    s_playerMid = mid;
    s_playerSid = sid;
    UIAddLog("[Attack] 玩家 ID 設置: MID=0x%X SID=0x%X", mid, sid);
}

void GetPlayerID(DWORD* outMid, DWORD* outSid) {
    if (outMid) *outMid = s_playerMid;
    if (outSid) *outSid = s_playerSid;
}

void SetTargetID(DWORD mid, DWORD sid) {
    s_targetMid = mid;
    s_targetSid = sid;
}

void UpdateSessionIdFromPacket(const BYTE* data, int len) {
    if (!data || len < 16) return;

    DWORD sessionId = *(const DWORD*)(data + 4);
    if (sessionId != 0 && sessionId != 0xFFFFFFFF) {
        DWORD oldVal = s_sessionId.load();
        if (oldVal != sessionId) {
            s_sessionId.store(sessionId);
            s_sessionIdValid = true;
            static bool s_loggedOnce = false;
            if (!s_loggedOnce) {
                UIAddLog("[Attack] Session ID 已獲取: 0x%08X", sessionId);
                s_loggedOnce = true;
            }
        }
    }
}

void SetSessionId(DWORD sessionId) {
    s_sessionId.store(sessionId);
    s_sessionIdValid = (sessionId != 0 && sessionId != 0xFFFFFFFF);
    UIAddLog("[Attack] Session ID 手動設置: 0x%08X", sessionId);
}

DWORD GetSessionId() {
    return s_sessionId.load();
}

bool HasValidSessionId() {
    DWORD sid = s_sessionId.load();
    return s_sessionIdValid && sid != 0 && sid != 0xFFFFFFFF;
}

// ============================================================
// 構造技能封包
// ============================================================
int BuildSkillPacket(BYTE* outBuf, int bufSize, DWORD skillId, DWORD targetMid, DWORD targetSid) {
    if (!outBuf || bufSize < (int)sizeof(RAN2SkillPacket)) return 0;

    RAN2SkillPacket pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.dwKey = RAN2Packet::MAGIC;           // 0xAE01
    pkt.wType = RAN2Packet::TYPE_SKILL;       // 3819
    pkt.wSize = 16;                           // payload 大小
    pkt.dwSkillID = skillId;
    pkt.dwTargetCount = 1;                    // 單體
    pkt.dwTargetMID = targetMid;
    pkt.dwTargetSID = targetSid;

    memcpy(outBuf, &pkt, sizeof(RAN2SkillPacket));
    return sizeof(RAN2SkillPacket);
}

// ============================================================
// 構造攻擊封包
// ============================================================
int BuildAttackPacket(BYTE* outBuf, int bufSize, DWORD attackerMid, DWORD attackerSid,
                     DWORD targetMid, DWORD targetSid) {
    if (!outBuf || bufSize < (int)sizeof(RAN2AttackPacket)) return 0;

    RAN2AttackPacket pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.dwKey = RAN2Packet::MAGIC;            // 0xAE01
    pkt.wType = RAN2Packet::TYPE_ATTACK;       // 3634
    pkt.wSize = 16;                            // payload 大小
    pkt.dwAttackerMID = attackerMid;
    pkt.dwAttackerSID = attackerSid;
    pkt.dwTargetMID = targetMid;
    pkt.dwTargetSID = targetSid;

    memcpy(outBuf, &pkt, sizeof(RAN2AttackPacket));
    return sizeof(RAN2AttackPacket);
}

// ============================================================
// 發送封包
// ============================================================
static bool SendPacket(const BYTE* data, int len) {
    if (!data || len <= 0) return false;

    // 調試模式：只構造不發送
    if (s_debugMode) {
        char hex[512] = {0};
        for (int i = 0; i < len && i < 128; i++) {
            char tmp[8];
            sprintf_s(tmp, "%02X ", data[i]);
            strcat_s(hex, tmp);
        }
        UIAddLog("[Attack-DBG] 構造封包: len=%d HEX: %s", len, hex);
        return true;
    }

    EnterCriticalSection(&s_cs);

    if (s_socket == INVALID_SOCKET) {
        if (!ConnectToServer()) {
            LeaveCriticalSection(&s_cs);
            return false;
        }
    }

    int ret = send(s_socket, (const char*)data, len, 0);

    LeaveCriticalSection(&s_cs);

    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        sprintf_s(s_error, "send failed: %d", err);

        if (err == WSAECONNRESET || err == WSAENOTSOCK) {
            EnterCriticalSection(&s_cs);
            if (s_socket != INVALID_SOCKET) {
                closesocket(s_socket);
                s_socket = INVALID_SOCKET;
            }
            LeaveCriticalSection(&s_cs);
            UIAddLog("[Attack] 連接重置，嘗試重建...");
            if (ConnectToServer()) {
                ret = send(s_socket, (const char*)data, len, 0);
            }
        }

        if (ret == SOCKET_ERROR) {
            static int s_errCount = 0;
            if (s_errCount < 5) {
                UIAddLog("[Attack] 發送失敗: %s", s_error);
                s_errCount++;
            }
            return false;
        }
    }

    return true;
}

// ============================================================
// 發送技能封包（單體）
// ============================================================
bool SendSkillPacket(DWORD skillId, DWORD targetMid, DWORD targetSid) {
    // 使用當前設置的玩家 ID
    (void)s_playerMid;
    (void)s_playerSid;

    BYTE buf[sizeof(RAN2SkillPacket)];
    int len = BuildSkillPacket(buf, sizeof(buf), skillId, targetMid, targetSid);
    if (len == 0) return false;

    bool ok = SendPacket(buf, len);

    static int s_lastLog = 0;
    DWORD now = GetTickCount();
    if (now - s_lastLog > 3000) {
        UIAddLog("[Attack] >>> 技能封包: SkillID=%d Target=(MID=0x%X SID=0x%X) %s",
            skillId, targetMid, targetSid, ok ? "OK" : "FAIL");
        s_lastLog = now;
    }

    return ok;
}

bool SendSkillPacketPos(DWORD skillId, DWORD targetMid, DWORD targetSid,
                        float posX, float posY, float posZ) {
    // 對於帶位置的技能封包，目前使用基本版本
    // 位置信息在部分遊戲中可選
    (void)posX;
    (void)posY;
    (void)posZ;
    return SendSkillPacket(skillId, targetMid, targetSid);
}

// ============================================================
// 發送普通攻擊封包
// ============================================================
bool SendAttackPacket(DWORD attackerMid, DWORD attackerSid,
                     DWORD targetMid, DWORD targetSid) {
    BYTE buf[sizeof(RAN2AttackPacket)];
    int len = BuildAttackPacket(buf, sizeof(buf), attackerMid, attackerSid, targetMid, targetSid);
    if (len == 0) return false;

    bool ok = SendPacket(buf, len);
    s_attackCount.fetch_add(1);

    static int s_lastLog = 0;
    DWORD now = GetTickCount();
    if (now - s_lastLog > 3000) {
        UIAddLog("[Attack] >>> 攻擊封包: Attacker=(MID=0x%X SID=0x%X) Target=(MID=0x%X SID=0x%X) %s",
            attackerMid, attackerSid, targetMid, targetSid, ok ? "OK" : "FAIL");
        s_lastLog = now;
    }

    return ok;
}

// ============================================================
// 發送範圍技能封包
// ============================================================
bool SendAreaSkillPacket(DWORD skillId, const DWORD* targets, int count) {
    if (!targets || count <= 0) return false;
    if (count > 50) count = 50;  // 限制最大目標數

    // 計算封包大小
    int payloadSize = 8 + count * 8;  // SkillID + TargetCount + (MID+SID) * count
    int totalSize = 8 + payloadSize;  // header + payload

    if (totalSize > 4096) return false;

    BYTE buf[4096];
    memset(buf, 0, sizeof(buf));

    // Header
    *(DWORD*)(buf + 0) = RAN2Packet::MAGIC;     // key
    *(WORD*)(buf + 4) = RAN2Packet::TYPE_SKILL;  // type
    *(WORD*)(buf + 6) = (WORD)payloadSize;         // size

    // Payload
    *(DWORD*)(buf + 8) = skillId;
    *(DWORD*)(buf + 12) = count;

    // Targets
    for (int i = 0; i < count; i++) {
        *(DWORD*)(buf + 16 + i * 8) = targets[i * 2];     // MID
        *(DWORD*)(buf + 16 + i * 8 + 4) = targets[i * 2 + 1]; // SID
    }

    bool ok = SendPacket(buf, totalSize);

    UIAddLog("[Attack] >>> 範圍技能封包: SkillID=%d Targets=%d %s",
        skillId, count, ok ? "OK" : "FAIL");

    return ok;
}

// ============================================================
// 狀態查詢
// ============================================================
bool IsAttackSenderConnected() {
    return s_socket != INVALID_SOCKET;
}

const char* GetAttackSenderError() {
    return s_error;
}

DWORD GetAttackPacketCount() {
    return s_attackCount.load();
}

void SetAttackSenderDebugMode(bool enable) {
    s_debugMode = enable;
    UIAddLog("[Attack] 調試模式: %s", enable ? "ON" : "OFF");
}
