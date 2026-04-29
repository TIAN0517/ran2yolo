// ============================================================
// FSM Integration
// ============================================================
#include "state_handler.h"
#include "../bot_logic.h"
#include "fsm_integration.h"

void InitFSM() {
    StateHandlerRegistry::Instance().Initialize();
    Log("FSM", "狀態機初始化完成");
}

void ShutdownFSM() {
    StateHandlerRegistry::Instance().Shutdown();
    Log("FSM", "狀態機已清理");
}
