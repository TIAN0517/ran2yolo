// ============================================================
// 純鍵盤模式實現 - 不依賴記憶體讀取
// 2026-05-02
// ============================================================
#include "pure_keyboard_mode.h"
#include "bot_logic.h"
#include "coords.h"
#include "../common/utils.h"

PureKeyboardConfig g_pureKeyboard;

// 攻擊技能計時器
static DWORD s_lastAttackTime = 0;
static DWORD s_lastCircleTime = 0;
static DWORD s_lastPickupTime = 0;
static int s_currentSkillIndex = 0;
static int s_currentPointIndex = 0;
static bool s_lastPickupState = false;

// 初始化純鍵盤模式
void InitPureKeyboardMode() {
    s_lastAttackTime = 0;
    s_lastCircleTime = 0;
    s_lastPickupTime = 0;
    s_currentSkillIndex = 0;
    s_currentPointIndex = 0;
    s_lastPickupState = false;
    Log("純鍵盤", "✅ 純鍵盤模式已初始化");
}

// 純鍵盤模式主循環
void PureKeyboardTick(HWND hWnd, bool pureMode) {
    if (!pureMode || !hWnd || !IsWindow(hWnd)) {
        return;
    }

    DWORD now = GetTickCount();

    // === 1. 圓形掃描 + 攻擊 ===
    if (now - s_lastCircleTime >= (DWORD)g_pureKeyboard.circleIntervalMs) {
        s_lastCircleTime = now;

        // 獲取攻擊點
        int pointIndex = s_currentPointIndex % Coords::ATTACK_SCAN_COUNT;
        Coords::ScanPoint pt = Coords::GetAttackPoint(pointIndex, g_pureKeyboard.enabled ?
            Coords::Point(510, 380) : Coords::Point(510, 380));

        // 發送攻擊點擊
        ClickAttackPoint(hWnd, pointIndex);

        // 更新點索引
        s_currentPointIndex = (s_currentPointIndex + 1) % Coords::ATTACK_SCAN_COUNT;

        // 發送技能鍵
        int skillCount = g_pureKeyboard.skillCount;
        if (skillCount < 1) skillCount = 1;
        if (skillCount > 5) skillCount = 5;

        BYTE skillKey = (BYTE)('1' + s_currentSkillIndex);
        SendKeyInputFocused(skillKey, hWnd);

        // 更新技能索引
        s_currentSkillIndex = (s_currentSkillIndex + 1) % skillCount;

        Logf("純鍵盤", "[攻擊] 技能%c 點%d (%d,%d)",
            (char)skillKey, pointIndex + 1, pt.x, pt.z);
    }

    // === 2. 自動撿物 ===
    if (now - s_lastPickupTime >= (DWORD)g_pureKeyboard.pickupIntervalMs) {
        s_lastPickupTime = now;
        SendKeyInputFocused((BYTE)g_pureKeyboard.pickupKey, hWnd);
        Logf("純鍵盤", "[撿物] 空白鍵");
    }

    // === 3. 冷卻延遲 ===
    Sleep(50);
}
