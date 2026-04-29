// ============================================================
// FSM State Handler - Extended Implementation
// Includes integration with existing bot_logic functions
// ============================================================

#include "state_handler.h"
#include "../bot_logic.h"
#include "../recovery_vision.h"
#include "../input/input_sender.h"
#include "../input/attack_packet.h"
#include "../input/target_lock.h"
#include "../vision/dm_visual_supply.h"
#include "../embed/dm_plugin.h"
#include "../embed/embedded_anti_pk_images.h"
#include "../config/coords.h"
#include "../platform/coord_calib.h"
#include "../game/nethook_shmem.h"
#include <cstring>
#include <algorithm>

// 遊戲視窗驗證
extern bool IsGameWindow(HWND hWnd);

// 前景視窗檢測（僅在遊戲處於前景時執行關鍵操作）
inline bool IsInForeground(HWND hWnd) {
    if (!hWnd) return false;
    HWND fg = GetForegroundWindow();
    return (fg == hWnd);
}

// ============================================================
// 玩家攻擊偵測（視覺 + 記憶體 雙重保險）
// ============================================================
static DWORD s_antiPkLastCheck = 0;  // 移到函數外，避免 Early Return Bug
static int s_antiPkHpThreshold = 0;  // HP 必須低於此值才能觸發記憶體偵測
static bool s_antiPkMemoryTriggered = false;  // 防止短時間內重複觸發

bool IsPlayerAttackingMe() {
    if (!g_cfg.anti_pk_enable.load()) return false;

    DWORD now = GetTickCount();
    if (now - s_antiPkLastCheck < 700) return false;
    s_antiPkLastCheck = now;

    GameHandle gh = GetGameHandle();
    if (!gh.hWnd) {
        // 每 10 秒打印一次警告
        static DWORD s_warnLogTime = 0;
        if (now - s_warnLogTime > 10000) {
            Log("反PK", "⚠️ hWnd 無效，跳過檢測");
            s_warnLogTime = now;
        }
        return false;
    }

    int foundX = -1, foundY = -1;

    // ===== 檢查 3 張紅名圖 =====
    const char* redNameImages[] = {
        "Player_red_name.png",
        "Player_red_name2.png",
        "pk_alert.png"
    };

    for (int i = 0; i < 3; i++) {
        if (DM_FindPic(gh.hWnd, 0, 0, 1000, 1000, redNameImages[i], "0", 0.80f, 0, &foundX, &foundY, NULL)) {
            Logf("反PK", "★★★ 視覺偵測到紅名！%s at (%d,%d)", redNameImages[i], foundX, foundY);
            return true;
        }
    }

    // ===== 記憶體血量下降判斷（輔助，必須 HP < 800 才啟用，防止再生誤判）=====
    extern PlayerState GetCachedPlayerState();
    PlayerState st = GetCachedPlayerState();

    static int s_lastHp = 0;
    static DWORD s_lastMemoryTrigger = 0;
    if (s_lastHp == 0) s_lastHp = st.hp;

    // 防止短時間內重複觸發（至少間隔 5 秒）
    if (now - s_lastMemoryTrigger < 5000) {
        return false;
    }

    // 只有在玩家 HP 較低（< 800）時才觸發記憶體偵測
    // 防止 HP 再生造成的誤判
    if (st.hp > 0 && s_lastHp > 0 && s_antiPkHpThreshold > 0 && st.hp < s_antiPkHpThreshold) {
        if (st.hp < s_lastHp - 60) {
            Logf("反PK", "★★★ 血量下降！HP: %d -> %d [閾值=%d]", s_lastHp, st.hp, s_antiPkHpThreshold);
            s_lastMemoryTrigger = now;
            s_lastHp = st.hp;
            return true;
        }
    }
    s_lastHp = st.hp;

    // 更新閾值
    s_antiPkHpThreshold = (st.hp > 0) ? st.hp : 0;

    return false;
}

// ============================================================
// State Handler Implementations (must be defined before static instances)
// ============================================================
struct IdleHandler : public IStateHandler {
    IdleHandler() : IStateHandler(30000) {}

    const char* Name() const override { return "IDLE"; }
    int StateId() const override { return (int)BotState::IDLE; }

    void OnEnter(const PlayerState* st) override {
        _watchdog.Reset();
        _recoveryCooldown = 0;  // 重置冷卻計時器
        Log("FSM", "[IDLE] 進入閒置狀態");
    }

    void OnExit() override {}

    int Tick(GameHandle* gh, const PlayerState* st, DWORD now) override {
        _watchdog.IncrementTick();

        // 防止 RECOVERY → IDLE → HUNTING 快速循環（至少等待 3 秒）
        if (_recoveryCooldown == 0) {
            _recoveryCooldown = now;  // 記錄進入 IDLE 的時間
        }

        // 如果從 RECOVERY 來的，等待至少 3 秒再允許進入 HUNTING
        if (g_cfg.active.load() && (now - _recoveryCooldown) > 3000) {
            // BotTick 會自動推進到 HUNTING（遊戲就緒時）
            return -1;
        }

        // 冷卻中：拒絕進入 HUNTING
        return -1;
    }

private:
    DWORD _recoveryCooldown = 0;  // RECOVERY 來的冷卻計時器
};

// ============================================================
// PausedStateHandler
// ============================================================
struct PausedHandler : public IStateHandler {
    PausedHandler() : IStateHandler(600000) {} // 10 minutes

    const char* Name() const override { return "PAUSED"; }
    int StateId() const override { return (int)BotState::PAUSED; }

    void OnEnter(const PlayerState* st) override {
        _watchdog.Reset();
        Log("FSM", "[PAUSED] 進入暫停狀態");
    }

    void OnExit() override {}

    int Tick(GameHandle* gh, const PlayerState* st, DWORD now) override {
        _watchdog.IncrementTick();
        // PAUSED: 等待用戶按 F11 恢復，不自動轉換
        // F11 handler 會直接 SetBotState(HUNTING)
        return -1;
    }
};

// ============================================================
// HuntingStateHandler
// ============================================================
struct HuntingHandler : public IStateHandler {
    HuntingHandler() : IStateHandler(30000) {}

    const char* Name() const override { return "HUNTING"; }
    int StateId() const override { return (int)BotState::HUNTING; }

    void OnEnter(const PlayerState* st) override {
        _watchdog.Reset();
        Log("FSM", "[HUNTING] 進入戰鬥狀態");
        // 刷新攻擊座標快取
        RefreshAttackPointCache();
    }

    void OnExit() override {
        // 重置計時器，以便下次進入時重新初始化
        _lastAttackPoint = 0;
    }

    // 快取攻擊座標（避免每幀重複查詢 CoordCalibrator）
    void RefreshAttackPointCache() {
        CoordCalibrator& calib = CoordCalibrator::Instance();
        const Coords::ScanPoint* scanPoints = Coords::GetAttackScanPoints();
        for (int i = 0; i < 8; i++) {
            CalibIndex calibIdx = (CalibIndex)((int)CalibIndex::SCAN_PT01 + i);
            if (calib.IsCalibrated(calibIdx)) {
                _cachedPoints[i].x = calib.GetX(calibIdx);
                _cachedPoints[i].y = calib.GetZ(calibIdx);
            } else {
                _cachedPoints[i].x = scanPoints[i].x;
                _cachedPoints[i].y = scanPoints[i].z;
            }
        }
        _cacheValid = true;
    }

    inline void GetCachedAttackPoint(int index, int* outX, int* outY) {
        if (!_cacheValid) RefreshAttackPointCache();
        *outX = _cachedPoints[index].x;
        *outY = _cachedPoints[index].y;
    }

    int Tick(GameHandle* gh, const PlayerState* st, DWORD now) override {
        _watchdog.IncrementTick();

        if (!st || !gh || !gh->hWnd) return -1;

        // 每次進入 HUNTING 時重置計時器（使用 _lastAttackPoint == 0 判斷）
        if (_lastAttackPoint == 0) {
            _attackPointIndex = 0;
            _attackSkillIndex = 0;
            _auxSkillIndex = 0;
            _lastAttackPoint = now;
            _lastAttackSkill = 0;
            _lastAuxSkill = 0;
            _lastSkillStatusLog = now;
            _f1Set = false;
        }

        int hpPct = st->maxHp > 0 ? (st->hp * 100) / st->maxHp : 100;
        int hpReturnThresh = g_cfg.hp_return_pct.load();

        // 防連續起點的靜態變數（宣告在前面供後面使用）
        static DWORD s_lastTownReturn = 0;
        static bool s_waitingCooldown = false;

        // 1. 死亡檢測
        if (st->maxHp > 0 && st->hp <= 0) {
            Log("FSM", "[HUNTING -> DEAD] HP 歸零");
            return (int)BotState::DEAD;
        }

        // 2. 低血量回城（防連續起點：至少間隔30秒）
        if (hpPct < hpReturnThresh) {
            if (s_waitingCooldown && (now - s_lastTownReturn < 30000)) {
                // 在冷卻中，忽略這次低血量觸發
            } else {
                s_lastTownReturn = now;
                s_waitingCooldown = true;
                Logf("FSM", "[HUNTING -> RETURNING] 低HP %d%% < %d%%（防連續起點）", hpPct, hpReturnThresh);
                return (int)BotState::RETURNING;
            }
        } else {
            s_waitingCooldown = false;
        }

        // 3. 背包滿回城
        if (g_cfg.inventory_return.load()) {
            int invCount = GetCachedInvCount();
            int maxSlots = 78;
            int pct = maxSlots > 0 ? (invCount * 100) / maxSlots : 0;
            if (pct >= g_cfg.inventory_full_pct.load()) {
                Logf("FSM", "[HUNTING -> RETURNING] 背包滿 %d%%", pct);
                return (int)BotState::RETURNING;
            }
        }

        // 4. 藥水不足回城
        if (g_cfg.potion_check_enable.load() && IsPotionSlotsLow(gh)) {
            Log("FSM", "[HUNTING -> RETURNING] 藥水不足");
            return (int)BotState::RETURNING;
        }

        // 5. 自動補給：檢查是否已在城鎮
        if (g_cfg.auto_supply.load() && IsTownMap(st->mapId)) {
            Logf("FSM", "[HUNTING -> TOWN_SUPPLY] 到達城鎮 MapID=%d", st->mapId);
            return (int)BotState::TOWN_SUPPLY;
        }

        // ==================== 反PK：被攻擊回城 ====================
        {
            static bool s_evadeUsed = false;
            static DWORD s_evadeUseTime = 0;

            if (IsPlayerAttackingMe()) {
                Log("反PK", "★★★ 偵測到玩家攻擊！按S回城 ★★★");

                // 按S起點卡回城
                SendKeyDirect(gh->hWnd, 'S');
                s_evadeUsed = true;
                s_evadeUseTime = now;

                return (int)BotState::IDLE;  // 返回IDLE等待
            }

            // 5分鐘冷卻後：按D前點回練功點
            if (s_evadeUsed) {
                DWORD cooldownMs = (DWORD)(g_cfg.anti_pk_cooldown_sec.load() * 1000);
                if (now - s_evadeUseTime > cooldownMs) {
                    Log("反PK", "[冷卻結束] 按D回練功點");
                    SendKeyDirect(gh->hWnd, 'D');
                    s_evadeUsed = false;
                    return (int)BotState::HUNTING;
                }
                return (int)BotState::IDLE;  // 冷卻中，保持IDLE
            }
        }

        // ==================== 攻擊+右鍵同步（轉圈圈鎖怪）====================
        // 根據戰鬥意向狀態調整行為
        extern int GetCombatIntentState();
        int intentState = GetCombatIntentState();

        // 攻擊決策日誌（每5秒一次）
        static DWORD s_lastAttackDecisionLog = 0;
        if (now - s_lastAttackDecisionLog > 5000) {
            bool inForeground = IsInForeground(gh->hWnd);
            bool gameWindow = IsGameWindow(gh->hWnd);
            const char* intentName[] = {"SEEKING", "ENGAGING", "LOOTING"};
            Logf("FSM", "[攻擊決策] intent=%s foreground=%d gameWindow=%d",
                intentName[intentState], inForeground, gameWindow);
            s_lastAttackDecisionLog = now;
        }

        // SEEKING(0): 正常攻擊, ENGAGING(1): 專注攻擊, LOOTING(2): 專注撿物
        bool shouldAttack = (intentState != 2);  // LOOTING 時不攻擊
        bool shouldPickup = true;

        // 前景模式下才執行關鍵操作（但允許背景攻擊！）
        bool inForeground = IsInForeground(gh->hWnd);
        if (!inForeground) {
            // 背景模式：仍然允許攻擊，只是降低頻率
            shouldAttack = shouldAttack && true;  // 移除限制，允許背景攻擊
            shouldPickup = true;  // 仍然允許撿物
        }

        if (!IsGameWindow(gh->hWnd)) {
            static DWORD s_lastWarn = 0;
            if (now - s_lastWarn > 5000) {
                Log("FSM", "⚠️ 攻擊被阻擋：不是遊戲視窗");
                s_lastWarn = now;
            }
            // 不再 return -1，只是跳過攻擊
        }

        // 攻擊技能輪替間隔（GUI：攻擊間隔 ms）
        DWORD atkIntervalMs = (DWORD)((std::max)(100, g_cfg.attack_interval_ms.load()));

        if (shouldAttack && (_lastAttackSkill == 0 || now - _lastAttackSkill >= atkIntervalMs)) {
            // 按 F1 技能欄（首次）
            if (!_f1Set) {
                SendKeyAttack(gh->hWnd, VK_F1);  // 快速按 F1，無 Focus
                Sleep(IsWin7Platform() ? 80 : 50);
                _f1Set = true;
            }

            // 選怪物攻擊點（使用 NetHook 找怪物）
            int clickX, clickY;
            bool monsterFound = false;

            // 嘗試從 NetHook 找最近怪物
            if (NetHookShmem_IsConnected() && st) {
                ShmemEntity mon = {};
                if (NetHookShmem_GetNearestMonster(st->x, st->z, &mon) && mon.id != 0) {
                    // 將世界座標轉換為相對座標 (0-1000)
                    // 假設地圖範圍是 0-2000 世界座標
                    int relX = (int)(mon.x * 1000.0f / 2000.0f);
                    int relZ = (int)(mon.z * 1000.0f / 2000.0f);

                    // 限制在有效範圍
                    if (relX < 0) relX = 0;
                    if (relX > 1000) relX = 1000;
                    if (relZ < 0) relZ = 0;
                    if (relZ > 1000) relZ = 1000;

                    clickX = relX;
                    clickY = relZ;
                    monsterFound = true;
                    Logf("FSM", "[怪物] 找到 ID=%d 世界(%.1f,%.1f) -> 相對(%d,%d)",
                          mon.id, mon.x, mon.z, relX, relZ);
                }
            }

            // 如果沒找到怪物，使用固定攻擊點
            if (!monsterFound) {
                GetCachedAttackPoint(_attackPointIndex, &clickX, &clickY);
                static DWORD s_lastNoMonsterLog = 0;
                if (now - s_lastNoMonsterLog > 3000) {
                    Log("FSM", "[攻擊] 未找到怪物，使用固定攻擊點");
                    s_lastNoMonsterLog = now;
                }
            }

            // 攻擊：數字鍵 + 右鍵同步施放
            static const BYTE atkKeys[5] = {'1','2','3','4','5'};
            int attackCount = (int)((std::max)(1, (std::min)(5, g_cfg.attackSkillCount.load())));
            int idx = _attackSkillIndex % attackCount;

            // 同時按：數字鍵 + 右鍵點（完美同步）
            SendKeyAttack(gh->hWnd, atkKeys[idx]);  // 按技能鍵（快速，無 Focus）
            // 右鍵選怪：使用相對座標 (0-1000)
            const char* targetType = monsterFound ? "[怪物]" : "[固定點]";
            Logf("FSM", "%s 技能%c + 右鍵(%d,%d)", targetType, atkKeys[idx], clickX, clickY);
            RClickFast(gh->hWnd, clickX, clickY);      // 快速右鍵（避免黑屏）

            // 輪轉下一個
            _attackPointIndex = (_attackPointIndex + 1) % 8;
            _attackSkillIndex = (_attackSkillIndex + 1) % attackCount;
            g_cfg.currentSkillIndex.store(idx);
            _lastAttackSkill = now;
        }

        // ==================== 輔助技能（6-0）====================
        if (g_cfg.auto_support.load() && g_cfg.buffEnabled.load()) {
            static const BYTE auxKeys[5] = {'6','7','8','9','0'};
            int auxCount = (int)((std::max)(1, (std::min)(5, g_cfg.buffSkillCount.load())));
            int auxCooldownSec = (int)((std::max)(1, g_cfg.buffCastInterval.load()));
            DWORD auxCooldown = (DWORD)auxCooldownSec * 1000;
            if (_lastAuxSkill == 0 || now - _lastAuxSkill >= auxCooldown) {
                int idx = _auxSkillIndex % auxCount;
                SendKeyDirect(gh->hWnd, auxKeys[idx]);
                Logf("FSM", "[輔助] %c (%d/%d) cd=%ds",
                    auxKeys[idx], idx + 1, auxCount, auxCooldownSec);
                _auxSkillIndex = (_auxSkillIndex + 1) % auxCount;
                _lastAuxSkill = now;
            }
        }

        if (now - _lastSkillStatusLog > 5000) {
            Logf("FSM", "[技能] 攻擊1-5=%d招 | 輔助6-0=%d招 | 間隔=0.8秒",
                (int)((std::max)(1, (std::min)(5, g_cfg.attackSkillCount.load()))),
                (int)((std::max)(1, (std::min)(5, g_cfg.buffSkillCount.load()))));
            _lastSkillStatusLog = now;
        }

        // ==================== 自動喝水（記憶體偏移，快！）====================
        {
            extern void DMVisual_AutoDrinkFast(HWND hWnd);
            DMVisual_AutoDrinkFast(gh->hWnd);
        }

        // ==================== 撿物品（空白鍵）====================
        static DWORD s_lastPickup = 0;
        if (g_cfg.auto_pickup.load() && shouldPickup) {
            DWORD pickupInterval = (DWORD)(std::max)(200, g_cfg.pickup_interval_ms.load());
            // LOOTING 時縮短間隔，加快撿物
            if (intentState == 2) pickupInterval = pickupInterval / 2;
            // 非前景且非 LOOTING 時延長間隔
            if (!inForeground && intentState != 2) pickupInterval *= 2;

            if (now - s_lastPickup > pickupInterval) {
                SendKeyDirect(gh->hWnd, g_cfg.key_pickup.load());
                s_lastPickup = now;
            }
        } else {
            s_lastPickup = now;
        }

        return -1;
    }

private:
    int _attackPointIndex = 0;
    int _attackSkillIndex = 0;
    int _auxSkillIndex = 0;
    DWORD _lastAttackPoint = 0;
    DWORD _lastAttackSkill = 0;
    DWORD _lastAuxSkill = 0;
    DWORD _lastSkillStatusLog = 0;
    bool _f1Set = false;

    // 攻擊座標快取（避免每幀重複查詢）
    struct Point { int x, y; };
    Point _cachedPoints[8];
    bool _cacheValid = false;
};

// ============================================================
// DeadStateHandler - 60秒超時後轉移到 IDLE（不停機）
// ============================================================
struct DeadHandler : public IStateHandler {
    DeadHandler() : IStateHandler(60000) {}

    const char* Name() const override { return "DEAD"; }
    int StateId() const override { return (int)BotState::DEAD; }

    void OnEnter(const PlayerState* st) override {
        _watchdog.Reset();
        _reviveDelay = 0;
        Log("FSM", "[DEAD] 進入死亡狀態");
    }

    void OnExit() override {}

    int Tick(GameHandle* gh, const PlayerState* st, DWORD now) override {
        _watchdog.IncrementTick();

        if (!st || !gh || !gh->hWnd) return -1;

        // 1. HP 恢復 → IDLE
        if (st->maxHp > 0 && st->hp > 0) {
            Log("FSM", "[DEAD -> IDLE] HP 恢復");
            return (int)BotState::IDLE;
        }

        // 2. 60秒超時 → IDLE
        if (_watchdog.IsTimeout()) {
            Log("Watchdog", "[DEAD -> IDLE] 超時 60s，強制返回 IDLE");
            return (int)BotState::IDLE;
        }

        // 3. 復活邏輯
        if (!g_cfg.auto_revive.load()) return -1;

        if (_reviveDelay == 0) {
            _reviveDelay = now;
            Log("FSM", "[DEAD] 等待復活選單...");
            return -1;
        }

        DWORD elapsed = now - _reviveDelay;
        DWORD delayMs = (DWORD)(std::max)(500, g_cfg.revive_delay_ms.load());
        if (elapsed < delayMs) return -1;

        // 執行復活（根據 revive_mode）
        extern bool ExecuteRevive(HWND hWnd);
        if (ExecuteRevive(gh->hWnd)) {
            Log("FSM", "[DEAD] 復活已執行，等待 HP 恢復");
            _reviveDelay = 0;
            _watchdog.Reset();
        }

        return -1;
    }

private:
    DWORD _reviveDelay = 0;
};

// ============================================================
// ReturningStateHandler - 90秒超時後轉移到 TOWN_SUPPLY
// ============================================================
struct ReturningHandler : public IStateHandler {
    ReturningHandler() : IStateHandler(90000) {}

    const char* Name() const override { return "RETURNING"; }
    int StateId() const override { return (int)BotState::RETURNING; }

    void OnEnter(const PlayerState* st) override {
        _watchdog.Reset();
        _cardSent = false;
        _enterTick = 0;
        Log("FSM", "[RETURNING] 進入返回城鎮狀態");
    }

    void OnExit() override {}

    int Tick(GameHandle* gh, const PlayerState* st, DWORD now) override {
        _watchdog.IncrementTick();

        if (!gh || !gh->hWnd) return -1;

        if (!_cardSent) {
            BYTE key = g_cfg.key_waypoint_start.load();
            if (key) {
                SendKeyDirect(gh->hWnd, key);
                Logf("FSM", "[RETURNING] 發送起點卡 key=0x%02X", key);
            } else {
                Log("FSM", "[RETURNING] 起點卡按鍵未設定，略過發送");
            }
            _cardSent = true;
            _enterTick = now;
        }

        if (!st) return -1;

        // 1. 到達城鎮
        if (IsTownMap(st->mapId)) {
            Log("FSM", "[RETURNING -> TOWN_SUPPLY] 到達城鎮");
            return (int)BotState::TOWN_SUPPLY;
        }

        DWORD elapsed = _enterTick ? (now - _enterTick) : 0;
        DWORD minDelay = (DWORD)(std::max)(1000, g_cfg.teleport_delay_ms.load());
        if (elapsed >= minDelay) {
            Logf("FSM", "[RETURNING -> TOWN_SUPPLY] 起點卡等待完成 (%ums)", elapsed);
            return (int)BotState::TOWN_SUPPLY;
        }

        // 2. 90秒超時 → TOWN_SUPPLY
        if (_watchdog.IsTimeout()) {
            Log("Watchdog", "[RETURNING -> TOWN_SUPPLY] 超時 90s");
            return (int)BotState::TOWN_SUPPLY;
        }

        // 3. 返回途中死亡
        if (st->maxHp > 0 && st->hp <= 0) {
            Log("FSM", "[RETURNING -> DEAD] 返回途中死亡");
            return (int)BotState::DEAD;
        }

        return -1;
    }

private:
    bool _cardSent = false;
    DWORD _enterTick = 0;
};

// ============================================================
// TownSupplyStateHandler
// ============================================================
struct TownSupplyHandler : public IStateHandler {
    TownSupplyHandler() : IStateHandler(300000) {} // 5 minutes

    const char* Name() const override { return "TOWN_SUPPLY"; }
    int StateId() const override { return (int)BotState::TOWN_SUPPLY; }

    void OnEnter(const PlayerState* st) override {
        _watchdog.Reset();
        Log("FSM", "[TOWN_SUPPLY] 進入城鎮補給狀態");
        VisualSupply::Reset();
        VisualSupply::Begin();
    }

    void OnExit() override {}

    int Tick(GameHandle* gh, const PlayerState* st, DWORD now) override {
        _watchdog.IncrementTick();

        if (!g_cfg.auto_supply.load()) {
            Log("FSM", "[TOWN_SUPPLY -> BACK_TO_FIELD] 自動補給關閉");
            return (int)BotState::BACK_TO_FIELD;
        }

        bool supplyDone = VisualSupply::Tick();

        // 1. 補給失敗 → BACK_TO_FIELD（即使失敗也要嘗試返回）
        if (supplyDone && VisualSupply::IsSupplyFailed()) {
            Log("FSM", "[TOWN_SUPPLY -> BACK_TO_FIELD] 補給失敗但嘗試返回");
            return (int)BotState::BACK_TO_FIELD;
        }

        // 2. 補給完成 → BACK_TO_FIELD
        if (supplyDone && VisualSupply::IsSupplyCompleted()) {
            Log("FSM", "[TOWN_SUPPLY -> BACK_TO_FIELD] 補給完成");
            return (int)BotState::BACK_TO_FIELD;
        }

        // 3. 5分鐘超時 → BACK_TO_FIELD（防止永久卡死）
        if (_watchdog.IsTimeout()) {
            Log("Watchdog", "[TOWN_SUPPLY -> BACK_TO_FIELD] 超時 5min");
            return (int)BotState::BACK_TO_FIELD;
        }

        // 4. 補給中死亡
        if (st && st->maxHp > 0 && st->hp <= 0) {
            Log("FSM", "[TOWN_SUPPLY -> DEAD] 補給中死亡");
            return (int)BotState::DEAD;
        }

        return -1;
    }
};

// ============================================================
// BackToFieldStateHandler
// ============================================================
struct BackToFieldHandler : public IStateHandler {
    BackToFieldHandler() : IStateHandler(120000) {} // 2 minutes

    const char* Name() const override { return "BACK_TO_FIELD"; }
    int StateId() const override { return (int)BotState::BACK_TO_FIELD; }

    void OnEnter(const PlayerState* st) override {
        _watchdog.Reset();
        _cardSent = false;
        _enterTick = 0;
        Log("FSM", "[BACK_TO_FIELD] 進入返回野外狀態");
    }

    void OnExit() override {}

    int Tick(GameHandle* gh, const PlayerState* st, DWORD now) override {
        _watchdog.IncrementTick();

        if (!gh || !gh->hWnd) return -1;

        if (!_cardSent) {
            BYTE key = g_cfg.key_waypoint_end.load();
            if (key) {
                SendKeyDirect(gh->hWnd, key);
                Logf("FSM", "[BACK_TO_FIELD] 發送前點卡 key=0x%02X", key);
            } else {
                Log("FSM", "[BACK_TO_FIELD] 前點卡按鍵未設定，略過發送");
            }
            _cardSent = true;
            _enterTick = now;
        }

        DWORD elapsed = _enterTick ? (now - _enterTick) : 0;
        DWORD minDelay = (DWORD)(std::max)(1000, g_cfg.teleport_delay_ms.load());

        if (!st) return -1;

        if (elapsed >= minDelay && !IsTownMap(st->mapId)) {
            Logf("FSM", "[BACK_TO_FIELD -> HUNTING] 前點卡等待完成，已離開城鎮 (%ums)", elapsed);
            return (int)BotState::HUNTING;
        }

        // 2. 2分鐘超時 → HUNTING
        if (_watchdog.IsTimeout()) {
            Log("Watchdog", "[BACK_TO_FIELD -> HUNTING] 超時 2min");
            return (int)BotState::HUNTING;
        }

        // 3. 返回途中死亡
        if (st->maxHp > 0 && st->hp <= 0) {
            Log("FSM", "[BACK_TO_FIELD -> DEAD] 返回途中死亡");
            return (int)BotState::DEAD;
        }

        return -1;
    }

private:
    bool _cardSent = false;
    DWORD _enterTick = 0;
};

// ============================================================
// RecoveryStateHandler - 絕對不停機
// ============================================================
struct RecoveryHandler : public IStateHandler {
    RecoveryHandler() : IStateHandler(30000) {}

    const char* Name() const override { return "RECOVERY"; }
    int StateId() const override { return (int)BotState::RECOVERY; }

    void OnEnter(const PlayerState* st) override {
        _watchdog.Reset();
        _attempts = 0;
        Log("FSM", "[RECOVERY] 進入復原狀態");
        VisionRecovery::EnterRecovery("Handler 觸發 Recovery");
    }

    void OnExit() override {}

    int Tick(GameHandle* gh, const PlayerState* st, DWORD now) override {
        _watchdog.IncrementTick();

        // 1. 視覺識別確認
        VisionRecovery::VisionCheckResult vr = VisionRecovery::AnalyzeCurrentScreen();
        if (vr.confidence > 0.5f) {
            Logf("Recovery", "視覺識別: %s (conf=%.2f)",
                vr.description.c_str(), vr.confidence);
        }

        // 2. 記憶體恢復 → HUNTING
        if (st) {
            Log("Recovery", "[RECOVERY -> HUNTING] 記憶體恢復");
            _attempts = 0;
            VisionRecovery::ExitRecovery("記憶體恢復");
            return (int)BotState::HUNTING;
        }

        // 3. 重試計數：5次後仍失敗 → IDLE（不是 StopBot！）
        _attempts++;
        if (_attempts >= 5) {
            Log("Watchdog", "[RECOVERY -> IDLE] 5次重試後仍失敗，進入 IDLE 等待復原");
            _attempts = 0;
            VisionRecovery::ExitRecovery("重試超限");
            return (int)BotState::IDLE;
        }

        // 4. 超時後重試
        if (_watchdog.IsTimeout()) {
            _watchdog.Reset();
            Logf("Recovery", "重試 %d/5", _attempts);
        }

        return -1;
    }

private:
    int _attempts = 0;
};

// ============================================================
// RandomEvadeHandler - 15秒隨機走位脫困
// ============================================================
struct RandomEvadeHandler : public IStateHandler {
    RandomEvadeHandler() : IStateHandler(15000) {}

    const char* Name() const override { return "RANDOM_EVADE"; }
    int StateId() const override { return (int)BotState::RANDOM_EVADE; }

    void OnEnter(const PlayerState* st) override {
        _watchdog.Reset();
        Log("FSM", "[RANDOM_EVADE] 進入隨機走位脫困");
    }

    void OnExit() override {}

    int Tick(GameHandle* gh, const PlayerState* st, DWORD now) override {
        _watchdog.IncrementTick();

        // 15秒後返回 HUNTING
        if (_watchdog.IsTimeout()) {
            Log("Watchdog", "[RANDOM_EVADE -> HUNTING] 超時 15s，返回戰鬥");
            return (int)BotState::HUNTING;
        }

        // 隨機方向鍵走位
        static BYTE keys[] = { VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN };
        int idx = ((now / 800) % 4);
        SendKeyDirect(gh->hWnd, keys[idx]);

        return -1;
    }
};

// ============================================================
// EmergencyStopHandler
// ============================================================
struct EmergencyStopHandler : public IStateHandler {
    EmergencyStopHandler() : IStateHandler(60000) {} // 1 minute

    const char* Name() const override { return "EMERGENCY_STOP"; }
    int StateId() const override { return (int)BotState::EMERGENCY_STOP; }

    void OnEnter(const PlayerState* st) override {
        _watchdog.Reset();
        Log("FSM", "[EMERGENCY_STOP] 進入緊急停機");
    }

    void OnExit() override {}

    int Tick(GameHandle* gh, const PlayerState* st, DWORD now) override {
        _watchdog.IncrementTick();

        // 1分鐘後自動進入 IDLE
        if (_watchdog.IsTimeout()) {
            Log("Watchdog", "[EMERGENCY_STOP -> IDLE] 超時 60s");
            return (int)BotState::IDLE;
        }

        return -1;
    }
};

// ============================================================
// Global static Handler instances
// ============================================================
static IdleHandler g_idleHandler;
static PausedHandler g_pausedHandler;
static HuntingHandler g_huntingHandler;
static DeadHandler g_deadHandler;
static ReturningHandler g_returningHandler;
static TownSupplyHandler g_townSupplyHandler;
static BackToFieldHandler g_backToFieldHandler;
RecoveryHandler g_recoveryHandler;  // non-static for extern access
static RandomEvadeHandler g_randomEvadeHandler;
static EmergencyStopHandler g_emergencyStopHandler;

// ============================================================
// StateHandlerRegistry::Initialize
// ============================================================
void StateHandlerRegistry::Initialize() {
    Register((int)BotState::IDLE, &g_idleHandler);
    Register((int)BotState::PAUSED, &g_pausedHandler);
    Register((int)BotState::HUNTING, &g_huntingHandler);
    Register((int)BotState::DEAD, &g_deadHandler);
    Register((int)BotState::RETURNING, &g_returningHandler);
    Register((int)BotState::TOWN_SUPPLY, &g_townSupplyHandler);
    Register((int)BotState::BACK_TO_FIELD, &g_backToFieldHandler);
    Register((int)BotState::RECOVERY, &g_recoveryHandler);
    Register((int)BotState::RANDOM_EVADE, &g_randomEvadeHandler);
    Register((int)BotState::EMERGENCY_STOP, &g_emergencyStopHandler);

    Log("FSM", "Handler 註冊完成");
}

void StateHandlerRegistry::Shutdown() {
    _handlers.clear();
}

// ============================================================
// Tool functions
// ============================================================
const char* GetStateName(int stateId) {
    switch (stateId) {
        case (int)BotState::IDLE: return "IDLE";
        case (int)BotState::HUNTING: return "HUNTING";
        case (int)BotState::DEAD: return "DEAD";
        case (int)BotState::RETURNING: return "RETURNING";
        case (int)BotState::TOWN_SUPPLY: return "TOWN_SUPPLY";
        case (int)BotState::BACK_TO_FIELD: return "BACK_TO_FIELD";
        case (int)BotState::RANDOM_EVADE: return "RANDOM_EVADE";
        case (int)BotState::EMERGENCY_STOP: return "EMERGENCY_STOP";
        case (int)BotState::RECOVERY: return "RECOVERY";
        case (int)BotState::PAUSED: return "PAUSED";
        default: return "UNKNOWN";
    }
}

int GetDefaultTimeout(BotState state) {
    switch (state) {
        case BotState::DEAD: return 60000;
        case BotState::RETURNING: return 90000;
        case BotState::RANDOM_EVADE: return 15000;
        case BotState::EMERGENCY_STOP: return 60000;
        case BotState::BACK_TO_FIELD: return 120000;
        case BotState::TOWN_SUPPLY: return 300000;
        case BotState::RECOVERY: return 30000;
        default: return 30000;
    }
}

bool IsSafeState(BotState state) {
    return state == BotState::IDLE ||
           state == BotState::TOWN_SUPPLY ||
           state == BotState::BACK_TO_FIELD;
}

bool ShouldAutoRevive(BotState fromState) {
    return fromState == BotState::DEAD;
}
