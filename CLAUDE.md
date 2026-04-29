# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 專案概述

**JyTrainer** - Ran2 Online 外部 Trainer（非注入）。獨立進程透過 `FindWindow` + `OpenProcess` + `ReadProcessMemory` 讀取遊戲記憶體，ImGui + DirectX 9 渲染 UI。

## 編譯

| 腳本 | 用途 |
|------|------|
| `build\_build.bat` | Release Win32（需 vcvarsall） |
| `build\_build_quick.bat` | Release Win32（MSBuild 直接） |
| `build\_build_x64.bat` | x64 建置 |
| `build\_build_test.bat` | Test 模式 Win32 |

- 工具鏈：v143 (VS2022)
- 輸出：`../dist/JyTrainer.exe`（Win32）或 `../dist/win64/JyTrainer.exe`（x64）
- Win32：`WINVER=0x0A00`（Win10）；x64：`WINVER=0x0601`（Win7 相容）

## 執行緒模型

```
main()
  ├─ InitBotLogic()
  ├─ UIThread (CreateThread) → ImGui + DX9 視窗
  └─ BotThread (CreateThread) → Bot FSM 主迴圈（~20ms tick）
```

**線程間同步**：`BotConfig` 使用 `CRITICAL_SECTION cs_protected`。

## 架構（需讀多個檔案理解）

### BotTick 主迴圈 → FSM 架構

```
BotTick()
  ├── ValidateBotPreconditions()  # 前置檢查（卡密、遊戲就緒）
  ├── CheckSecurityCodeWindowOnce()  # 安全碼（只檢查一次）
  └── StateHandlerRegistry::Get(currentState)->Tick()
```

**檔案對應關係**：
- `src/core/bot_logic.cpp` — BotTick 主迴圈、RaII 防重入 `ReentryGuard`
- `src/core/fsm/state_machine.h` — 通用 StateMachine 框架（帶 Watchdog）
- `src/core/fsm/state_handler.h` — `IStateHandler` 介面、`WatchdogTimer`
- `src/core/fsm/state_handler.cpp` — 各狀態 Handler 實作（定義於此）
- `src/core/fsm/fsm_integration.cpp` — FSM 初始化整合
- `src/core/fsm/bot_tick_simplified.h` — 簡化版 BotTick 邏輯（供 Handler 呼叫）

### 狀態 FSM

```
IDLE → HUNTING → DEAD → RETURNING → TOWN_SUPPLY → BACK_TO_FIELD → HUNTING
         ↑
    TRAVELING ────────────────────────────────┘
    PAUSED (安全碼偵測時強制暫停)
```

**超時設計**（每個 Handler 自帶 `WatchdogTimer`）：
| 狀態 | 超時 | 觸發 |
|------|------|------|
| IDLE | 30s | — |
| HUNTING | 30s | → RANDOM_EVADE |
| DEAD | 60s | → IDLE |
| RETURNING | 90s | → TOWN_SUPPLY |
| TOWN_SUPPLY | 5min | → BACK_TO_FIELD |
| BACK_TO_FIELD | 2min | → HUNTING |
| RECOVERY | 30s | → 重試 5 次 → IDLE（不停機） |
| RANDOM_EVADE | 15s | → HUNTING |

**TOWN_SUPPLY 子狀態** (`s_supplyPhase`)：Phase 0~4（找 NPC → 買藥水 → 賣垃圾 → 寵物 → 完成）

### 視覺系統（Vision Fusion）

自動選擇後端：
1. **DM Plugin**（Primary）：大漠插件後台鍵鼠 + 圖檔辨識
2. **VLM Vision**（Fallback）：NVIDIA DeepSeek V4 Pro API
3. **RPM**（最終 fallback）

**相關檔案**：
- `src/vision/vision_fusion.cpp` — 融合層（DM + VLM）
- `src/vision/dm_visual.cpp` — 大漠插件主介面
- `src/vision/dm_visual_supply.cpp` — 城鎮補給視覺
- `src/vision/vlm_vision_api.cpp` — DeepSeek V4 API
- `src/vision/screenshot_*.cpp` — 多種截圖方案

**截圖方案優先序**：PrintWindow (PW_RENDERFULLCONTENT=3) → GDI BitBlt

### 配置檔案

- `offsets.ini` — 偏移配置（明文）
- `offsets.dat` — 偏移配置（AES-256-CBC 加密）
- `models/monster_detection.onnx` — YOLO 怪物偵測模型

### 實體讀取策略

1. **優先**：`Local\RanBot_NetHook` 共享記憶體（NetHook）
2. **DM 模式**：`use_dm_mode=true`
3. **視覺模式**：`use_visual_mode=true`
4. **記憶體模式**：RPM

## 偏移配置系統

**載入順序**：`offsets.dat` (AES-256-CBC) → `offsets.ini` (明文) → 內建預設值（`offsets.h`）

**關鍵 RVA**：
| 符號 | RVA | 說明 |
|------|-----|------|
| GLCharacter_Obj | `0x92F19C` | 角色物件 |
| HP/MP/SP | `0x92F2F8~0x92F30C` | 屬性 |
| PosX/PosZ | `0x930DF8~0x930DFC` | 座標（float） |
| MsgProcess_RVA | `0x6C7340` | 訊息處理 |
| PacketSend_RVA | `0x58B590` | 封包發送 |

## 封包結構

| 封包 | MsgID | 大小 |
|------|-------|------|
| ATTACK | 0x3A22 | 70 |
| SKILL | 0x3807 | 68 |
| NPC_BUY | 13559 | 35 |
| NPC_SELL | 13560 | 33 |

## 反PK（Anti-PK）

**配置**（`bot_logic.h`）：
| 參數 | 預設值 | 說明 |
|------|--------|------|
| `anti_pk_enable` | `true` | 啟用/停用 |
| `anti_pk_cooldown_sec` | `300` | 逃生後冷卻時間（秒） |

**偵測方式**（`state_handler.cpp`）：
1. **視覺偵測**：識別紅名圖片（`Player_red_name.png`、`Player_red_name2.png`、`pk_alert.png`）
2. **記憶體偵測**：血量瞬間下降 >40 視為被攻擊

**觸發動作**：
1. 按 `S` 起點卡回城
2. 進入 `IDLE` 狀態
3. 冷卻 5 分鐘後按 `D` 前點返回練功點，繼續 `HUNTING`

**相關檔案**：`src/core/fsm/state_handler.cpp`（`IsPlayerAttackingMe()`）

## 熱鍵

| 熱鍵 | 功能 |
|------|------|
| `F10` | 切換 GUI |
| `F11` | 暫停 Bot（從 PAUSED 恢復） |
| `F12` | 緊急停止 |

## YOLO 怪物偵測模型更新

```bash
# 1. 訓練腳本位置
tools/yolo_scripts/

# 2. 模型更新流程
# - 從 GitHub 下載新模型：models/monster_detection.onnx
# - 最新 commit: b4a4d24 feat: update YOLO monster detection model
```

## 已知限制

1. **EntityPool TLS 依賴**：外部無法直接讀取，改用 NetHook
2. **MapID 讀取失敗**：RETURNING 改用坐標移動判斷
3. **DirectX 黑屏**：禁用 `AttachToWindow`，使用 `SetForegroundWindow`

## 常見問題排查

### baseAddr=0x00000000
1. 以管理員身份執行
2. 檢查 LogGame 日誌 `[偵測]` 模組列表
3. 確認遊戲進程名稱在 `IsKnownGameExeNameW` 列表中

## 待處理（TODO）

| 檔案 | 問題 |
|------|------|
| `input_sender.cpp:556` | Win7 RClickFast 測試待驗證 |

## 已清理

### 刪除的檔案
| 檔案 | 原因 |
|------|------|
| `vision/screenshot_unified.cpp/.h` | 與 `screenshot_universal.cpp` 完全重複 |
| `vision/screenshot_assist_compat.cpp/.h` | 空殼 Stub，純轉發已刪除檔案 |
| `vision/obs_capture.cpp/.h` | 從未使用，`CaptureFrame()` 永遠返回失敗 |
| `vision/vision_fusion.cpp` | 與 `vision_fusion_new.cpp` 完全重複，`_new` 才是實際使用的版本 |
| `gui/game_overlay.cpp/.h` | `.cpp` 被註解掉，`.h` 無任何編譯產出 |
| `platform/wow64_compat.h` | 162 行，無任何原始碼引用（Header-only） |
| `embed/embedded_vision_images.h` | 原本不存在，導致編譯失敗 |

### 修復的 Bug
- `dm_visual.cpp` — 靜態變數 Shadowing（namespace 內外各宣告一份）
- `dm_visual.cpp` — `GetBotConfig()` 引用介面不一致（`BotConfig&` 實際返回 `BotConfig*`）
- `embedded_anti_pk_images.h:3225` — 陣列初始化缺少逗號
- `dm_plugin.cpp` — 缺少 `#include <shlwapi.h>`
- `dm_visual.cpp` — `GetCachedPlayerStateRaw()` 缺少 extern 宣告
- `bot_logic.h` — `GetCachedPlayerStateRaw()` 未 export

### 清理的程式碼
- `dm_visual.h/.cpp` — 移除 `DM_VISUAL_IMAGE_PATH` 註解
- `bot_logic.cpp` — 移除對已刪除 `screenshot_assist_compat.h` 的引用和呼叫
- `utils.h` — 移除空的 `CoordUtil` namespace（已遷移到 `coord_calib.h`）
- `main.cpp` — 移除 `embed_dlls.h` 引用和 `LoadEmbeddedDll()` ONNX 載入邏輯
- `resource.h` — 移除 `IDR_ONNXRUNTIME_*` 定義
- `embedded_resources.h` — 移除 `IDR_ONNXRUNTIME_*` 定義