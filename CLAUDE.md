# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 專案概述

**JyTrainer** - Ran2 Online 外部 Trainer（獨立進程，非注入）。透過 `FindWindow` + `OpenProcess` + `ReadProcessMemory` 讀取遊戲記憶體，ImGui + DirectX 9 渲染 UI。

## 編譯

```batch
_build.bat              # Release x86 建置
_build_quick.bat        # 使用 vcvarsall x86（環境變數版）
_build_license_admin.bat # 授權管理工具
```

- 工具鏈：v142 (VS2022)
- 輸出：`../dist/JyTrainer.exe`（靜態 CRT）
- 建置變數：`WINVER=0x0601`（Win7 相容）

## 執行緒模型

```
main()
  ├─ InitBotLogic()
  ├─ UIThread (CreateThread)  → ImGui + DX9 視窗
  └─ BotThread (CreateThread) → Bot FSM 主迴圈（~20ms tick）
```

**線程間同步**：
- `BotConfig` 使用 `CRITICAL_SECTION cs_protected` 保護（禁用拷貝/移動）
- `PlayerState` UI 快取使用 CriticalSection
- 避免在持有鎖的同時做 RPM（會阻塞 UI）

## 核心模組

| 檔案 | 職責 |
|------|------|
| `main.cpp` | 執行緒建立、D3D9 初始化、訊息分發 |
| `bot_logic.cpp/h` | Bot FSM (`BotTick`)、實體掃描、`SupplyTick`、背包讀取 |
| `game_process.cpp/h` | FindWindow、OpenProcess、GameBase 位址 |
| `memory_reader.cpp/h` | NT API RPM 包裝（`SafeNtRPM`） |
| `input_sender.cpp/h` | PostMessage/SendInput 輸入模擬 |
| `gui_ranbot.cpp/h` | ImGui DX9 UI 渲染 |
| `offsets.h` | 遊戲偏移（RVA 格式，+GameBase 使用） |
| `offset_config.cpp/h` | 偏移配置管理（.dat/.ini 載入） |
| `config_updater.cpp/h` | 遠端配置更新 |
| `attack_packet.cpp/h` | 攻擊封包發送器（Winsock）|
| `nethook_shmem.cpp/h` | NetHook 共享記憶體客戶端 (`Local\RanBot_NetHook`) |
| `coords.h` | 座標定義（復活點、NPC 等）|
| `visionentity.cpp/h` | 視覺實體掃描（像素血條）|
| `visionentity.cpp/h` | 視覺實體掃描（像素掃描取代記憶體讀取）|
| `coord_calib.cpp/h` | 座標校正 |
| `target_lock.cpp/h` | 目標鎖定 |
| `screenshot.cpp/h` | 螢幕截圖功能 |
| `screenshot_assist.cpp/h` | 截圖輔助工具 |
| `offline_license.cpp/h` | 離線授權管理 |
| `license_admin.cpp/h` | 授權管理工具（獨立建置）|
| `kami_client.cpp/h` | 卡密驗證客戶端 |

## Bot FSM

```
IDLE → HUNTING → DEAD → RETURNING → TOWN_SUPPLY → BACK_TO_FIELD → HUNTING
         ↑
    TRAVELING ────────────────────────────────┘
    PAUSED (安全碼偵測時強制暫停)
```

### 主要狀態

| 狀態 | 說明 |
|------|------|
| `IDLE` | 閒置，未開始 |
| `HUNTING` | 戰鬥中，掃怪+攻擊 |
| `DEAD` | 死亡，檢測復活選單 |
| `RETURNING` | 返回城鎮（按起點卡）|
| `TOWN_SUPPLY` | 城鎮補給（由 `s_supplyPhase` 0-4 控制子流程）|
| `BACK_TO_FIELD` | 返回野外（按前點卡）|
| `TRAVELING` | [未實現] 移動中 |
| `PAUSED` | 安全碼偵測時強制暫停 |

### TOWN_SUPPLY 子狀態 (`s_supplyPhase`)

```
Phase 0: 找 NPC → 對話 → Phase 1
Phase 1: 買藥水 → Phase 2 或 Skip
Phase 2: 賣垃圾 → Phase 3 或 Skip
Phase 3: 寵物餵食（可選）→ Phase 4
Phase 4: 完成，關閉對話，恢復 HUNTING
```

## 戰鬥意向 (`CombatIntent`)

Bot FSM 內部的戰鬥狀態機：

```
SEEKING (0) → ENGAGING (1) → LOOTING (2) → SEEKING
     ↑                                            ↓
     └────────────────────────────────────────────┘
```

- **SEEKING**: 尋找目標中
- **ENGAGING**: 已在攻擊範圍，施放技能中
- **LOOTING**: 目標死亡，等待撿物品

## 實體讀取策略

1. **優先**：NetHook 共享記憶體 (`Local\RanBot_NetHook`) - 從 recv() hook 讀取
2. **視覺模式**：`use_visual_mode=true` 時使用 visionentity.cpp 像素掃描血條
3. **已知限制**：EntityPool TLS 依賴，外部無法直接讀取

## 實體結構 (`Entity`)

```cpp
struct Entity {
    DWORD id = 0;
    int type = 0;      // 2=怪物, 1=NPC, 0=玩家
    float x, z, y;     // 座標
    float dist = 99999.0f;  // 與玩家距離
    int hp = 0, maxHp = 1;  // 怪物 HP（預判死亡）
    bool isDead = false;
};
```

## 視覺模式 (`use_visual_mode`)

以視覺辨識為主，取代記憶體讀取的備用方案：
- 像素掃描血條顏色
- 螢幕座標轉換為遊戲座標
- `visual_scan_timeout_ms`: 掃描超時（預設 100ms）

## 偏移配置系統

### 載入順序

1. `offsets.dat` (AES-256-CBC 加密格式)
2. `offsets.ini` (明文格式，降級備用)
3. `corrected_offsets.ini` (修正版本)
4. 內建預設值（offsets.h）

### 加密格式

```
[Magic: 4 bytes] "JYOF"
[Version: 2 bytes] 0x0001
[Flags: 2 bytes] reserved
[IV: 16 bytes] AES CBC IV
[DataSize: 4 bytes] 明文大小
[EncryptedData: N bytes] AES-256-CBC + PKCS7
[Checksum: 4 bytes] CRC32
```

### 使用方式

使用 `OffsetConfig::` 命名空間的全域 getter 函式，而非直接使用 `Offsets::`（編譯期常量）。

```cpp
DWORD hp = SafeRPM<DWORD>(gh.hProcess, gh.baseAddr + OffsetConfig::PlayerHP());
```

### 偏移格式說明

- **RVA (Relative Virtual Address)**：相對於模組載入基底的偏移
- **地址公式**：`實際地址 = GameBase + RVA`
- **IDA → CE 轉換**：`CE = IDA_RVA + 0xBD0000`
- **IDA 基址**：0x400000
- **CE 基址**：0xFD0000（實際載入位置）

## 授權系統

**離線一卡一機** - 不需要 VPS/API/資料庫，根據玩家 HWID 發卡。

### 核心檔案

| 檔案 | 用途 |
|------|------|
| `license_public.blob` | 放在 JyTrainer.exe 同目錄 |
| `license_private.blob` | 留在發卡機，**不要發給玩家** |
| `license.dat` | 玩家本地授權緩存（驗證後自動生成）|

### 指令

```batch
license_admin.exe genkey .                    # 產生金鑰對
license_admin.exe issue <私鑰> <HWID> 30 basic  # 發 30 天卡
license_admin.exe issue <私鑰> <HWID> 7 basic   # 發 7 天卡
```

### 變數

| 變數 | 說明 |
|------|------|
| `g_licenseValid` | 離線授權狀態（`true`=已授權）|

## 熱鍵

| 熱鍵 | 功能 |
|------|------|
| `F10` | 切換 GUI 顯示/隱藏 |
| `F11` | 暫停/繼續 Bot |
| `F12` | 停止 Bot |

### 戰鬥熱鍵

| 熱鍵 | 功能 |
|------|------|
| `Q` | HP 藥水 |
| `W` | MP 藥水 |
| `E` | SP 藥水 |
| `Z` | 攻擊 |
| `Space` | 撿物 / NPC 對話 |
| `1-0` | 攻擊技能選擇 |
| `F1` | 攻擊欄切換 |
| `F2` | 輔助欄切換 |

### 移動熱鍵

| 熱鍵 | 功能 |
|------|------|
| `S` | 起點卡（回城）|
| `D` | 前點卡（返回野外）|
| `↑↓←→` | 方向鍵移動 |

## 座標系統

- 所有 UI 座標使用 **相對座標** (0-1000)，執行時 `ToAbsolute()` 轉為實際像素
- 基準解析度：1024x768
- Win7/Win11 自動切換：`IsWin11()` 函式檢測作業系統

## 攻擊圓圈座標

定義於 `bot_logic.cpp` 頂部：

```cpp
static const int ATTACK_CENTER_X = IsWin7Platform() ? 520 : 500;
static const int ATTACK_CENTER_Y = IsWin7Platform() ? 390 : 370;
static const int ATTACK_TARGET_RADIUS = 260;
```

## 記憶體偏移（已驗證 2026-04-18）

### 地址翻譯
- IDA 基址：0x400000
- CE 基址：0xFD0000（= IDA + 0xBD0000）
- GameBase 動態：`GetModuleHandle("Game.exe")`

### 玩家屬性 (Player)

| 符號 | RVA | 說明 |
|------|-----|------|
| GLCharacter_Obj | `0x92F19C` | GLCharacter 物件（CE 驗證） |
| HP | `0x92F2F8` | HP (DWORD) |
| MaxHP | `0x92F2FC` | 最大 HP (DWORD) |
| MP | `0x92F300` | MP (DWORD) |
| MaxMP | `0x92F304` | 最大 MP (DWORD) |
| SP | `0x92F308` | SP (DWORD) |
| MaxSP | `0x92F30C` | 最大 SP (DWORD) |
| Gold | `0x92F248` | 金幣 (DWORD) |
| Level | `0x92F240` | 等級 (DWORD) |
| EXP | `0x92F2D0` | 當前經驗 (DWORD) |
| EXPMax | `0x92F2D8` | 升級所需經驗 (DWORD) |
| MapID | `0x930DEC` | 地圖 ID (DWORD) |
| PosX | `0x930DF8` | X 座標 (float) |
| PosZ | `0x930DFC` | Z 座標 (float) |
| PosY | `0x930E02` | Y 座標 (float) |
| CombatPower | `0x9311F8` | 戰鬥力 (DWORD) |
| STR/VIT/SPR/DEX/END | `0x8FEAD0~0x8FEAE4` | 角色屬性 |
| PhysAtkMin/Max | `0x932120~0x932124` | 物理攻擊力 |
| SprAtkMin/Max | `0x93212C~0x932130` | 精神攻擊力 |

### 背包 (Inventory)

| 符號 | RVA | 說明 |
|------|-----|------|
| InvPtr | `+0x15C` | 背包池指標（相對於 GLCharacter_Obj）|
| InvCount | `+0x642C` | 背包物品數量 |
| ItemStride | 16 bytes | 每格大小 |

### 快捷欄 (QuickSlot)

| 符號 | RVA | 說明 |
|------|-----|------|
| ArrowCount | `0x92F8D0` | 箭矢數量 |
| TalismanCount | `0x92F8D4` | 符咒數量 |

### 遊戲時間 (GameTime)

| 符號 | RVA | 說明 |
|------|-----|------|
| Hour | `0x9016A8` | 小時 (0-23) |
| Minute | `0x94C5F0` | 分鐘 (0-59) |

### 鎖定目標 (Target)

| 符號 | RVA | 說明 |
|------|-----|------|
| HasTarget | `0x93275C` | 是否有目標 (DWORD) |
| ID | `0x931D90` | 目標 ID (DWORD) |
| LockedState | `0x930D28` | 鎖定狀態 (DWORD) |

### 實體池 (EntityPool) - IDA 驗證

| 符號 | RVA | 說明 |
|------|-----|------|
| LandMan_CROWList | `+0xA790` | 怪物鏈表偏移 |
| Crow::HP | `+0x7B0` | 怪物 HP |
| Crow::PosX/PosY/PosZ | `+0x890~+0x898` | 怪物座標 |
| Crow::ServerID | `+0x91C` | 伺服器 ID |

### 函數 RVA (Functions)

| 符號 | RVA | 說明 |
|------|-----|------|
| MsgProcess_RVA | `0x6C7340` | 訊息處理 |
| FrameMove_RVA | `0x595510` | 框架更新 |
| NetClient_RVA | `0x58B6D0` | 取得 NetClient |
| PacketSend_RVA | `0x58B590` | 封包發送 |
| NPCBuySell_RVA | `0x636480` | NPC 買/賣處理 |
| PickupPacket_RVA | `0x6297D0` | 撿物封包建構 |

## 封包結構

定義於 `offsets.h`，使用 `#pragma pack(push, 1)` 確保 1 位元組對齊：

| 封包 | 大小 | MsgID/Subtype |
|------|------|---------------|
| `AttackPkt` | 70 bytes | Opcode=1, Subtype=0x3808 |
| `SkillPkt` | 68 bytes | Opcode=1, Subtype=0x3807 |
| `NPCBuyPkt` | 35 bytes | MsgID=13559 |
| `NPCSellPkt` | 33 bytes | MsgID=13560 |
| `NPCTalkPkt` | 12 bytes | MsgID=13522 |
| `NPCClosePkt` | 8 bytes | MsgID=13523 |
| `PickupItemPkt` | 12 bytes | MsgID=13351 |
| `PickupGoldPkt` | 12 bytes | MsgID=13353 |

## 技能系統

### F1 攻擊欄 / F2 輔助欄

- `attackSkillCount`: 使用的攻擊技能數量（1~10）
- `attackSkillInterval`: 攻擊技能間隔 (ms)
- `buffEnabled`: 是否啟用輔助技能
- `buffSkillCount`: 使用的輔助技能數量（1~5）
- `buffCastInterval`: 輔助施放間隔（秒）

### 技能熱鍵

- `attackBarKey`: 攻擊欄切換鍵（預設 VK_F1）
- `buffBarKey`: 輔助欄切換鍵（預設 VK_F2）
- `attackSkillInterval`: 右鍵點擊延遲 (ms)

## 安全碼防偵測

`CheckSecurityCodeWindow()` 偵測「轉轉樂」或「抽獎」視窗，自動將 Bot 狀態設為 `PAUSED`。

```cpp
HWND h1 = FindWindowW(NULL, L"抽獎轉轉樂");
HWND h2 = FindWindowW(NULL, L"轉轉樂安全碼");
```

## 高級功能

### Waypoint 巡航
- `waypoint_patrol`: 啟用 Waypoint 巡航
- `waypoint_radius`: 到達 Waypoint 半徑（格）
- `huntWaypoints`: 煉功點座標清單

### 敵人接近偵測
- `enemy_approach_detect`: 啟閉開關
- `enemy_approach_speed`: 速度閾值（distance/sec）
- `enemy_approach_dist`: 偵測距離上限（格）
- `enemy_approach_trigger`: 0=回城, 1=暫停

### 防呆自動移動
- `anti_stuck_enable`: 啟用防呆自動移動
- `anti_stuck_interval_sec`: 移動間隔（秒）
- `anti_stuck_offset`: 偏移距離（格）
- `anti_stuck_move_ms`: 移動持續時間（毫秒）

### 自動遊戲時間傳送
- `auto_game_time`: 啟用開關
- `game_time_hour/game_time_min`: 回城時間
- `auto_return_to_field`: 是否自動返回野外
- `game_time_return_hour/game_time_return_min`: 返回野外時間

### 背包保護系統
背包 78 格 = 13 列 x 6 欄

| 保護方式 | 說明 |
|---------|------|
| **列保護** | 預設保護第 1-3 列（slot 0-17），存放藥水/消耗品 |
| `protected_rows[row]` | `true` = 該列不賣 |
| **物品 ID 保護** | 補充保護特定貴重物品 |
| `protected_item_ids` | 物品 ID 清單（需 CE 掃描填充）|

貴重物品建議放置位置：
- **第 1-3 列**：藥水、消耗品（自動保護）
- **第 4+ 列**：傳送卡、強化材料（需手動加入 ID 保護）

## 已知限制

1. **EntityPool TLS 依賴**：外部無法直接讀取，改用 NetHook 共享記憶體
2. **保護區域**：0xD10XXX 區域為遊戲保護區域，外部 RPM 返回失敗
3. **Win7/Win11 座標差異**：NPC/商店座標需分別校準
4. **GameBase 動態變化**：ASLR 可能改變載入基底，但 RVA 不變

## 關鍵配置變數

| 變數 | 說明 |
|------|------|
| `g_cfg.active` | Bot 啟用開關 |
| `g_cfg.use_visual_mode` | 視覺模式開關 |
| `g_cfg.waypoint_patrol` | Waypoint 巡航開關 |
| `g_cfg.auto_supply` | 自動補給開關 |
| `g_cfg.auto_pickup` | 自動撿物開關 |
| `g_licenseValid` | 離線授權狀態 |
| `s_supplyPhase` | 補給子狀態 (0-4) |
| `s_combatIntent` | 戰鬥意向 (0-2) |
| `s_currentSkillIndex` | 當前技能索引 |
| `s_pausedPreviousState` | PAUSED 恢復時的前一狀態 |