// ============================================================
// State Handler Framework - 狀態模式實作
// 每個狀態對應一個 Handler，實現 OnEnter/Tick/OnExit
// ============================================================

#pragma once
#ifndef FSM_STATE_HANDLER_H
#define FSM_STATE_HANDLER_H

#include "../../core/bot_logic.h"
#include "../../game/game_process.h"

// Forward declarations for game process functions
extern GameHandle GetGameHandle();
extern void SetGameHandle(GameHandle* gh);
extern bool FindGameProcess(GameHandle* gh);

#include <chrono>
#include <functional>
#include <map>

// ============================================================
// WatchdogTimer - 超時計時器
// ============================================================
struct WatchdogTimer {
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    WatchdogTimer() : _enterTime(Clock::now()), _tickCount(0), _timeoutMs(30000) {}
    explicit WatchdogTimer(int timeoutMs) : _enterTime(Clock::now()), _tickCount(0), _timeoutMs(timeoutMs) {}

    void Reset() {
        _enterTime = Clock::now();
        _tickCount = 0;
    }

    void SetTimeout(int ms) { _timeoutMs = ms; }

    bool IsTimeout() const {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - _enterTime).count();
        return elapsed > _timeoutMs;
    }

    int64_t ElapsedMs() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - _enterTime).count();
    }

    int64_t RemainingMs() const {
        int64_t e = ElapsedMs();
        return (_timeoutMs > e) ? (_timeoutMs - e) : 0;
    }

    void IncrementTick() { ++_tickCount; }
    int64_t TickCount() const { return _tickCount; }

protected:
    TimePoint _enterTime;
    int64_t _tickCount;
    int _timeoutMs;
};

// ============================================================
// IStateHandler - 狀態處理器介面
// ============================================================
struct IStateHandler {
    virtual ~IStateHandler() = default;

    // 狀態名稱
    virtual const char* Name() const = 0;

    // 對應的 BotState ID
    virtual int StateId() const = 0;

    // 進入狀態
    virtual void OnEnter(const PlayerState* st) = 0;

    // 退出狀態
    virtual void OnExit() = 0;

    // 主更新邏輯
    // 返回 -1 = 留在本狀態，其他值 = 轉移到目標狀態
    virtual int Tick(GameHandle* gh, const PlayerState* st, DWORD now) = 0;

    // 超時計時器
    WatchdogTimer& Watchdog() { return _watchdog; }
    const WatchdogTimer& Watchdog() const { return _watchdog; }

protected:
    explicit IStateHandler(int timeoutMs = 30000) : _watchdog(timeoutMs) {}
    WatchdogTimer _watchdog;
};

// ============================================================
// Handler 創建器（用於延遲初始化）
// ============================================================
using StateHandlerFactory = std::function<IStateHandler*()>;

// ============================================================
// StateHandlerRegistry - 狀態處理器註冊表
// ============================================================
class StateHandlerRegistry {
public:
    static StateHandlerRegistry& Instance() {
        static StateHandlerRegistry inst;
        return inst;
    }

    void Register(int stateId, IStateHandler* handler) {
        _handlers[stateId] = handler;
    }

    IStateHandler* Get(int stateId) {
        auto it = _handlers.find(stateId);
        return (it != _handlers.end()) ? it->second : nullptr;
    }

    // 根據 BotState 獲取 Handler
    IStateHandler* Get(BotState state) {
        return Get(static_cast<int>(state));
    }

    // 初始化所有 Handler
    void Initialize();

    // 清理所有 Handler
    void Shutdown();

private:
    StateHandlerRegistry() = default;
    std::map<int, IStateHandler*> _handlers;
};

// ============================================================
// RecoveryHandler 前向宣告與 extern（供 bot_tick_simplified.h 使用）
// ============================================================
struct RecoveryHandler;  // 前向宣告（定義於 state_handler.cpp）
extern RecoveryHandler g_recoveryHandler;  // extern 宣告

// ============================================================
// 工具函式
// ============================================================
const char* GetStateName(int stateId);
int GetDefaultTimeout(BotState state);
bool IsSafeState(BotState state);
bool ShouldAutoRevive(BotState fromState);

#endif // FSM_STATE_HANDLER_H
