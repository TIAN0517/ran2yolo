# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 專案概述

**JyTrainer_Win11** - Ran2 Online 外部 Trainer（非注入）。獨立進程透過 `FindWindow` + `OpenProcess` + `ReadProcessMemory` 讀取遊戲記憶體，ImGui + DirectX 9 渲染 UI。

## 編譯

```batch
:: Win32 Release
build\_build.bat

:: x64 Release
build\_build_x64.bat

:: 直接使用 MSBuild
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" JyTrainer.vcxproj /p:Configuration=Release /p:Platform=Win32 /v:m
```

- 工具鏈：v143 (VS2022)
- 輸出：`../dist/win32/JyTrainer.exe` 或 `../dist/win64/JyTrainer.exe`

## 執行緒模型

```
main()
  ├─ InitBotLogic()
  ├─ UIThread → ImGui + DX9 視窗 (PeekMessage/WndProc)
  └─ BotThread → BotTick() (~20ms tick)
```

**線程間同步**：`BotConfig` 使用 `CRITICAL_SECTION cs_protected`。

## 核心模組

| 目錄 | 核心檔案 | 職責 |
|------|----------|------|
| `main.cpp` | 入口點 | 初始化、訊息迴圈、BotThread 建立 |
| `src/core/` | `bot_logic.cpp` | BotTick 主迴圈、狀態 FSM、BotConfig |
| `src/game/` | `game_process.cpp` | FindWindow、OpenProcess、GameHandle 管理 |
| `src/game/` | `memory_reader.cpp` | SafeRPM、NT API 讀取 |
| `src/input/` | `input_sender.cpp` | PostMessage/SendInput 輸入 |
| `src/vision/` | `vision_fusion.cpp` | 視覺融合（DM + VLM） |
| `src/gui/` | `gui_ranbot.cpp` | ImGui + DX9 主介面 |

## BotTick 主迴圈

`src/core/bot_logic.cpp` — BotTick 是核心，每幀調用

```
BotTick()
  ├── 許可證檢查
  ├── 讀取 PlayerState (RPM)
  ├── 安全碼偵測 (VisionFusion::CheckSecurityCode)
  ├── 狀態分支 (IDLE/HUNTING/DEAD/TOWN_SUPPLY/...)
  └── 自動喝水/攻擊/撿物
```

### 狀態 FSM

```
IDLE → HUNTING → DEAD → RETURNING → TOWN_SUPPLY → BACK_TO_FIELD
         ↑                                          ↓
    PAUSED (安全碼偵測時) ←────────────────────── RECOVERY
```

## 記憶體偏移架構

所有偏移為 **RVA**（相對虛擬地址）：
```
實際地址 = GameBase + RVA
GameBase = GetModuleHandle("Game.exe")
```

**驗證工具**：`OffsetValidator` 命名空間 — 運行時自動驗證

**外部偏移檔**：`offsets.ini`（exe 同層）→ 找不到時使用 `src/config/offsets.h`

## 座標讀取方案

當 RPM 無法讀取玩家座標時（記憶體保護），使用 Python 獨立監聽器：

| 檔案 | 說明 |
|------|------|
| `packet_listener.py` | Python UDP 封包監聽器（需 Npcap + Admin） |
| `packet_listener_setup.py` | 環境檢查腳本 |
| `PACKET_LISTENER_README.md` | 完整文件 |

## 熱鍵

| 熱鍵 | 功能 |
|------|------|
| F9 | 量尺座標抓取 |
| F10 | 切換 GUI |
| F11 | 暫停 Bot |
| F12 | 緊急停止 |

熱鍵由 `WH_KEYBOARD_LL` 鉤子處理（`gui_ranbot.cpp`）

## 關鍵資料結構

### BotState 列舉
- `IDLE` (0), `HUNTING` (1), `DEAD` (2), `RETURNING` (3), `TOWN_SUPPLY` (4), `BACK_TO_FIELD` (5), `PAUSED` (40), `RECOVERY` (41), `EMERGENCY_STOP` (99)

### PlayerState
角色屬性：HP/MP/SP/Gold/Level/EXP、STR/VIT/SPR/DEX/END、座標、地圖ID

### Entity
實體結構：id/type/position/hp/maxHp/isDead（用於怪物血量預判死亡）

### BotConfig
完整設定：喝水閾值、回城條件、技能設定（1~5 攻擊 / 6~0 輔助）、Waypoint

## 常見問題

**baseAddr=0x00000000**：
1. 以管理員身份執行
2. 檢查 LogGame 日誌 `[偵測]` 模組列表
3. 確認遊戲進程名稱在 `IsKnownGameExeNameW` 列表中

## 已知需清理的代碼

以下代碼已識別為死代碼或需審查：

1. **`game_process.cpp:319-327`** - `IsAlternateGameExeNameW/A` 永遠返回 false
2. **`game_process.cpp:924-928`** - `Real_LoadLibraryA` 定義但未使用
3. **`vision_fusion.cpp:72-78`** - 註釋掉的大漠初始化代碼
4. **`bot_logic.h:66-67`** - 註釋掉的 `BotThread` 文檔（重複內容）

## 開發注意事項

- **命名規範**：`g_` 前綴表示全域變數，`s_` 前綴表示靜態變數
- **錯誤處理**：使用 `LogGame()` 寫入 `RanBot_Trainer.log`
- **記憶體安全**：`GameHandle` 使用 `CRITICAL_SECTION` 保護
- **偏移驗證**：使用 CE MCP 或 IDA Pro MCP 驗證偏移
