#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// ============================================================
// Ran2 Online - 記憶體偏移（CE + CT 驗證）
// 驗證日期：2026-04-18
//
// ✅ 2026-04-18 用戶確認：
//   HP=810, MaxHP=810, MP=72, MaxMP=72, SP=244, MaxSP=244, Gold=588988
//   CE Cheat Table 確認：Game.exe+0x92F2F8 = 0x18FF2F8 ✓
// GameBase: 0x00FD0000（CE MCP verified, PID 27088）
// 偏移為 RVA，使用時需加上遊戲基底位址 (GameBase)
//
// ⚠️ 偏移格式說明：
//   所有 Player/Identity/Target/Network 全域靜態地址為 RVA 格式
//   使用時：實際地址 = GameBase + X
//   其中 GameBase = Game.exe 模組載入基底（動態值）
//
// ⚠️ 地址翻譯公式（2026-04-17 驗證）：
//   CE = IDA_RVA + 0xBD0000
//   IDA 基址：0x400000（PE 預設）
//   CE 基址：0xFD0000（實際加載）
//
// ⚠️ GameBase 歷史變更：
//   0x008E0000 → 0x0120_0000 → 0x00FD0000（最新, 2026-04-17）
//   ASLR 導致每次啟動可能不同，但 RVA 偏移不變
//
// ⚠️ 重要發現（2026-04-16）：
//   1. 0xD10XXX 區域為遊戲保護區域，外部 RPM 無法讀取
//   2. EntityPool 依賴 TLS，外部無法讀取
//   3. 玩家屬性、背包可正常讀取
//
// ✅ 已驗證偏移（2026-04-17）：
//   Player::HP/MaxHP/MP/MaxMP/SP/MaxSP/Gold/Level
//   Inventory::GLCharClient, InvCount
//   Functions::MsgProcess, FrameMove, NetClient, PacketSend, NPCBuySell, PickupPacket
//
// ✅ RVA 穩定性：
//   RVA 是 PE 格式的相對地址，與實際加載基底無關
//   Win7~Win11 完全相容，不受 ASLR 影響
//   唯一要求：遊戲版本不變（RVA 依賴特定 exe 版本）
// ============================================================

// L1 fix: 移除未使用的 snprintf/vsnprintf 巨集（v142 toolset 永遠 >= 1900）
// #if _MSC_VER < 1900
// #define snprintf _snprintf
// #define vsnprintf _vsnprintf
// #endif

#define GAME_PROCESS_NAME "Game.exe"
#define GAME_WINDOW_NAME "亂2 online"  // 視窗標題（繁體）
#define TRAINER_PIPE_NAME "\\\\.\\pipe\\RanTrainerPipe"

namespace Offsets {

    // ============================================================
    // PE 結構常量（dumpbin /headers 分析, 2026-04-16）
    // Game.exe: x86, MSVC 14.44 (VS2022), LTCG, ASLR+DEP
    // PDB: F:\RanBuild_vs2022\_BuildData\Basic\Client_Taiwan\Basic.pdb
    // ============================================================
    namespace PE {
        constexpr DWORD ImageBase      = 0x00400000;   // 預設載入位址
        constexpr DWORD EntryPoint_RVA = 0x5E7DF4;     // 入口點 RVA
        constexpr DWORD SectionAlignment = 0x1000;     // 區段對齊
        constexpr DWORD FileAlignment  = 0x200;        // 檔案對齊
        constexpr DWORD ImageSize      = 0x9AF000;     // 映像大小

        // 區段 RVA
        constexpr DWORD Text_RVA       = 0x001000;     // .text  (6.4MB, Code+ER)
        constexpr DWORD Rdata_RVA      = 0x671000;     // .rdata (828KB, Data+R)
        constexpr DWORD Data_RVA       = 0x744000;     // .data  (2MB, Data+RW)
        constexpr DWORD FpTable_RVA    = 0x949000;     // .fptable (128B, Data+RW)
        constexpr DWORD Rsrc_RVA       = 0x94A000;     // .rsrc  (42KB, Data+R)
        constexpr DWORD Reloc_RVA      = 0x955000;     // .reloc (360KB, Data+RD)

        // 目錄表 RVA
        constexpr DWORD ImportDir_RVA  = 0x73F18C;     // 導入表
        constexpr DWORD ImportDir_Size = 0x230;        // 導入表大小
        constexpr DWORD IAT_RVA        = 0x671000;     // IAT
        constexpr DWORD IAT_Size       = 0xD6C;        // IAT 大小
        constexpr DWORD RelocDir_RVA   = 0x955000;     // 重定位表
        constexpr DWORD RelocDir_Size  = 0x59150;      // 重定位表大小
        constexpr DWORD DebugDir_RVA   = 0x6F5AD0;     // 調試目錄
    }

    // ============================================================
    // 導入表 - IAT 基址（dumpbin /imports 分析, 2026-04-16）
    // ⚠️ 以下值為 IDA 虛擬地址(VA)，基於 ImageBase=0x00400000
    //    RPM 使用時需轉換: 實際地址 = GameBase + (VA - 0x400000)
    //    或直接由已載入模組用 GetModuleHandle + 函數名獲取
    // ============================================================
    namespace IAT {
        // TODO: verify after update, .rdata shift may differ from +0x3E2000
        // PE 分析顯示 .rdata 段 RVA 從 0x671000 變為 0x673000（+0x2000），IAT 位移可能不同
        // 以下值暫保留舊值，待 CE/IDA 驗證後更新

        // DirectX 9 渲染
        constexpr DWORD d3d9          = 0xA71B40;  // Direct3DCreate9
        constexpr DWORD d3dx9_43      = 0xA71B48;  // 65 個 D3DX 函數
        constexpr DWORD d3dxof        = 0xA71C10;  // DirectXFileCreate

        // 輸入系統
        constexpr DWORD DINPUT8       = 0xA71054;  // DirectInput8Create

        // 網路通訊
        constexpr DWORD WS2_32        = 0xA71AEC;  // Winsock (WSASend 等)
        constexpr DWORD IPHLPAPI      = 0xA71238;  // GetAdaptersAddresses

        // 反調試/進程枚舉
        constexpr DWORD KERNEL32      = 0xA71240;  // 130+ 函數 (含 IsDebuggerPresent 等)
        constexpr DWORD ADVAPI32      = 0xA71000;  // CryptAPI + Registry

        // 視窗/輸入
        constexpr DWORD USER32        = 0xA7168C;  // 180+ 函數 (含 GetAsyncKeyState 等)
        constexpr DWORD IMM32         = 0xA71208;  // 輸入法

        // 音效/多媒體
        constexpr DWORD DSOUND        = 0xA7105C;  // DirectSound
        constexpr DWORD WINMM         = 0xA71A9C;  // timeGetTime, mmio*
        constexpr DWORD libvlc        = 0xA71C18;  // VLC 視頻播放

        // 圖形
        constexpr DWORD GDI32         = 0xA71064;  // GDI 繪圖
        constexpr DWORD MSIMG32       = 0xA7159C;  // AlphaBlend, TransparentBlt
        constexpr DWORD gdiplus       = 0xA71C74;  // GDI+
        constexpr DWORD UxTheme       = 0xA71A58;  // 主題渲染

        // COM/Shell
        constexpr DWORD ole32         = 0xA71CD0;  // COM
        constexpr DWORD OLEAUT32      = 0xA71600;  // OLE Auto
        constexpr DWORD SHELL32       = 0xA71640;  // Shell
        constexpr DWORD SHLWAPI       = 0xA71670;  // 路徑工具

        // 其他
        constexpr DWORD WebView2      = 0xA71B38;  // Chromium 嵌入
        constexpr DWORD ODBC32        = 0xA715A8;  // 資料庫 (17 ordinal)
        constexpr DWORD VERSION       = 0xA71A8C;  // 版本查詢
        constexpr DWORD WINSPOOL      = 0xA71ADC;  // 列印
        constexpr DWORD OLEACC        = 0xA715F0;  // 無障礙
        constexpr DWORD oledlg        = 0xA71D64;  // OLE 對話框
    }

    // ============================================================
    // 反作弊 API 列表（IAT Hook 用）
    // dumpbin /imports 確認遊戲導入了以下關鍵 API
    // 
    // ⚠️ IAT Hook 方法: 不可直接用 hint 值當偏移！
    //    hint 是 PE 載入器的名稱查找提示，不是 IAT 陣列索引。
    //    正確做法: 遍歷 Import Name Table 按名匹配，對應 IAT[index*4]
    //    相關工具函數見分析腳本與核心模組
    // ============================================================
    namespace AntiCheat {
        // ── 反調試 API（Hook 返回 FALSE/0 繞過）──
        // IsDebuggerPresent     → KERNEL32.dll, hint=0x3AD
        // DebugBreak            → KERNEL32.dll, hint=0x124
        // OutputDebugStringW    → KERNEL32.dll, hint=0x448
        // OutputDebugStringA    → KERNEL32.dll, hint=0x447

        // ── 進程枚舉 API（Hook 返回空結果繞過）──
        // CreateToolhelp32Snapshot → KERNEL32.dll, hint=0x119
        // Process32First/Next     → KERNEL32.dll, hint=0x45B/0x45D
        // Module32First/Next      → KERNEL32.dll, hint=0x413/0x415
        // K32EnumProcesses        → KERNEL32.dll, hint=0x3C9
        // K32GetProcessImageFileNameA → KERNEL32.dll, hint=0x3D6
        // OpenProcess             → KERNEL32.dll, hint=0x43C

        // ── 輸入檢測 API ──
        // GetAsyncKeyState    → USER32.dll, hint=0x128
        // GetKeyState         → USER32.dll, hint=0x170
        // GetKeyboardState    → USER32.dll, hint=0x175

        // ── 網路 API ──
        // WSASend             → WS2_32.dll, hint=0x4E
        // WSAEventSelect      → WS2_32.dll, hint=0x2F

        // ── 加密 API（用於 param.ini 解密追蹤）──
        // CryptAcquireContextA → ADVAPI32.dll, hint=0xC1
        // CryptCreateHash      → ADVAPI32.dll, hint=0xC4
        // CryptHashData        → ADVAPI32.dll, hint=0xD9
        // CryptDeriveKey       → ADVAPI32.dll, hint=0xC6
        // CryptEncrypt         → ADVAPI32.dll, hint=0xCB
        // CryptDecrypt         → ADVAPI32.dll, hint=0xC5
    }

    // ============================================================
    // DirectX 9 Hook 點（歷史參考，主流程未使用）
    // ============================================================
    namespace DX9 {
        // Direct3DCreate9 导入序数 = 9
        constexpr DWORD D3D9_Create9_Ordinal = 9;
        // Hook 策略: CreateDevice → 保存 Device 指針 → Hook EndScene/Reset
        // vtable 偏移 (IDirect3DDevice9)
        constexpr int VTable_EndScene     = 42;  // EndScene
        constexpr int VTable_Reset        = 16;  // Reset
        constexpr int VTable_Present      = 17;  // Present
        constexpr int VTable_DrawIndexedPrimitive = 82;  // DIP
    }

    // ============================================================
    // Identity（遊戲核心指標）— CE MCP 驗證 2026-04-17
    // 驗證方式：
    //   Name = GLChar+0x48 = 0x92F200（CE 確認 "JyTian"）
    //   → GLChar base = 0x92F200 - 0x48 = 0x92F1B8
    //   交叉驗證：0x92F1B8 + 0x140 = 0x92F2F8（HP 偏移）✓
    // ⚠️ 注意：EntityPool TLS 依賴，外部無法直接讀取
    // ============================================================
    namespace Identity {
        // ✅ GLCharacter 單例物件（RVA from GameBase）
        // 2026-04-26 CE verified:
        // - HP @ base+0x92F2F8 = 810
        // - PosX @ base+0x92F550 = 512.00
        // - GLCharacter pointer is NOT directly accessible, using base offsets
        constexpr DWORD GLCharacter_Obj     = 0x92F19C;  // ✅ CE verified
        constexpr DWORD GLGaeaServer_Obj    = 0x92F170;
    }

    // ============================================================
    // 實體池（m_GlobCROWList 鏈表）— IDA Pro MCP v2 反編譯 2026-04-26
    // 指標鏈: GLChar(+0x24) → GLLandManClient(+0xA790) → CROWListNode
    // CROWListNode: +0x00=GLCrowClient*, +0x08=next node*
    // ============================================================
    namespace EntityPool {
        // GLLandManClient 內的怪物鏈表偏移
        // IDA: a1+42896=CROW, a1+42908=PC, a1+42920=Pet (十進位→十六進位驗證)
        constexpr DWORD LandMan_CROWList    = 0xA790;   // m_GlobCROWList (42896=0xA790)
        constexpr DWORD LandMan_PCList      = 0xA79C;   // m_GlobPCList (42908=0xA79C)
        constexpr DWORD LandMan_PetList     = 0xA7A8;   // m_GlobAnyPetList (42920=0xA7A8)

        // 鏈表節點結構 (直接頭指標，無哨兵)
        // k = *(LandMan + offset) → node
        // *k = GLCrowClient*, k[2] = next node*
        // 當 k = 0 或 *k = 0 時結束
        constexpr DWORD Node_CrowPtr  = 0x00;   // GLCrowClient* (NULL=已刪除)
        constexpr DWORD Node_Prev     = 0x04;   // 前一節點指標
        constexpr DWORD Node_Next     = 0x08;   // 下一節點指標

        // GLCrowClient 物件偏移 (IDA sub_6554C0 + sub_657B70 反編譯)
        // ⚠️ 2026-04-17: 這些偏移依賴 TLS，外部無法讀取
        //    改用共享記憶體方案：Local\RanBot_NetHook
        namespace Crow {
            constexpr DWORD VTable       = 0x00;   // vtable 指標
            constexpr DWORD DataType     = 0x04;   // 類型資料
            constexpr DWORD CrowDataPtr  = 0x08;   // GLCrowData* (非NULL=有效)
            constexpr DWORD HP           = 0x7B0;  // 當前 HP (DWORD)
            constexpr DWORD MaxHP        = 0x7B4;  // 最大 HP (WORD)
            constexpr DWORD SkinPtr      = 0x874;  // DxSkinCharData*
            constexpr DWORD SpawnPos     = 0x878;  // 出生座標 (QWORD: x,y)
            constexpr DWORD ServerID     = 0x91C;  // 伺服器 ID (DWORD) — RPM驗證匹配TargetID
            constexpr DWORD ServerID2    = 0x944;  // 舊標記（0xFFFFFFFF=無效）
            constexpr DWORD LandManPtr   = 0x924;  // GLLandManClient*
            constexpr DWORD AIState      = 0x938;  // AI 狀態 (0=idle,1=move,2=chase...)
            constexpr DWORD PosX         = 0x890;   // 怪物 X 座標 (float)
            constexpr DWORD PosY         = 0x894;   // 怪物 Y 座標 (float)
            constexpr DWORD PosZ         = 0x898;   // 怪物 Z 座標 (float)
        }

        // GLPCClient 物件偏移 (與 GLCrowClient 同結構，繼承自 GLCharClient)
        // ⚠️ 2026-04-17: 這些偏移依賴 TLS，外部無法讀取
        //    改用共享記憶體方案：Local\RanBot_NetHook
        namespace PC {
            constexpr DWORD VTable       = 0x00;   // vtable 指標
            constexpr DWORD DataType     = 0x04;   // 類型資料
            constexpr DWORD CharDataPtr  = 0x08;   // GLCharData* (非NULL=有效)
            constexpr DWORD HP           = 0x7B0;  // 當前 HP (DWORD)
            constexpr DWORD MaxHP        = 0x7B4;  // 最大 HP (WORD)
            constexpr DWORD SkinPtr      = 0x874;  // DxSkinCharData*
            constexpr DWORD SpawnPos     = 0x878;  // 出生座標 (QWORD: x,y)
            constexpr DWORD ServerID     = 0x91C;  // 伺服器 ID (DWORD)
            constexpr DWORD PosX         = 0x890;   // 玩家 X 座標 (float)
            constexpr DWORD PosY         = 0x894;   // 玩家 Y 座標 (float)
            constexpr DWORD PosZ         = 0x898;   // 玩家 Z 座標 (float)
        }

        constexpr int MAX_CROWS = 310000;  // 最大遍歷數量（防無限迴圈）
    }

    // ============================================================
    // 玩家屬性（✅ CE verified 2026-04-28）
    // ✅ 所有偏移都是靜態 RVA，可直接讀取
    // ✅ 驗證值：HP=810, MaxHP=810, MP=72, MaxMP=72, SP=244, MaxSP=244, Gold=155763
    // ⚠️ 注意：GLCHAR 是動態堆對象，沒有靜態指針
    // ============================================================
    namespace Player {
        constexpr DWORD Name   = 0x92F210;  // ✅ CE 2026-04-27: "JyTian"
        constexpr DWORD Gold  = 0x92F258;  // ✅ CE 2026-04-28: 155763
        constexpr DWORD Level = 0x92F250;  // ✅ CE 2026-04-27: 18 (WORD)
        constexpr DWORD EXP   = 0x92F2D0;  // ✅ CE verified
        constexpr DWORD EXPMax = 0x92F2D8;  // ✅ CE verified
        constexpr DWORD HP    = 0x92F308;  // ✅ CE 2026-04-28: 810
        constexpr DWORD MaxHP = 0x92F30C;  // ✅ CE 2026-04-28: 810
        constexpr DWORD MP    = 0x92F310;  // ✅ CE 2026-04-28: 72
        constexpr DWORD MaxMP = 0x92F314;  // ✅ CE 2026-04-28: 72
        constexpr DWORD SP    = 0x92F318;  // ✅ CE 2026-04-28: 244
        constexpr DWORD MaxSP = 0x92F31C;  // ✅ CE 2026-04-28: 244
        // ✅ PosX/PosZ CE verified 2026-04-26
        // CE raw: base+0x92F550=512.0, base+0x92F554=59.0 (in-game: x=8,z=59 shown in log)
        constexpr DWORD MapID = 0x930DEC;  // ✅ CE verified
        constexpr DWORD PosX  = 0x94C688;  // ✅ CE 2026-04-27: 59.0 (game x)
        constexpr DWORD PosZ  = 0x932E74;  // ✅ CE 2026-04-27: 10.28 (game z)
        constexpr DWORD PosY  = 0x930E02;  // ✅ CE verified
        constexpr DWORD CombatPower = 0x9311F8;  // ✅ CE verified
        // ✅ 角色屬性
        constexpr DWORD STR = 0x8FEAD0;  // 力量
        constexpr DWORD VIT = 0x8FEAD4;  // 體力
        constexpr DWORD SPR = 0x8FEAD8;  // 精神
        constexpr DWORD DEX = 0x8FEADC;  // 敏捷
        constexpr DWORD END = 0x8FEAE4;  // 耐力
        // ✅ 攻擊力
        constexpr DWORD PhysAtkMin = 0x932120;
        constexpr DWORD PhysAtkMax = 0x932124;
        constexpr DWORD SprAtkMin = 0x93212C;
        constexpr DWORD SprAtkMax = 0x932130;
    }

    // ============================================================
    // 遊戲時間（小時/分鐘）
    // ============================================================
    namespace GameTime {
        constexpr DWORD Hour   = 0x9016A8;  // ✅ CE: 22 (0-23)
        constexpr DWORD Minute = 0x94C5F0;  // ✅ CE: 48 (0-59)
    }

    // ============================================================
    // 鎖定目標（⚠️ 記憶體保護區域，無法直接讀取）
    // ⚠️ 0xD10XXX 區域為遊戲保護區域，外部 RPM 返回失敗
    // ============================================================
    namespace Target {
        constexpr DWORD HasTarget    = 0x93275C;  // ✅ CE verified
        constexpr DWORD ID           = 0x931D90;  // ✅ CE verified
        constexpr DWORD LockedState  = 0x930D28;  // ✅ CE verified
        // ✅ 2026-04-27 用戶驗證：對象鎖定檢測（怪物/NPC/玩家都有效）
        // 0=無鎖定, 1+=已鎖定對象
        constexpr DWORD ObjectLocked  = 0x93338C;  // ✅ CE verified by user
    }

    // ============================================================
    // 背包（✅ CE verified 2026-04-28）
    // ✅ 庫存槽結構：當前選擇的槽 (InvSlot_Current) 或庫存陣列基址 (InvSlot_Data)
    // ⚠️ 注意：滑鼠點擊哪格，InvSlot_Current 就顯示該格的數據
    // ============================================================
    namespace Inventory {
        // ✅ CE 2026-04-28 - 當前選擇的庫存槽指標
        constexpr DWORD InvPtr      = 0x76BBD0;  // 背包指標 (InvPtr for s_InvInvPtr)
        constexpr DWORD InvCount    = 0x76BBD4;  // 背包物品數量
        constexpr DWORD ItemStride  = 0x20;      // 每槽Stride (for s_InvItemStride)
        constexpr DWORD InvSlot_Current = 0x76BCC8;  // 當前選擇的庫存槽結構
        constexpr DWORD InvSlot_Data   = 0x76BC00;  // 庫存槽陣列基址
        constexpr DWORD InvMaxSlots     = 0x76BBBC;  // 最大格數 = 78 (0x4E)

        // 庫存槽結構 (每槽 32 bytes):
        //   +0x00 = 數量 (DWORD)
        //   +0x10 = 物品ID (DWORD)
        //   +0x38 = X座標 (DWORD)
        //   +0x3C = Y座標 (DWORD)
        constexpr DWORD ItemCount_off  = 0x00;     // 物品數量
        constexpr DWORD ItemId_off     = 0x10;     // 物品ID
        constexpr DWORD ItemX_off      = 0x38;     // X座標
        constexpr DWORD ItemY_off      = 0x3C;     // Y座標
        constexpr DWORD SlotStride     = 0x20;     // 每槽 32 bytes
        constexpr int   MaxSlots       = 78;       // 78格 (13x6)
    }

    // ============================================================
    // NPC/商店（✅ CE verified 2026-04-28）
    // ✅ 當 NPC 對話框打開時可讀取
    // ============================================================
    namespace NPC {
        constexpr DWORD NPC_ID         = 0x92F2D8;  // ✅ CE 2026-04-28: 439 (coon221)
        constexpr DWORD Shop_ItemCount = 0x92F468;  // ✅ CE 2026-04-28: 169
        constexpr DWORD Shop_ItemID    = 0x92F488;  // ✅ CE 2026-04-28: 520
        constexpr DWORD Shop_Price     = 0x92F48C;  // ✅ CE 2026-04-28: 324
        constexpr DWORD Shop_Name      = 0x92F3C0;  // ✅ CE 2026-04-28: "coon221"
    }

    // ============================================================
    // 快捷欄/彈藥（✅ CE verified 2026-04-28）
    // ============================================================
    namespace QuickSlot {
        constexpr DWORD ArrowCount = 0x92F8E0;  // ✅ CE 2026-04-27: 749 箭矢
        constexpr DWORD TalismanCount = 0x92F8E4;  // ⚠️ 待驗證（箭矢+4）
    }

    // ============================================================
    // 封包 MsgID（IDA Pro 驗證, 2026-04-13）
    // ============================================================
    namespace Packets {
        // 通用
        constexpr int HEARTBEAT   = 160;     // 0x00A0
        constexpr int USE_ITEM     = 4912;    // 0x1330

        // 移動
        constexpr int MOVE        = 13348;   // 0x3424 ✅ IDA
        constexpr int MOVE_STOP   = 13349;   // 0x3425

        // 撿拾
        constexpr int PICKUP_ITEM  = 13351;   // 0x3427 ✅ IDA
        constexpr int PICKUP_GOLD  = 13353;   // 0x3429 ✅ IDA

        // NPC（CE 重驗證 2026-04-15）
        constexpr int NPC_TALK    = 13522;   // 0x34D2 ✅ IDA+CE
        constexpr int NPC_BUY      = 13559;   // 0x34F7 ✅ IDA+CE
        constexpr int NPC_SELL     = 13560;   // 0x34F8 ✅ IDA+CE
        constexpr int NPC_CLOSE    = 13523;   // 0x34D3

        // 戰鬥（根據 Wireshark v7.6 分析 2026-04-15）
        // 攻擊封包: Opcode=1, Subtype=0x3808, 66 bytes
        constexpr int ATTACK_OPCODE   = 0x01;
        constexpr int ATTACK_SUBTYPE  = 0x3808;
        constexpr int ATTACK_SIZE    = 66;

        // 技能施放封包: Opcode=1, Subtype=0x3807, 64 bytes
        constexpr int SKILL_OPCODE    = 0x01;
        constexpr int SKILL_SUBTYPE   = 0x3807;
        constexpr int SKILL_SIZE     = 64;

        // ✅ IDA 分析確認：技能訊息 ID = 0x3E3 (995)
        constexpr int SKILL_MSGID     = 0x3E3;   // IDA sub_71A220 確認

        // 復活
        constexpr int REVIVE_TOWN  = 0x1014;
        constexpr int REVIVE_SELF   = 0x1011;

        // 實體同步封包（根據 Wireshark v7.6 分析 2026-04-15）
        // v7.6 封包頭結構:
        //   Offset 0-3:  封包大小 (little-endian, DWORD)
        //   Offset 4-7:  Session ID
        //   Offset 8-11: Opcode (DWORD)
        //   Offset 12-15: Subtype (DWORD)
        //   Offset 16+:   Payload
        //
        // 實體結構 (Subtype 0x38): ID(DWORD,4) + Y(WORD/100,2) + Z(WORD/100,2) = 8 bytes
        // 玩家坐標 (Subtype 0x2C): [16-17]=Y(WORD), [20-21]=Z(WORD)
        //
        // 實體 ID 分類:
        //   玩家: ID < 0xD05 (3333)
        //   怪物: ID >= 0xD05 (3333) 且 <= 0x10000 (65536)
        constexpr int ENTITY_SYNC_OPCODE = 0x00;
        constexpr int ENTITY_SYNC_SUBTYPE = 0x38;  // 怪物列表封包 (68 bytes)
        constexpr int PLAYER_ATTR_SUBTYPE = 0x2C; // 玩家屬性封包 (56 bytes)
        constexpr int ENTITY_HEADER_SIZE = 16;
        constexpr int ENTITY_STRUCT_SIZE = 8;

        // 位置更新封包: Opcode=0, Subtype=0x18, 36 bytes
        constexpr int MOVE_OPCODE    = 0x00;
        constexpr int MOVE_SUBTYPE   = 0x18;
        constexpr int MOVE_SIZE     = 36;

        // 心跳封包: Opcode=0, Subtype=0x08, 20 bytes
        constexpr int HEARTBEAT_OPCODE = 0x00;
        constexpr int HEARTBEAT_SUBTYPE = 0x08;
    }

    // ============================================================
    // IDA 找到的關鍵函數和字串（RTTI）- 2026-04-15
    // ============================================================
    namespace IDA {
        // 技能系統
        constexpr DWORD SkillTarget_Num_Str  = 0xAC5D03;  // "Skill %s , target num : %d"
        constexpr DWORD SkillTarget_Str       = 0xAC5DC4;  // "Skill %s , targets : %s"

        // 快捷技能列
        constexpr DWORD QuickSkillSlot_VTable = 0xAE8876;  // CBasicQuickSkillSlotEx vtable

        // 背包/物品系統
        constexpr DWORD InvenFull_Str         = 0xAC0D74;  // "inventory is full!!"
        constexpr DWORD InvenFindPosError_Str  = 0xAC285E;  // "GLInventory FindPosItem error"
        constexpr DWORD OpenInvenWindow_Str    = 0xAC1365;  // "Open Inventory Window, [%d][%s]"

        // 網路訊息
        constexpr DWORD GLMSG_SNETREQ_LANDIN  = 0xAC1308;  // GLMSG::SNETREQ_LANDIN
        constexpr DWORD GLMSG_SNETREQ_READY   = 0xAC1380;  // GLMSG::SNETREQ_READY

        // 怪物系統
        constexpr DWORD MobDel_Packet         = 0xABF279;  // "/mob_del"
        constexpr DWORD GLMobSchedule_Load    = 0xAC568C;  // "GLMobSchedule::Load() Unknown data"

        // GLCharacter 函數（RTTI 字串）
        constexpr DWORD GLChar_FrameMove      = 0xABDFB7;  // "GLCharacter::FrameMove"
        constexpr DWORD GLLandMan_FrameMove   = 0xABDFE2;  // "GLLandManClient::FrameMove"
        constexpr DWORD GLChar_DelHostile     = 0xAC5DF0;  // "GLChar::DelPlayHostile, pos->second"
    }

    // ============================================================
    // 網路（packet_sender.cpp 使用, 2026-04-16 伺服器更新後）
    // ⚠️ NetClientPtr 未經 IDA/CE 驗證，僅供參考
    // ============================================================
    namespace Network {
        constexpr DWORD NetClientPtr = 0x9163FE;  // Inferred (+0x20000 from 0x8F63FE, 未驗證)
        constexpr DWORD SessionId = 0x50;
    }

    // ============================================================
    // 函數地址 RVA（IDA Pro + CE MCP 雙驗證, 2026-04-17）
    //
    // 驗證方式：
    //   1. IDA Pro 分析確認函數邏輯
    //   2. CE AOB 掃描驗證字串參照確認函數正確性
    //   3. 地址翻譯：CE = IDA_RVA + 0xBD0000
    //
    // ⚠️ RVA 特 性：
    //   - RVA 是相對虛擬位址，相對於模組加載基底
    //   - ASLR 可能改變實際加載基底，但 RVA 永遠不變
    //   - Win7~Win11 PE 格式完全相容，RVA 機制通用
    //   - 使用時：實際地址 = GetModuleHandle("Game.exe") + RVA
    // ============================================================
    namespace Functions {
        // 訊息處理
        constexpr DWORD MsgProcess_RVA      = 0x6C7340;  // ✅ CE: 0x127C340 (GLCharMsg::MsgProcess)
        constexpr DWORD FrameMove_RVA       = 0x595510;  // ✅ CE: 0x11595510 (GLCharacter::FrameMove)

        // 封包發送
        constexpr DWORD NetClient_RVA        = 0x58B6D0;  // ✅ CE: 0x11596D0 (取得 NetClient)
        constexpr DWORD PacketSend_RVA      = 0x58B590;  // ✅ CE: 0x1158590 (封包發送)

        // NPC 互動
        constexpr DWORD NPCBuySell_RVA      = 0x636480;  // ✅ CE: 0x11F6480 (NPC 買/賣處理)
        // ✅ 2026-04-27 IDA 分析新增（更精確的子函數）
        constexpr DWORD NPCBuy_RVA          = 0x6A34A0;  // ✅ IDA: GLChar::MsgReqBuyFromNpc
        constexpr DWORD NPCSell_RVA         = 0x6A5500;  // ✅ IDA: GLChar::MsgReqSaleToNpc

        // 背包窗口
        constexpr DWORD InventoryWindow_RVA = 0x5DF3A0;  // ✅ IDA: 背包窗口開關處理

        // 撿物
        constexpr DWORD PickupPacket_RVA   = 0x6297D0;  // ✅ CE: 0x11F97D0 (撿物封包建構)
    }
}

// ============================================================
// 封包結構（#pragma pack(1) 對齊）
// ============================================================
#pragma pack(push, 1)

// v7.6 攻擊封包 (66 bytes) - Wireshark 分析
// Offset 0-3=大小, 4-7=SessionID, 8-11=Opcode(1), 12-15=Subtype(0x3808), 16+=Payload
struct AttackPkt {
    DWORD dwSize    = 70;     // 4+4+4+4+54 = 70
    DWORD dwSession = 0;       // Session ID
    DWORD dwOpcode  = Offsets::Packets::ATTACK_OPCODE;  // 0x01
    DWORD dwSubType = Offsets::Packets::ATTACK_SUBTYPE; // 0x3808
    BYTE  payload[54];         // 剩餘 54 bytes
};

// v7.6 技能施放封包 (68 bytes) - DM 模式使用
struct SkillPkt {
    DWORD dwSize    = 64;
    DWORD dwSession = 0;
    DWORD dwOpcode  = Offsets::Packets::SKILL_OPCODE;  // 0x01
    DWORD dwSubType = Offsets::Packets::SKILL_SUBTYPE; // 0x3807
    BYTE  payload[52];
};

// NPCBuyPkt / NPCSellPkt 已刪除 - 使用 dm_visual.h 視覺模式替代

struct NPCTalkPkt {
    DWORD dwSize    = 12;
    DWORD dwMsgID   = Offsets::Packets::NPC_TALK;  // 13522
    DWORD npcGlobId = 0;
};

struct NPCClosePkt {
    DWORD dwSize  = 8;
    DWORD dwMsgID = Offsets::Packets::NPC_CLOSE;   // 13523
};

struct PickupItemPkt {
    DWORD dwSize   = 12;
    DWORD dwMsgID  = Offsets::Packets::PICKUP_ITEM; // 13351
    DWORD itemId   = 0;
};

struct PickupGoldPkt {
    DWORD dwSize   = 12;
    DWORD dwMsgID  = Offsets::Packets::PICKUP_GOLD;  // 13353
    DWORD goldId   = 0;
};

#pragma pack(pop)

// 封包結構尺寸斷言（編譯期驗證）
static_assert(sizeof(AttackPkt) == 70, "AttackPkt struct size");
static_assert(sizeof(SkillPkt) == 68, "SkillPkt struct size");     // 4+4+4+4+52=68
static_assert(sizeof(NPCTalkPkt) == 12, "NPCTalkPkt struct size");
static_assert(sizeof(NPCClosePkt) == 8, "NPCClosePkt struct size");
static_assert(sizeof(PickupItemPkt) == 12, "PickupItemPkt struct size");
static_assert(sizeof(PickupGoldPkt) == 12, "PickupGoldPkt struct size");
