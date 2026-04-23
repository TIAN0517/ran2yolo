#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>
#include <atomic>

// ============================================================
// Bot 狀態機（完整 FSM）
// ============================================================
enum class BotState {
    IDLE           = 0,  // 閒置
    HUNTING        = 1,  // 戰鬥中
    DEAD           = 2,  // 死亡
    RETURNING      = 3,  // 返回城鎮（按下起點卡）
    TRAVELING      = 4,  // [未實現] 移動中（走路到定點）
    TOWN_SUPPLY    = 5,  // 城鎮補給（總協調，內部使用 s_supplyPhase 0-4）
    // （已刪除 SUPPLY_NPC_WALK~SUPPLY_CLOSE — 死代碼，實際使用 int s_supplyPhase）
    BACK_TO_FIELD  = 20, // 返回野外（按下前點卡）
    PAUSED        = 40, // 轉轉樂安全碼已暫停
};

// ============================================================
// 實體結構（含 HP，用於預判死亡）
// ============================================================
struct Entity {
    DWORD id    = 0;
    int   type  = 0;       // 2=怪物, 1=NPC, 0=玩家
    float x     = 0.0f;
    float z     = 0.0f;
    float y     = 0.0f;     // ✅ Y 高度坐標
    float dist  = 99999.0f;
    int   hp    = 0;       // ✅ 怪物 HP（預判死亡）
    int   maxHp = 1;       // ✅ 怪物最大HP
    bool  isDead = false;  // ✅ 死亡預判
};

// ============================================================
// 玩家狀態結構
// ============================================================
struct PlayerState {
    char  name[22] = "";   // 角色名稱 (+0x050, char[21] + null terminator)
    int   hp    = 0, maxHp = 1;
    int   mp    = 0, maxMp = 1;
    int   sp    = 0, maxSp = 1;
    int   gold  = 0;
    int   level = 0;
    int   exp   = 0;       // ✅ 當前經驗 (0x92F2D0)
    int   expMax = 0;      // ✅ 升級所需經驗 (0x92F2D8)
    int   combatPower = 0;  // ✅ 戰鬥力
    int   str = 0;  // 力量
    int   vit = 0;  // 體力
    int   spr = 0;  // 精神
    int   dex = 0;  // 敏捷
    int   end = 0;  // 耐力
    int   physAtkMin = 0;  // 物理攻擊力 最小
    int   physAtkMax = 0;  // 物理攻擊力 最大
    int   sprAtkMin = 0;  // 精神攻擊力 最小
    int   sprAtkMax = 0;  // 精神攻擊力 最大
    int   arrowCount = 0;   // ✅ 箭矢數量 (0x92F8D0)
    int   talismanCount = 0; // ✅ 符咒數量 (0x92F8D4)
    int   mapId = 0;
    float x     = 0.0f;
    float z     = 0.0f;
    float y     = 0.0f;    // ✅ Y 高度坐標
    DWORD targetId = 0;
    int   hasTarget = 0;
    int   skillLockState = 0;  // ✅ 技能施放鎖定狀態 (0x92E280: 右鍵施放=1, 死亡=0)
    int   attackCount = 0;    // ✅ 攻擊計數/冷卻 (0x724)
    int   attackRange = 0;    // ✅ 攻擊範圍 (0x728)
    BotState state = BotState::IDLE;
    int   inventoryCount = 0;  // 背包物品數量
};

// ============================================================
// 背包格子
// ============================================================
struct InvSlot {
    int slotIdx = -1;
    DWORD itemId = 0;
    DWORD count = 0;
    bool valid = false;
};

// ============================================================
// 定點結構（用於座標巡航）
// ============================================================
struct Waypoint {
    float x = 0.0f;
    float z = 0.0f;
    const char* name = "";
};

// ============================================================
// Bot 設定（完整版）
// ============================================================
struct BotConfig {
    BotConfig() {
        InitializeCriticalSection(&cs_protected);
        // 預設保護背包前三排（18格 = 3行 x 6列），存放藥水/消耗品
        protected_rows[0] = true;  // 第1排 (slot 0-5)
        protected_rows[1] = true;  // 第2排 (slot 6-11)
        protected_rows[2] = true;  // 第3排 (slot 12-17)
    }
    void Shutdown() {
        shutdownRequested_.store(true);
        // 等待所有鎖持有者釋放（簡化：直接刪除）
        // 注意：線程需在 termination 前呼叫此方法
    }
    ~BotConfig() {
        // 只有在 Shutdown() 未被呼叫的情況下才刪除
        // （Shutdown() 會設置 shutdownRequested_ 並刪除 CS）
        if (!shutdownRequested_.load()) {
            DeleteCriticalSection(&cs_protected);
        }
    }

    // 禁用拷貝/移動 — CRITICAL_SECTION 不能被值拷貝，否則雙重刪除崩潰
    BotConfig(const BotConfig&) = delete;
    BotConfig& operator=(const BotConfig&) = delete;
    BotConfig(BotConfig&&) = delete;
    BotConfig& operator=(BotConfig&&) = delete;

    std::atomic<bool>  active{false};
    std::atomic<bool>  shutdownRequested_{false};  // BUG-016: 防止 CriticalSection 双重删除

    // ── 自動喝水 ──
    std::atomic<int>   hp_potion_pct{80};
    std::atomic<int>   mp_potion_pct{30};
    std::atomic<int>   sp_potion_pct{30};

    // ── 回城設定 ──
    std::atomic<int>   hp_return_pct{30};    // 低血回城
    std::atomic<bool>  inventory_return{false}; // 背包滿回城
    std::atomic<int>   inventory_full_pct{90}; // 背包滿於 %

    // ── 藥水不足回城 ──
    std::atomic<bool>  potion_check_enable{true};  // 啟用藥水不足檢測
    std::atomic<int>   potion_slot_start{12};      // 藥水格起始 (第3列=slot 12)
    std::atomic<int>   potion_slot_end{29};        // 藥水格結束 (第5列=slot 29)
    std::atomic<int>   min_potion_slots{6};        // 最低藥水泥格數 (低於此值回城)
    std::vector<int>   townMapIds;             // 城鎮地圖 ID 清單（安全區）
    std::atomic<int>  teleport_delay_ms{3000}; // 傳送後等待時間（確保載入完成）

    // ── 戰鬥 ──
    std::atomic<int>   attack_interval_ms{400};
    std::atomic<int>   attack_range{8};
    std::atomic<int>   pickup_range{3};
    std::atomic<bool>  auto_pickup{true};
    std::atomic<int>   pickup_interval_ms{1000};

    // ── 自動復活 ──
    std::atomic<bool>  auto_revive{true};
    std::atomic<int>   revive_delay_ms{1500};     // 等待復活選單出現的延遲
    std::atomic<int>   revive_mode{0};            // 0=歸魂珠優先, 1=原地復活, 2=基本復活

// ── 城鎮補給（完整循環）──
    std::atomic<bool>  auto_supply{true};         // 啟用自動補給循環
    std::atomic<bool>  auto_sell{true};          // 自動賣垃圾
    std::atomic<bool>  auto_buy{true};           // 自動買藥水
    std::atomic<bool>  feed_pet{false};           // 餵寵物（預設關閉）
    std::atomic<int>   feed_pet_interval{60};    // 餵寵物間隔（秒）

    // ── 買藥數量設定 ──
    std::atomic<int>   buy_hp_qty{300};          // HP 藥水購買數量
    std::atomic<int>   buy_mp_qty{300};          // MP 藥水購買數量
    std::atomic<int>   buy_sp_qty{300};          // SP 藥水購買數量
    std::atomic<int>   buy_arrow_qty{0};         // 箭矢購買數量 (0=不買)
    std::atomic<int>   buy_charm_qty{0};         // 符咒購買數量 (0=不買)

    // ── 補給超時保護 ──
    std::atomic<int>   supply_npc_timeout{8000};    // 找 NPC / 對話超時（ms）
    std::atomic<int>   supply_sell_timeout{5000};    // 賣物超時（ms）
    std::atomic<int>   supply_max_retries{3};       // 每階段最大重試次數

    // ── 遊戲時間自動傳送 ──
    std::atomic<bool>  auto_game_time{false};       // 啟用開關
    std::atomic<int>   game_time_hour{6};           // 回城時間（小時）
    std::atomic<int>   game_time_min{30};           // 回城時間（分鐘）
    std::atomic<bool>  auto_return_to_field{true}; // 是否自動返回野外
    std::atomic<int>   game_time_return_hour{8};    // 返回野外時間（小時）
    std::atomic<int>   game_time_return_min{0};    // 返回野外時間（分鐘）
    std::atomic<int>   last_trigger_day{-1};        // 上次觸發日（防同一天重複觸發）

    // ── 行走設定 ──
    std::atomic<int>   walk_interval_ms{300};   // 走路按鍵間隔
    std::atomic<int>   move_angle_threshold{30}; // 角度閾值（度）

    // ── 城鎮選擇 ──
    std::atomic<int> town_index{0};  // 0=聖門 1=商洞 2=玄巖 3=鳳凰

    // ── 防呆自動移動 ──
    std::atomic<bool>  anti_stuck_enable{true};  // 啟用防呆自動移動
    std::atomic<int>   anti_stuck_interval_sec{10}; // 移動間隔（秒）
    std::atomic<int>   anti_stuck_offset{15};    // 偏移距離（格）
    std::atomic<int>   anti_stuck_move_ms{500};  // 移動持續時間（毫秒）

    // ── 敵人接近偵測 ──
    std::atomic<bool>  enemy_approach_detect{false};  // 啟閉開關
    std::atomic<float> enemy_approach_speed{50.0f}; // 速度閾值（distance/sec）
    std::atomic<float> enemy_approach_dist{30.0f};   // 偵測距離上限（格）
    std::atomic<int>   enemy_approach_trigger{0};     // 0=回城, 1=暫停

    // ═══════════════ 固定掃打座標 ════════════════
    // 使用 coords.h 的 AttackScanPoints 固定 8 點輪轉
    // 不再使用圓周半徑或投影座標

    // ── Waypoint 巡航 ──
    std::atomic<bool>  waypoint_patrol{false}; // 啟用 Waypoint 巡航
    std::atomic<int>   waypoint_radius{5};     // 到達 Waypoint 半徑（格）

    // ═══════════════ 視覺模式 ════════════════
    // 視覺模式：以視覺辨識為主，取代記憶體讀取
    std::atomic<bool>  use_visual_mode{false}; // 開啟視覺模式（主力）
    std::atomic<int>   visual_scan_timeout_ms{100}; // 視覺掃描超時（毫秒）

    // ═══════════════ YOLO 視覺辨識 ════════════════
    // 獨立 YOLO 模式，與 use_visual_mode 互斥（像素血條）
    std::atomic<bool>  use_yolo_mode{false};        // 啟用 YOLO 視覺辨識
    std::atomic<float> yolo_confidence{0.50f};     // 信心度閾值 (0.0~1.0)
    std::atomic<int>   yolo_nms_threshold{45};      // NMS 閾值 (0~100, 轉為 0.0~1.0)
    // ═════════════════════════════════════════

    // ── 熱鍵 ──
    std::atomic<BYTE>  key_hp_potion{'Q'};       // HP 藥水
    std::atomic<BYTE>  key_mp_potion{'W'};       // MP 藥水
    std::atomic<BYTE>  key_sp_potion{'E'};       // SP 藥水
    std::atomic<BYTE>  key_attack{'Z'};
    std::atomic<BYTE>  key_pickup{VK_SPACE};  // 撿物：空白鍵
    std::atomic<BYTE>  key_skill_select{'1'};    // 攻擊技能選擇鍵（數字鍵 1-0）
    std::atomic<BYTE>  key_support_skill{'2'};   // 輔助技能選擇鍵（數字鍵 1-0）
    std::atomic<bool>  auto_support{true};       // 自動施放輔助技能
    std::atomic<int>   support_interval_ms{5000}; // 輔助技能施放間隔（毫秒）
    std::atomic<BYTE>  key_mount{0x75};          // F6（預留：乘騎功能）
    std::atomic<BYTE>  key_waypoint_start{'S'};  // 起點卡（回城）
    std::atomic<BYTE>  key_waypoint_end{'D'};    // 前點卡（返回野外）
    std::atomic<BYTE>  key_npc_talk{VK_SPACE};   // 空格
    std::atomic<BYTE>  key_walk_forward{VK_UP};  // 方向鍵上
    std::atomic<BYTE>  key_walk_back{VK_DOWN};   // 方向鍵下
    std::atomic<BYTE>  key_walk_left{VK_LEFT};   // 方向鍵左
    std::atomic<BYTE>  key_walk_right{VK_RIGHT}; // 方向鍵右
    std::atomic<BYTE>  key_mount_updown{VK_SPACE};

    // ═══════════════ 技能系統（F1攻擊欄 / F2輔助欄）═══════════════
    // F1/F2 只是欄位切換鍵，按一次即可，不需每次施放都按
    // 攻擊技能：F1 切到攻擊欄 → 數字鍵 1~0 施放
    // 輔助技能：定時 F2 切到輔助欄 → 數字鍵施放 → F1 切回

    // ── 攻擊技能（F1 欄位）──
    static constexpr int MAX_SKILLS = 10;
    std::atomic<int> attackSkillCount{10};         // 使用幾個攻擊技能（1~10）
    std::atomic<int> attackSkillInterval{50};     // 攻擊技能間隔 (ms)
    std::atomic<int> rightClickDelayMs{200};       // 右鍵點擊延遲 (ms)
    std::atomic<BYTE> attackBarKey{VK_F1};         // 攻擊欄切換鍵

    // ── 輔助技能（F2 欄位）──
    static constexpr int MAX_AUX_SKILLS = 5;
    std::atomic<bool> buffEnabled{true};           // 是否啟用輔助技能
    std::atomic<int>  buffSkillCount{2};           // 使用幾個輔助技能（1~5）
    std::atomic<int>  buffCastInterval{30};        // 輔助施放間隔（秒）
    std::atomic<BYTE> buffBarKey{VK_F2};           // 輔助欄切換鍵

    // ── 輪替狀態（thread-safe）──
    std::atomic<int> currentSkillIndex{0};         // 目前輪到哪個攻擊技能
    std::atomic<DWORD> lastRightClickTime{0};      // 上次右鍵攻擊時間
    std::atomic<DWORD> lastSkillTime{0};           // 上次技能施放時間
    std::atomic<DWORD> lastBuffTime{0};            // 上次輔助技能施放時間
    // ════════════════════════════════════════════════

    // ── 保護物品（不賣）──
    std::vector<int>   protected_item_ids;
    CRITICAL_SECTION   cs_protected;

    // ── 列保護（不賣特定列）──
    // 背包78格 = 13列 x 6欄，protected_rows[列索引] = true 表示該列不賣
    static constexpr int MAX_INVENTORY_ROWS = 13;
    bool protected_rows[MAX_INVENTORY_ROWS] = {false};

    // 預設保護物品ID（需掃描記憶體取得實際ID）
    // 格式: {物品名稱, ID}
    struct ProtectedItem {
        const char* name;
        int id;
    };
    // ⚠️ 這些ID需要通過CE掃描記憶體取得，暫時用0作為占位符
    static const ProtectedItem defaultProtectedItems[];
    static const int defaultProtectedCount = 12;

    // ── 煉功點座標（可由 GUI 設定多個）──
    std::vector<Waypoint> huntWaypoints;
};

// ============================================================
// Bot 邏輯函式
// ============================================================

// 初始化
extern void InitBotLogic();
extern void ShutdownBotLogic();  // M-B1: 清理 critical section

// 讀取玩家狀態（RPM）
extern bool ReadPlayerState(struct GameHandle* gh, PlayerState* out);

// 掃描實體（怪物 + NPC，含 HP）
extern int ScanEntities(struct GameHandle* gh,
    std::vector<Entity>* monsters,
    std::vector<Entity>* npcs);

// 讀取背包
extern int ScanInventory(struct GameHandle* gh, std::vector<InvSlot>* out);

// 檢查背包是否滿
extern bool IsInventoryFull(struct GameHandle* gh);
extern bool IsPotionSlotsLow(struct GameHandle* gh);
extern bool CheckGameTimeReturn(struct GameHandle* gh);

// 找最近怪物（可用於戰鬥的）
extern bool FindNearestMonster(struct GameHandle* gh, Entity* out);

// 找最近 NPC
extern bool FindNearestNPC(struct GameHandle* gh, Entity* out);

// 根據 ID 查怪物（含 HP）
extern bool GetMonsterById(struct GameHandle* gh, DWORD id, Entity* out);

// ✅ 預判死亡：檢查當前目標 HP 是否歸零
extern bool IsTargetDying(struct GameHandle* gh);

// Bot 大腦主迴圈（每幀調用）
extern void BotTick(struct GameHandle* gh);

// 狀態查詢
extern BotState GetBotState();
extern void     SetBotState(BotState s);
extern BotConfig* GetBotConfig();
extern BotConfig g_cfg;           // 全域配置實例（定義於 bot_logic.cpp）

// 停止 / 重設
extern void StopBot();
extern void ForceStopBot();
extern void ToggleBotActive();  // 切換開始/暫停
extern void ResetBotTarget();

// ── GUI Pipe 命令用 ──
extern int GetMonsterCount(struct GameHandle* gh);
extern int GetInventoryCount(struct GameHandle* gh);

// ✅ 獲取技能鎖定狀態 (供 GUI 使用)
extern int GetSkillLockState(struct GameHandle* gh);

// ✅ 獲取攻擊計數 (供 GUI 使用)
extern int GetAttackCount(struct GameHandle* gh);

// ✅ 獲取攻擊範圍 (供 GUI 使用)
extern int GetAttackRange(struct GameHandle* gh);

// ✅ 設定移動目標
extern void SetMoveTarget(float x, float z);

// ✅ 補給子狀態 FSM（由 TOWN_SUPPLY 協調呼叫）
extern void SupplyTick(struct GameHandle* gh);

// ✅ 玩家狀態快取（BotThread→UIThread，thread-safe）
extern struct PlayerState GetCachedPlayerState();
extern bool HasCachedPlayerStateData();
extern bool IsRelativeOnlyCombatMode();

// ✅ UI 日誌鉤子（可變參，支援格式化）
extern void UIAddLog(const char* fmt, ...);

// ✅ 全域執行旗標（供 main.cpp 引用）
extern volatile bool g_Running;

// ✅ 實體池狀態查詢（供 UI 顯示）
extern bool IsEntityPoolWorking();

// ✅ 掃描並顯示背包物品（調試用，幫助識別物品ID）
extern void DumpInventoryItems(struct GameHandle* gh);

// ✅ 添加物品到保護列表
extern void AddProtectedItem(int itemId);

// ✅ 從保護列表移除物品
extern void RemoveProtectedItem(int itemId);

// ✅ 獲取保護列表中的物品數量
extern int GetProtectedItemCount();

// ✅ 列保護設定（1X6, 2X6, 3X6...）
extern bool IsRowProtected(int row);
extern void SetRowProtected(int row, bool protect);
extern int GetProtectedRowCount();

// ✅ 背包快取（BotThread 寫入，GUIThread 讀取）
extern int  GetCachedInvCount();
extern bool GetCachedInvSlot(int idx, int* outItemId, int* outCount);
extern DWORD GetCachedInvTime();

// ✅ GUI 背包掃描（點擊按鈕時掃描）
extern int ScanInventoryForGui();
extern bool GetGuiInvSlot(int idx, int* outItemId, int* outCount);

// ✅ 獲取保護列表中的物品ID（按索引）
extern int GetProtectedItemId(int index);

// ✅ 獲取當前技能索引
extern int GetCurrentSkillIndex();

// ✅ 獲取當前/總座標索引
extern int GetHuntPointIndex();

// ✅ 獲取座標總數
extern int GetHuntPointCount();

// ✅ 獲取戰鬥意向狀態（0=SEEKING, 1=ENGAGING, 2=LOOTING）
extern int GetCombatIntentState();

// ✅ 獲取擊殺計數
extern DWORD GetKillCount();

// ✅ 獲取角色名稱
extern void GetPlayerName(char* outName, int maxLen);

// ✅ 離線卡密驗證狀態
extern std::atomic<bool> g_licenseValid;
extern bool IsLicenseValid();
extern void SetLicenseValid(bool valid);

// ✅ YOLO 設定存取（供 GUI 使用）
extern bool GetYoloMode();
extern void SetYoloMode(bool enabled);
extern float GetYoloConfidence();
extern void SetYoloConfidence(float conf);

// ✅ PAUSED 恢復時用的前一個狀態（用於正確恢復）
extern BotState s_pausedPreviousState;
