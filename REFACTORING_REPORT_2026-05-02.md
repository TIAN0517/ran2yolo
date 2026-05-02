# JyTrainer_Win11 代碼清理重構報告

**日期**: 2026-05-02
**操作**: 倉庫歷史清理 + 代碼重構

---

## 一、倉庫清理原因

### 1.1 歷史積累問題
- 累積大量零散的小 commit，歷史混亂
- 部分舊功能（如 DLL 注入、YOLO 腳本）已過時
- CE_MCP、scripts 目錄的廢棄文件

### 1.2 刪除的廢棄文件
```
CE_MCP/ce_mcp_bridge.lua      # 已棄用
CE_MCP/responses.txt           # 廢棄
CE_MCP/validate_packets.lua    # 廢棄
scripts/ce_bridge.lua          # 廢棄
src/core/recovery_vision.cpp   # 視覺恢復已停用
src/core/recovery_vision.h    # 視覺恢復已停用
tools/yolo_scripts/*           # YOLO 腳本已移至獨立專案
tools/license_admin/*          # License Admin 已重構
vision_fusion.cpp              # 重複文件
app.aps                        # 建置臨時文件
build_now.ps1                  # 廢棄腳本
```

---

## 二、本次代碼修復

### 2.1 CRITICAL 修復

| 檔案 | 問題 | 修復 |
|------|------|------|
| `pattern_scanner.cpp:41-43` | namespace 語法錯誤 | 變數移到 namespace 內 |
| `offset_config.cpp:343-354` | Inventory 解析重複賦值 | 移除重複 `s_InvItemPtr` |
| `offset_config.cpp:410` | `EntityLandManPtr=0x38` 衝突 | 統一為 `0x24` |
| `attack_packet.h:91` | NPC_TALK=`0x34D6` 衝突 | 改為 `0x34D2` (IDA+CE) |
| `packet_offsets.h:24` | MOVE=`0x3435` 衝突 | 改為 `0x3424` (IDA) |
| `packet_offsets.h:25` | NPC_TALK=`0x34D6` 衝突 | 改為 `0x34D2` (IDA+CE) |

### 2.2 HIGH 修復

| 檔案 | 問題 | 修復 |
|------|------|------|
| `offset_config.cpp` | ResetToDefaults 缺少變數 | 補加 GLCharClientPtr, InvItemId, SlotStride |
| `bot_logic.cpp:2190-2191` | TransitionState Bug | 修復比較邏輯 `oldState == s_lastTransitionState` |

### 2.3 封包 ID 統一

統一使用 `offsets.h`（IDA+CE 驗證）的值：

| 封包 | 正確值 | 驗證來源 |
|------|--------|----------|
| NPC_TALK | 0x34D2 (13522) | IDA Pro + CE |
| MOVE | 0x3424 (13348) | IDA Pro |
| ATTACK | 0x3A22 (14882) | Wireshark |

---

## 三、核心偏移修復

### 3.1 EntityLandManPtr 值衝突修復

```
問題: 初始化使用 0x24，ResetToDefaults() 使用 0x38

分析:
- offsets.h 指標鏈: GLChar(+0x24) → GLLandManClient(+0xA790)
- ResetToDefaults() 錯誤使用 0x38

修復: 統一使用 0x24
```

### 3.2 Inventory 偏移修復

```
問題:
1. s_InvItemPtr 被解析兩次（行 344 和 348）
2. s_GLCharClientPtr 無 INI key 支援
3. ResetToDefaults() 缺少 Inventory 變數

修復:
1. 移除重複的 s_InvItemPtr 解析
2. 添加缺失的 GLCharClientPtr, InvItemId, SlotStride 重置
```

---

## 四、戰鬥意向不動作根因

### 4.1 TransitionState Bug

```cpp
// 修復前（錯誤）:
if (s_lastTransitionState == nextState)  // 永遠 false

// 修復後（正確）:
if (s_lastTransitionState == oldState)   // 比較舊狀態
```

### 4.2 封包 ID 衝突導致攻擊失效

NPC_TALK 和 MOVE 封包 ID 在不同檔案有不同的值，導致：
- NPC 對話失敗
- 移動封包發送錯誤地址

---

## 五、倉庫清理後結構

```
JyTrainer_Win11/
├── main.cpp              # 入口點
├── JyTrainer.vcxproj    # VS 專案
├── CLAUDE.md            # 專案文檔
├── PACKET_LISTENER_README.md  # 封包監聽器文檔
├── offsets.ini          # 外部偏移配置
├── packet_listener.py   # Python UDP 封包監聽器
├── build/               # 建置腳本
├── src/
│   ├── core/            # Bot 大腦
│   ├── game/            # 遊戲進程管理
│   ├── input/           # 輸入發送
│   ├── vision/          # 視覺融合
│   ├── gui/             # UI
│   ├── config/          # 偏移配置
│   ├── embed/           # 大漠插件
│   ├── license/         # 授權系統
│   └── platform/        # 平台工具
└── dist/                # 編譯輸出
```

---

## 六、版本資訊

| 項目 | 值 |
|------|-----|
| 工具鏈 | v143 (VS2022) |
| 架構 | Win32 / x64 |
| 平臺 | Windows 11 Pro |
| 遊戲 | Ran2 Online |

---

## 七、已知需清理的死代碼（未處理）

以下代碼已識別但本次未處理：

| 檔案 | 問題 | 影響 |
|------|------|------|
| `game_process.cpp:319-327` | IsAlternateGameExeNameW/A 返回 false | 低 |
| `dm_visual.cpp` | 視覺模式已禁用 | 低 |
| `vision_api_rotator.cpp` | API keys 為空 | 低 |

---

## 八、commit 歷史清理

本次操作：
1. 刪除所有舊 commit 歷史
2. 保留當前工作區狀態為第一個新 commit
3. 強制推送到 origin/main

**注意**: 此操作會永久刪除以下 commit 歷史：
```
e88fd93 fix: 修正狀態機 STOP 回應 IDLE 的防護衝突
948fce9 fix: 調整座標面板高度填補空白
b93fed9 fix: 更新偏移配置和 PostMessage 輸入方式
... (共 50+ commits)
```

---

## 九、測試建議

1. **編譯測試**
   ```batch
   build\_build.bat
   ```

2. **功能測試**
   - [ ] Bot 啟動與停止
   - [ ] HUNTING 狀態戰鬥意向
   - [ ] NPC 對話功能
   - [ ] 自動喝水/技能
   - [ ] 偏移驗證

3. **回滾方案**
   ```bash
   # 如果需要回滾到清理前（假設還有 local 備份）
   git reflog
   git reset --hard <commit-hash>
   ```

---

## 十、作者

- Claude Code (claude.ai/code)
- 2026-05-02
