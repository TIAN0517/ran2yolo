// ============================================================
// StateMachine Framework - 帶有 Watchdog 計時器的狀態機
// Timeout = 最大存活時間，超時自動發出 Recovery 中斷
// ============================================================

#pragma once
#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <functional>
#include <chrono>
#include <string>
#include <memory>

// --------------------------------------------------------
// StateResult - 狀態執行結果
// --------------------------------------------------------
struct StateResult {
    enum class Type {
        RUNNING,     // 仍在執行中
        COMPLETED,   // 正常完成
        TIMEOUT,     // 超時中斷（Watchdog 觸發）
        ERROR,       // 錯誤
        RECOVER      // 需要恢復
    };

    Type type = Type::RUNNING;
    std::string message;
    int nextStateId = -1;  // -1 = 使用預設轉換

    static StateResult Running(const std::string& msg = "") {
        return {Type::RUNNING, msg, -1};
    }
    static StateResult Completed(const std::string& msg = "", int next = -1) {
        return {Type::COMPLETED, msg, next};
    }
    static StateResult Timeout(const std::string& msg = "") {
        return {Type::TIMEOUT, msg, -1};
    }
    static StateResult Error(const std::string& msg = "", int next = -1) {
        return {Type::ERROR, msg, next};
    }
    static StateResult Recover(const std::string& msg = "", int next = -1) {
        return {Type::RECOVER, msg, next};
    }
};

// --------------------------------------------------------
// State 基底類別（虛擬）
// --------------------------------------------------------
class State {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    State(int id, const std::string& name, int timeoutMs = 30000)
        : _id(id)
        , _name(name)
        , _timeoutMs(timeoutMs)
        , _entryTime(Clock::now())
        , _lastUpdateTime(Clock::now())
        , _isFirstUpdate(true)
    {}

    virtual ~State() = default;

    // 進入狀態（鉤子）
    virtual void OnEnter() {}

    // 退出狀態（鉤子）
    virtual void OnExit() {}

    // 主更新邏輯（子類別必須實作）
    virtual StateResult Update() = 0;

    // 取得狀態 ID
    int GetId() const { return _id; }

    // 取得狀態名稱
    const std::string& GetName() const { return _name; }

    // 取得已運行時間（毫秒）
    int64_t GetElapsedMs() const {
        auto now = Clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - _entryTime).count();
    }

    // 檢查是否超時
    bool IsTimeout() const {
        return GetElapsedMs() > _timeoutMs;
    }

    // 取得剩餘時間
    int64_t GetRemainingMs() const {
        int64_t elapsed = GetElapsedMs();
        return (_timeoutMs > elapsed) ? (_timeoutMs - elapsed) : 0;
    }

    // 設定超時時間
    void SetTimeout(int timeoutMs) { _timeoutMs = timeoutMs; }

    // 內部更新（由 StateMachine 呼叫）
    StateResult Execute() {
        // 首次進入
        if (_isFirstUpdate) {
            _isFirstUpdate = false;
            _entryTime = Clock::now();
            _lastUpdateTime = _entryTime;
            OnEnter();
        }

        // 檢查超時
        if (IsTimeout()) {
            return HandleTimeout();
        }

        // 執行更新
        auto result = Update();
        _lastUpdateTime = Clock::now();
        return result;
    }

    // 處理超時（虛擬，子類別可覆寫）
    virtual StateResult HandleTimeout() {
        return StateResult::Timeout(_name + " 超時 " + std::to_string(_timeoutMs) + "ms");
    }

protected:
    int _id;
    std::string _name;
    int _timeoutMs;
    TimePoint _entryTime;
    TimePoint _lastUpdateTime;
    bool _isFirstUpdate = true;
};

// --------------------------------------------------------
// RecoveryState - 專門用於異常恢復的特殊狀態
// --------------------------------------------------------
class RecoveryState : public State {
public:
    RecoveryState()
        : State(0, "RECOVERY")
        , _recoveryStep(0)
    {}

    void SetRecoveryInfo(int step, const std::string& reason, int targetState) {
        _recoveryStep = step;
        _reason = reason;
        _targetState = targetState;
        _timeoutMs = 10000;  // Recovery 預設 10 秒
    }

    StateResult Update() override {
        // 根據步驟執行對應的恢復動作
        switch (_recoveryStep) {
        case 0:  // 停止所有動作
            return StateResult::Running("停止中...");
        case 1:  // 等待畫面穩定
            return StateResult::Running("等待畫面...");
        case 2:  // 嘗試返回安全點
            return StateResult::Running("返回安全點...");
        case 3:  // 確認位置
            return StateResult::Running("確認位置...");
        default:
            return StateResult::Completed("恢復完成", _targetState);
        }
    }

private:
    int _recoveryStep = 0;
    std::string _reason;
    int _targetState = -1;
};

// --------------------------------------------------------
// StateMachine - 狀態機管理器
// --------------------------------------------------------
class StateMachine {
public:
    using TransitionFunc = std::function<int(const State&, const StateResult&)>;

    StateMachine(int defaultTimeout = 30000)
        : _currentState(nullptr)
        , _previousState(nullptr)
        , _isRunning(false)
        , _defaultTimeout(defaultTimeout)
    {}

    ~StateMachine() {
        // 清理所有狀態
        for (auto& pair : _states) {
            delete pair.second;
        }
    }

    // 註冊狀態
    template<typename T>
    T* Register(int id, const std::string& name, int timeoutMs = -1) {
        if (_states.find(id) != _states.end()) {
            return dynamic_cast<T*>(_states[id]);
        }
        if (timeoutMs < 0) timeoutMs = _defaultTimeout;
        T* state = new T(id, name, timeoutMs);
        _states[id] = state;
        return state;
    }

    // 註冊帶有工廠函數的狀態
    using StateFactory = std::function<State*(int, const std::string&, int)>;
    void RegisterFactory(int id, StateFactory factory) {
        _factories[id] = factory;
    }

    // 設定轉換函數
    void SetTransitionFunc(TransitionFunc func) {
        _transitionFunc = func;
    }

    // 啟動狀態機
    void Start(int initialStateId) {
        TransitionTo(initialStateId);
        _isRunning = true;
    }

    // 停止
    void Stop() {
        if (_currentState) {
            _currentState->OnExit();
        }
        _currentState = nullptr;
        _isRunning = false;
    }

    // 暫停/恢復
    void Pause() { _isRunning = false; }
    void Resume() { _isRunning = true; }

    // 取得當前狀態
    State* GetCurrentState() const { return _currentState; }
    int GetCurrentStateId() const { return _currentState ? _currentState->GetId() : -1; }
    const std::string& GetCurrentStateName() const {
        static std::string empty = "";
        return _currentState ? _currentState->GetName() : empty;
    }

    // 是否運行中
    bool IsRunning() const { return _isRunning; }

    // 更新（由遊戲迴圈呼叫）
    StateResult Update() {
        if (!_isRunning || !_currentState) {
            return StateResult::Error("狀態機未運行");
        }

        // 執行當前狀態
        auto result = _currentState->Execute();

        // 處理結果
        if (result.type == StateResult::Type::RUNNING) {
            return result;
        }

        // 非 RUNNING：需要轉換狀態
        int nextStateId = result.nextStateId;

        // 使用轉換函數計算下一狀態
        if (_transitionFunc && nextStateId < 0) {
            nextStateId = _transitionFunc(*_currentState, result);
        }

        // 執行轉換
        if (nextStateId >= 0) {
            TransitionTo(nextStateId);
        }

        return result;
    }

    // 直接切換狀態
    void TransitionTo(int stateId) {
        // 退出當前狀態
        if (_currentState) {
            _currentState->OnExit();
            _previousState = _currentState;
        }

        // 查找並進入新狀態
        auto it = _states.find(stateId);
        if (it != _states.end()) {
            _currentState = it->second;
            _currentState->OnEnter();
        } else if (_factories.find(stateId) != _factories.end()) {
            // 使用工廠創建
            auto factory = _factories[stateId];
            _currentState = factory(stateId, "State_" + std::to_string(stateId), _defaultTimeout);
            _states[stateId] = _currentState;
            _currentState->OnEnter();
        } else {
            _currentState = nullptr;
        }

        // 觸發轉換回調
        if (_onTransition) {
            _onTransition(_previousState ? _previousState->GetId() : -1, stateId);
        }
    }

    // 強制進入恢復狀態
    void ForceRecovery(int fromStateId, const std::string& reason) {
        auto recovery = GetOrCreateRecoveryState();
        recovery->SetRecoveryInfo(0, reason, GetSafeStateId());
        TransitionTo(recovery->GetId());
    }

    // 設定回調
    using TransitionCallback = std::function<void(int from, int to)>;
    void SetTransitionCallback(TransitionCallback cb) {
        _onTransition = cb;
    }

    // 狀態計時資訊
    struct StateTiming {
        int stateId;
        std::string stateName;
        int64_t elapsedMs;
        int64_t remainingMs;
        bool isTimeout;
    };

    StateTiming GetCurrentTiming() const {
        StateTiming t;
        if (_currentState) {
            t.stateId = _currentState->GetId();
            t.stateName = _currentState->GetName();
            t.elapsedMs = _currentState->GetElapsedMs();
            t.remainingMs = _currentState->GetRemainingMs();
            t.isTimeout = _currentState->IsTimeout();
        }
        return t;
    }

private:
    RecoveryState* GetOrCreateRecoveryState() {
        auto it = _states.find(0);
        if (it != _states.end()) {
            return dynamic_cast<RecoveryState*>(it->second);
        }
        auto recovery = new RecoveryState();
        _states[0] = recovery;
        return recovery;
    }

    int GetSafeStateId() const {
        // 返回安全狀態（通常是 IDLE 或 HUNTING）
        if (_previousState && _previousState->GetId() != 0) {
            return _previousState->GetId();
        }
        return 1;  // 預設返回 IDLE
    }

    std::map<int, State*> _states;
    std::map<int, StateFactory> _factories;
    State* _currentState = nullptr;
    State* _previousState = nullptr;
    TransitionFunc _transitionFunc;
    TransitionCallback _onTransition;
    bool _isRunning = false;
    int _defaultTimeout = 30000;
};

#endif // STATE_MACHINE_H
