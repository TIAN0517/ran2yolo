#include "gui_ranbot.h"
#include "bot_logic.h"
#include "offset_config.h"
#include "game_process.h"
#include "coords.h"
#include "coord_calib.h"
#include "offline_license.h"
#include "input_sender.h"
#include "nethook_shmem.h"
#include "imgui/imgui.h"
#include <cstdio>
#include <cstring>

// F9 量尺座標抓取：自訂訊息
#define WM_COORDCAPTURE (WM_USER + 100)

// ═══════════════ F9 量尺抓取共用狀態 ═══════════════
volatile LONG s_captureMode = 0;       // 0=關閉, 1=等待點擊（供 main.cpp WM_HOTKEY 使用）
volatile LONG s_captureFieldId = 0;    // 瞄準的 ImGui field ID
HHOOK s_hMouseHookCapture = NULL;      // 抓取用的滑鼠鉤子
HHOOK s_hKeyboardHook = NULL;          // 鍵盤鉤子 (WH_KEYBOARD_LL)
static HWND s_gameHwndForCapture = NULL;      // 遊戲窗口句柄（用於座標轉換）

// ═══════════════ 座標路由狀態（跨執行緒：WnProc → GUI frame）═══════════════
static int  s_routingFieldId = 0;
static int  s_routingX = 0;
static int  s_routingY = 0;
static bool s_hasRouting = false;

void RouteCapturedCoord(int fieldId, int x, int y) {
    s_routingFieldId = fieldId;
    s_routingX = x;
    s_routingY = y;
    s_hasRouting = true;
}

// ============================================================
// 外部引用
// ============================================================
extern bool g_guiVisible;
extern HWND g_hWnd;
void UIAddLog(const char* fmt, ...);
void ToggleBotActive();
void ForceStopBot();
void SetLicenseValid(bool valid);
bool IsLicenseValid();
void ClearDllLogBuffer();
void DrawDllLogs(bool autoScroll);

// ============================================================
// UI 主題
// ============================================================
static const ImVec4 COL_BG(0.10f, 0.11f, 0.14f, 1.0f);
static const ImVec4 COL_PANEL(0.13f, 0.14f, 0.18f, 1.0f);
static const ImVec4 COL_BORDER(0.25f, 0.28f, 0.35f, 1.0f);
static const ImVec4 COL_TEXT(0.90f, 0.92f, 0.96f, 1.0f);
static const ImVec4 COL_TEXT_DIM(0.55f, 0.58f, 0.65f, 1.0f);
static const ImVec4 COL_ACCENT(0.30f, 0.65f, 1.00f, 1.0f);
static const ImVec4 COL_GREEN(0.30f, 0.85f, 0.50f, 1.0f);
static const ImVec4 COL_RED(0.95f, 0.35f, 0.35f, 1.0f);
static const ImVec4 COL_ORANGE(1.00f, 0.65f, 0.25f, 1.0f);

// ============================================================
// 分頁宣告
// ============================================================
static void PageCombat();
static void PagePotion();
static void PageSupply();
static void PageCoords();
static void PageSystem();

// ============================================================
// 主渲染
// ============================================================
void RenderMainGUI() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, COL_BG);
    ImGui::PushStyleColor(ImGuiCol_Border, COL_BORDER);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));

    if (!ImGui::Begin("JyTrainer", NULL,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoBringToFrontOnFocus))
    {
        ImGui::End();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
        return;
    }

    // ========================================================
    // 頂部狀態列
    // ========================================================
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
        ImGui::BeginChild("##Header", ImVec2(0, 72), false);
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);

        // 讀取一次快取（避免每幀重複讀取造成數值跳動）
        static PlayerState s_stCache = {};
        static DWORD s_lastCacheUpdate = 0;
        DWORD nowT = GetTickCount();
        if (nowT - s_lastCacheUpdate > 200) {
            s_stCache = GetCachedPlayerState();
            s_lastCacheUpdate = nowT;
        }
        PlayerState st = s_stCache;

        // 左：標題
        ImGui::SetCursorPos(ImVec2(12, 6));
        ImGui::TextColored(COL_ACCENT, "JyTrainer");
        ImGui::SameLine();
        ImGui::TextColored(COL_TEXT_DIM, "v1.0");

        // 中：角色信息
        ImGui::SetCursorPos(ImVec2(180, 6));
        ImGui::TextColored(COL_TEXT, "%s", st.name[0] ? st.name : "---");
        ImGui::SetCursorPos(ImVec2(180, 24));
        ImGui::TextColored(COL_TEXT_DIM, "Lv.%d", st.level);

        // HP/MP/SP 條（三排）- 防止無效資料造成異常顯示
        float hpPct = (st.maxHp > 0 && st.hp >= 0) ? (float)st.hp / st.maxHp : 0.0f;
        float mpPct = (st.maxMp > 0 && st.mp >= 0) ? (float)st.mp / st.maxMp : 0.0f;
        float spPct = (st.maxSp > 0 && st.sp >= 0) ? (float)st.sp / st.maxSp : 0.0f;
        hpPct = (hpPct > 1.0f) ? 1.0f : hpPct;
        mpPct = (mpPct > 1.0f) ? 1.0f : mpPct;
        spPct = (spPct > 1.0f) ? 1.0f : spPct;

        bool hpValid = (st.maxHp > 0 && st.hp >= 0 && st.hp <= st.maxHp);
        bool mpValid = (st.maxMp > 0 && st.mp >= 0 && st.mp <= st.maxMp);
        bool spValid = (st.maxSp > 0 && st.sp >= 0 && st.sp <= st.maxSp);

        ImGui::SetCursorPos(ImVec2(280, 6));
        ImGui::TextColored(COL_TEXT_DIM, "HP");
        ImGui::SameLine();
        ImGui::SetCursorPosX(305);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, hpValid ? COL_GREEN : COL_TEXT_DIM);
        ImGui::ProgressBar(hpPct, ImVec2(100, 12), "");
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::TextColored(hpValid ? COL_TEXT : COL_TEXT_DIM, " %d/%d", st.hp, st.maxHp);

        ImGui::SetCursorPos(ImVec2(280, 26));
        ImGui::TextColored(COL_TEXT_DIM, "MP");
        ImGui::SameLine();
        ImGui::SetCursorPosX(305);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, mpValid ? COL_ACCENT : COL_TEXT_DIM);
        ImGui::ProgressBar(mpPct, ImVec2(100, 12), "");
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::TextColored(mpValid ? COL_TEXT : COL_TEXT_DIM, " %d/%d", st.mp, st.maxMp);

        ImGui::SetCursorPos(ImVec2(280, 46));
        ImGui::TextColored(COL_TEXT_DIM, "SP");
        ImGui::SameLine();
        ImGui::SetCursorPosX(305);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, spValid ? COL_ORANGE : COL_TEXT_DIM);
        ImGui::ProgressBar(spPct, ImVec2(100, 12), "");
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::TextColored(spValid ? COL_TEXT : COL_TEXT_DIM, " %d/%d", st.sp, st.maxSp);

        // 左：Bot狀態與控制（置於最左方，避免擋住 HP/MP/SP）
        ImGui::SetCursorPos(ImVec2(10, 70));
        ImGui::TextColored(COL_TEXT_DIM, "Bot:");
        ImGui::SameLine();
        BotState state = GetBotState();
        const char* stateName = "UNKNOWN";
        ImVec4 stateColor = COL_TEXT_DIM;
        switch (state) {
            case BotState::IDLE: stateName = "IDLE"; stateColor = COL_TEXT_DIM; break;
            case BotState::HUNTING: stateName = "HUNTING"; stateColor = COL_GREEN; break;
            case BotState::DEAD: stateName = "DEAD"; stateColor = COL_RED; break;
            case BotState::RETURNING: stateName = "RETURN"; stateColor = COL_ORANGE; break;
            case BotState::TOWN_SUPPLY: stateName = "SUPPLY"; stateColor = COL_ACCENT; break;
            case BotState::BACK_TO_FIELD: stateName = "BACK"; stateColor = COL_ACCENT; break;
            case BotState::PAUSED: stateName = "PAUSED"; stateColor = COL_ORANGE; break;
            default: stateName = "UNKNOWN"; stateColor = COL_RED; break;
        }
        ImGui::SameLine();
        ImGui::TextColored(stateColor, "[%s]", stateName);

        // 戰鬥意向
        ImGui::SameLine();
        ImGui::TextColored(COL_TEXT_DIM, " Intent:");
        int intentState = GetCombatIntentState();
        const char* intentNames[] = {"尋找", "戰鬥", "撿物"};
        const char* intentName = intentNames[intentState % 3];
        ImVec4 intentColor = (intentState == 1) ? COL_GREEN : (intentState == 2 ? COL_ORANGE : COL_ACCENT);
        ImGui::SameLine();
        ImGui::TextColored(intentColor, "[%s]", intentName);

        // 擊殺計數
        ImGui::SameLine();
        ImGui::TextColored(COL_TEXT_DIM, " Kill:");
        DWORD killCount = GetKillCount();
        ImGui::SameLine();
        ImGui::TextColored(COL_ACCENT, "%d", killCount);

        // 開始/停止按鈕（放在左側名字下方）
        ImGui::SameLine();
        ImGui::SetCursorPosX(520);
        bool licenseOk = IsLicenseValid();
        bool active = GetBotConfig()->active.load();
        ImVec4 btnColor = active ? COL_RED : (licenseOk ? COL_GREEN : COL_ORANGE);
        ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(btnColor.x * 1.2f, btnColor.y * 1.2f, btnColor.z * 1.2f, 1.0f));
        // 修復：使用 IsItemClicked 防止按鈕被視為 "仍在點擊" 導致雙次觸發
        ImGui::Button(active ? "STOP" : "START", ImVec2(55, 20));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            ToggleBotActive();
        }
        ImGui::PopStyleColor(2);

        // 卡密狀態指示
        ImGui::SetCursorPos(ImVec2(620, 42));
        ImVec4 licColor = licenseOk ? COL_GREEN : COL_RED;
        ImGui::TextColored(licColor, "[%s]", licenseOk ? "已認證" : "未認證");

        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ========================================================
    // 卡密面板 (離線一卡一機)
    // ========================================================
    {
        static char s_licenseInput[8192] = {0};
        static char s_cachedHwid[64] = {0};
        static bool s_hwidLoaded = false;
        static OfflineLicenseInfo s_licenseInfo = {0};
        static DWORD s_lastVerifyTime = 0;
        static bool s_licenseValid = false;

        // 獲取 HWID（只取一次）
        if (!s_hwidLoaded) {
            const char* hwid = GetMachineHWID();
            strncpy(s_cachedHwid, hwid ? hwid : "UNKNOWN", sizeof(s_cachedHwid) - 1);
            s_hwidLoaded = true;

            // 自動載入已緩存的卡密
            char cachedToken[8192] = {0};
            if (OfflineLicenseLoadCached(cachedToken, sizeof(cachedToken))) {
                strncpy(s_licenseInput, cachedToken, sizeof(s_licenseInput) - 1);
            }
        }

        // 每秒檢查一次卡密狀態
        DWORD now = GetTickCount();
        if (now - s_lastVerifyTime > 1000) {
            s_lastVerifyTime = now;

            // 嘗試驗證當前輸入
            OfflineLicenseInfo info = {0};
            if (s_licenseInput[0] && OfflineLicenseVerifySimple(s_licenseInput, &info)) {
                if (!s_licenseValid) {
                    // 首次驗證成功，保存緩存
                    OfflineLicenseSaveCached(s_licenseInput);
                    UIAddLog("[認證] 卡密驗證成功！");
                }
                s_licenseValid = true;
                s_licenseInfo = info;
                SetLicenseValid(true);
            } else if (s_licenseInput[0]) {
                // 有輸入但驗證失敗，才記錄失敗
                if (s_licenseValid) {
                    UIAddLog("[認證] 卡密驗證失敗：%s", info.message);
                }
                s_licenseValid = false;
                SetLicenseValid(false);
            }
            // 如果 s_licenseInput 為空（緩存未加載），不做任何操作
            // 保持 g_licenseValid 不變（InitBotLogic 已驗證過）
        }

        ImGui::PushStyleColor(ImGuiCol_ChildBg, s_licenseValid ?
            ImVec4(0.08f, 0.15f, 0.10f, 1.0f) : ImVec4(0.15f, 0.08f, 0.08f, 1.0f));
        ImGui::BeginChild("##LicensePanel", ImVec2(0, 65), false);

        // 第一行：狀態（放在最前面，避免被 HWID 擋住）
        ImGui::SetCursorPos(ImVec2(12, 8));
        ImGui::TextColored(COL_TEXT_DIM, "狀態:");
        ImGui::SameLine();
        ImGui::SetCursorPosX(50);
        if (s_licenseValid) {
            ImGui::TextColored(COL_GREEN, "已激活 (剩餘 %d 天)", s_licenseInfo.days_left);
        } else {
            ImGui::TextColored(COL_RED, "未激活");
        }

        // 第一行：HWID + 複製按鈕
        ImGui::SetCursorPos(ImVec2(180, 8));
        ImGui::TextColored(COL_TEXT_DIM, "機器碼:");
        ImGui::SameLine();
        ImGui::SetCursorPosX(235);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
        ImGui::TextWrapped("%s", s_cachedHwid);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("複製", ImVec2(45, 18))) {
            if (OpenClipboard(NULL)) {
                EmptyClipboard();
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, strlen(s_cachedHwid) + 1);
                if (hMem) {
                    memcpy(GlobalLock(hMem), s_cachedHwid, strlen(s_cachedHwid) + 1);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_TEXT, hMem);
                }
                CloseClipboard();
                UIAddLog("[複製] 機器碼已複製到剪貼簿");
            }
        }

        // 第二行：卡密輸入框
        ImGui::SetCursorPos(ImVec2(12, 35));
        ImGui::TextColored(COL_TEXT_DIM, "卡密:");
        ImGui::SameLine();
        ImGui::SetCursorPosX(50);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
        ImGui::PushItemWidth(400);
        ImGui::InputText("##LicenseKey", s_licenseInput, sizeof(s_licenseInput),
            ImGuiInputTextFlags_None);
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();

        ImGui::SameLine();
        if (ImGui::Button("激活", ImVec2(60, 22))) {
            OfflineLicenseInfo info = {0};
            if (OfflineLicenseVerifySimple(s_licenseInput, &info)) {
                OfflineLicenseSaveCached(s_licenseInput);
                s_licenseValid = true;
                s_licenseInfo = info;
                SetLicenseValid(true);
                UIAddLog("[認證] 卡密驗證成功！");
            } else {
                s_licenseValid = false;
                UIAddLog("[認證] 卡密無效：%s", info.message);
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();

    // ========================================================
    // 主內容區（Tab 分頁）
    // ========================================================
    static int s_activeTab = 0;
    const char* tabs[] = { "戰鬥", "藥水", "補給", "座標", "系統" };

    ImGui::PushStyleColor(ImGuiCol_Button, COL_PANEL);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.22f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, COL_ACCENT);
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16, 8));

    for (int i = 0; i < 5; i++) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::Button(tabs[i], ImVec2(80, 32))) {
            s_activeTab = i;
        }
        if (s_activeTab == i) {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(min.x, max.y - 2),
                ImVec2(max.x, max.y),
                IM_COL32(48, 100, 180, 255));
        }
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);

    ImGui::Spacing();

    // ========================================================
    // 分頁內容
    // ========================================================
    switch (s_activeTab) {
        case 0: PageCombat(); break;
        case 1: PagePotion(); break;
        case 2: PageSupply(); break;
        case 3: PageCoords(); break;
        case 4: PageSystem(); break;
    }

    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
}

// ============================================================
// 戰鬥分頁
// ============================================================
static void PageCombat() {
    BotConfig* cfg = GetBotConfig();

    ImGui::Columns(2, "##CombatCols", true);
    ImGui::SetColumnWidth(0, 280);

    // 左列：攻擊設定
    {
        ImGui::BeginChild("##AttackPanel", ImVec2(0, 0), false);
        ImGui::TextColored(COL_ACCENT, "攻擊設定");
        ImGui::Separator();

        // 戰鬥意向控制
        int intentState = GetCombatIntentState();
        const char* intentNames[] = {"尋找目標", "戰鬥中", "撿物中"};
        ImVec4 intentColor = (intentState == 1) ? COL_GREEN : (intentState == 2 ? COL_ORANGE : COL_ACCENT);
        ImGui::TextColored(COL_TEXT_DIM, "戰鬥意向");
        ImGui::SameLine();
        if (ImGui::Button(intentNames[intentState % 3], ImVec2(90, 25))) {
            CycleCombatIntent();
        }
        ImGui::SameLine();
        ImGui::TextColored(intentColor, "[%s]", intentNames[intentState % 3]);

        ImGui::Spacing();

        // 視覺模式開關
        bool visualMode = cfg->use_visual_mode.load();
        ImGui::TextColored(COL_TEXT_DIM, "視覺模式");
        ImGui::SameLine();
        if (ImGui::Checkbox("##VisualMode", &visualMode)) {
            cfg->use_visual_mode.store(visualMode);
        }
        ImGui::SameLine();
        ImGui::TextColored(visualMode ? COL_GREEN : COL_TEXT_DIM, "[%s]", visualMode ? "開" : "關");

        // ✅ 封包攻擊模式開關
        bool packetAttack = cfg->use_packet_attack.load();
        ImGui::TextColored(COL_TEXT_DIM, "封包攻擊");
        ImGui::SameLine();
        if (ImGui::Checkbox("##PacketAttack", &packetAttack)) {
            cfg->use_packet_attack.store(packetAttack);
        }
        ImGui::SameLine();
        ImGui::TextColored(packetAttack ? COL_GREEN : COL_TEXT_DIM, "[%s]", packetAttack ? "開" : "關");

        ImGui::Spacing();

        // 攻擊技能數量
        int attackCount = cfg->attackSkillCount.load();
        ImGui::TextColored(COL_TEXT_DIM, "攻擊技能 (1~5)");
        ImGui::PushItemWidth(180);
        if (ImGui::SliderInt("##AttackCount", &attackCount, 1, 5, "%d 個")) {
            cfg->attackSkillCount.store(attackCount);
        }
        ImGui::PopItemWidth();

        // 技能間隔
        int skillInterval = cfg->attackSkillInterval.load();
        ImGui::TextColored(COL_TEXT_DIM, "施放間隔 (ms)");
        ImGui::PushItemWidth(180);
        if (ImGui::SliderInt("##SkillInterval", &skillInterval, 200, 2000, "%d ms")) {
            cfg->attackSkillInterval.store(skillInterval);
        }
        ImGui::PopItemWidth();

        // 圓形旋轉間隔
        int circleInterval = cfg->circleRotationIntervalMs.load();
        ImGui::TextColored(COL_TEXT_DIM, "圓形旋轉 (ms)");
        ImGui::PushItemWidth(180);
        if (ImGui::SliderInt("##CircleInterval", &circleInterval, 50, 500, "%d ms")) {
            cfg->circleRotationIntervalMs.store(circleInterval);
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();
        ImGui::TextColored(COL_ACCENT, "攻擊中心點");
        ImGui::Separator();

        int centerX = cfg->attackCenterX.load();
        int centerZ = cfg->attackCenterZ.load();
        ImGui::PushItemWidth(100);
        if (ImGui::InputInt("X", &centerX, 10, 50)) {
            cfg->attackCenterX.store(centerX);
        }
        ImGui::SameLine();
        if (ImGui::InputInt("Z", &centerZ, 10, 50)) {
            cfg->attackCenterZ.store(centerZ);
        }
        ImGui::PopItemWidth();
        if (ImGui::Button("重置 (510,380)", ImVec2(120, 25))) {
            cfg->attackCenterX.store(510);
            cfg->attackCenterZ.store(380);
        }

        ImGui::EndChild();
    }

    ImGui::NextColumn();

    // 右列：輔助設定
    {
        ImGui::BeginChild("##AuxPanel", ImVec2(0, 0), false);
        ImGui::TextColored(COL_ACCENT, "輔助技能 (6~0)");
        ImGui::Separator();

        bool auxEnabled = cfg->auxEnabled.load();
        ImGui::Checkbox("啟用輔助技能", &auxEnabled);
        cfg->auxEnabled.store(auxEnabled);

        if (auxEnabled) {
            int auxCount = cfg->auxSkillCount.load();
            ImGui::TextColored(COL_TEXT_DIM, "技能數量");
            ImGui::PushItemWidth(180);
            if (ImGui::SliderInt("##AuxCount", &auxCount, 1, 5, "%d 個")) {
                cfg->auxSkillCount.store(auxCount);
            }
            ImGui::PopItemWidth();

            int auxInterval = cfg->auxCastIntervalSec.load();
            ImGui::TextColored(COL_TEXT_DIM, "施放間隔 (秒)");
            ImGui::PushItemWidth(180);
            if (ImGui::SliderInt("##AuxInterval", &auxInterval, 1, 120, "%d 秒")) {
                cfg->auxCastIntervalSec.store(auxInterval);
            }
            ImGui::PopItemWidth();
        }

        ImGui::Spacing();
        ImGui::TextColored(COL_ACCENT, "撿物設定");
        ImGui::Separator();

        bool autoPickup = cfg->auto_pickup.load();
        ImGui::Checkbox("自動撿物 (空白鍵)", &autoPickup);
        cfg->auto_pickup.store(autoPickup);

        int pickupInterval = cfg->pickup_interval_ms.load();
        if (autoPickup) {
            ImGui::TextColored(COL_TEXT_DIM, "撿物間隔");
            ImGui::PushItemWidth(180);
            if (ImGui::SliderInt("##PickupInterval", &pickupInterval, 200, 2000, "%d ms")) {
                cfg->pickup_interval_ms.store(pickupInterval);
            }
            ImGui::PopItemWidth();
        }

        ImGui::EndChild();
    }

    ImGui::Columns(1);
}

// ============================================================
// 藥水分頁
// ============================================================
static void PagePotion() {
    BotConfig* cfg = GetBotConfig();

    ImGui::Columns(2, "##PotionCols", true);
    ImGui::SetColumnWidth(0, 280);

    // 左列：喝水設定
    {
        ImGui::BeginChild("##DrinkPanel", ImVec2(0, 0), false);
        ImGui::TextColored(COL_ACCENT, "自動喝水");
        ImGui::Separator();

        int hpPct = cfg->hp_potion_pct.load();
        ImGui::TextColored(COL_TEXT_DIM, "HP < %%");
        ImGui::PushItemWidth(180);
        if (ImGui::SliderInt("##HpPct", &hpPct, 0, 100, "%d%%")) {
            cfg->hp_potion_pct.store(hpPct);
        }
        ImGui::PopItemWidth();
        ImGui::TextColored(COL_GREEN, "  按鍵: Q");

        int mpPct = cfg->mp_potion_pct.load();
        ImGui::TextColored(COL_TEXT_DIM, "MP < %%");
        ImGui::PushItemWidth(180);
        if (ImGui::SliderInt("##MpPct", &mpPct, 0, 100, "%d%%")) {
            cfg->mp_potion_pct.store(mpPct);
        }
        ImGui::PopItemWidth();
        ImGui::TextColored(COL_ACCENT, "  按鍵: W");

        int spPct = cfg->sp_potion_pct.load();
        ImGui::TextColored(COL_TEXT_DIM, "SP < %%");
        ImGui::PushItemWidth(180);
        if (ImGui::SliderInt("##SpPct", &spPct, 0, 100, "%d%%")) {
            cfg->sp_potion_pct.store(spPct);
        }
        ImGui::PopItemWidth();
        ImGui::TextColored(COL_ORANGE, "  按鍵: E");

        ImGui::Spacing();
        int potionInterval = cfg->potion_interval_ms.load();
        ImGui::TextColored(COL_TEXT_DIM, "喝水間隔");
        ImGui::PushItemWidth(180);
        if (ImGui::SliderInt("##PotionInterval", &potionInterval, 100, 2000, "%d ms")) {
            cfg->potion_interval_ms.store(potionInterval);
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 箭矢/符咒購買（每次150，750滿）
        int buyArrow = cfg->buy_arrow_qty.load();
        int buyCharm = cfg->buy_charm_qty.load();

        ImGui::TextColored(COL_TEXT_DIM, "箭矢 (滿750)");
        ImGui::PushItemWidth(100);
        if (ImGui::InputInt("##BuyArrow", &buyArrow, 150, 300)) {
            if (buyArrow < 0) buyArrow = 0;
            cfg->buy_arrow_qty.store(buyArrow);
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::TextColored(COL_TEXT_DIM, " (每150)");

        ImGui::TextColored(COL_TEXT_DIM, "符咒 (滿750)");
        ImGui::PushItemWidth(100);
        if (ImGui::InputInt("##BuyCharm", &buyCharm, 150, 300)) {
            if (buyCharm < 0) buyCharm = 0;
            cfg->buy_charm_qty.store(buyCharm);
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::TextColored(COL_TEXT_DIM, " (每150)");

        ImGui::EndChild();
    }

    ImGui::NextColumn();

    // 右列：回城設定
    {
        ImGui::BeginChild("##ReturnPanel", ImVec2(0, 0), false);
        ImGui::TextColored(COL_ACCENT, "低血回城");
        ImGui::Separator();

        int hpReturn = cfg->hp_return_pct.load();
        ImGui::TextColored(COL_TEXT_DIM, "HP < %% 回城");
        ImGui::PushItemWidth(180);
        if (ImGui::SliderInt("##HpReturn", &hpReturn, 0, 100, "%d%%")) {
            cfg->hp_return_pct.store(hpReturn);
        }
        ImGui::PopItemWidth();
        ImGui::TextColored(COL_RED, "  按鍵: S (起點卡)");

        ImGui::Spacing();
        ImGui::TextColored(COL_ACCENT, "自動復活");
        ImGui::Separator();

        bool autoRevive = cfg->auto_revive.load();
        ImGui::Checkbox("啟用自動復活", &autoRevive);
        cfg->auto_revive.store(autoRevive);

        ImGui::EndChild();
    }

    ImGui::Columns(1);
}

// ============================================================
// 補給分頁
// ============================================================
static void PageSupply() {
    BotConfig* cfg = GetBotConfig();

    ImGui::Columns(2, "##SupplyCols", true);
    ImGui::SetColumnWidth(0, 280);

    // 左列：自動補給
    {
        ImGui::BeginChild("##AutoSupply", ImVec2(0, 0), false);
        ImGui::TextColored(COL_ACCENT, "自動補給循環");
        ImGui::Separator();

        bool autoSupply = cfg->auto_supply.load();
        ImGui::Checkbox("啟用自動補給", &autoSupply);
        cfg->auto_supply.store(autoSupply);

        if (autoSupply) {
            bool autoSell = cfg->auto_sell.load();
            bool autoBuy = cfg->auto_buy.load();
            ImGui::Checkbox("  自動賣物", &autoSell);
            ImGui::Checkbox("  自動買藥水", &autoBuy);
            cfg->auto_sell.store(autoSell);
            cfg->auto_buy.store(autoBuy);

            ImGui::Spacing();
            ImGui::TextColored(COL_TEXT_DIM, "城鎮");
            const char* towns[] = { "聖門", "商洞", "玄巖", "鳳凰" };
            int townIdx = cfg->town_index.load();
            ImGui::PushItemWidth(120);
            if (ImGui::Combo("##Town", &townIdx, towns, 4)) {
                cfg->town_index.store(townIdx);
            }
            ImGui::PopItemWidth();
        }

        ImGui::EndChild();
    }

    ImGui::NextColumn();

    // 右列：購買數量
    {
        ImGui::BeginChild("##BuyPanel", ImVec2(0, 0), false);
        ImGui::TextColored(COL_ACCENT, "購買數量");
        ImGui::Separator();

        int buyHp = cfg->buy_hp_qty.load();
        int buyMp = cfg->buy_mp_qty.load();
        int buySp = cfg->buy_sp_qty.load();

        ImGui::TextColored(COL_TEXT_DIM, "HP 藥水");
        ImGui::PushItemWidth(100);
        if (ImGui::InputInt("##BuyHp", &buyHp, 50, 100)) {
            if (buyHp < 0) buyHp = 0;
            cfg->buy_hp_qty.store(buyHp);
        }
        ImGui::PopItemWidth();

        ImGui::TextColored(COL_TEXT_DIM, "MP 藥水");
        ImGui::PushItemWidth(100);
        if (ImGui::InputInt("##BuyMp", &buyMp, 50, 100)) {
            if (buyMp < 0) buyMp = 0;
            cfg->buy_mp_qty.store(buyMp);
        }
        ImGui::PopItemWidth();

        ImGui::TextColored(COL_TEXT_DIM, "SP 藥水");
        ImGui::PushItemWidth(100);
        if (ImGui::InputInt("##BuySp", &buySp, 50, 100)) {
            if (buySp < 0) buySp = 0;
            cfg->buy_sp_qty.store(buySp);
        }
        ImGui::PopItemWidth();

        ImGui::EndChild();
    }

    ImGui::Columns(1);
}

// ═══════════════════════════════════════════════════════════════════
// 座標/物資分頁
// 功能：所有物資買賣相關座標可自訂、F9 量尺抓取、INI 持久化
// ═══════════════════════════════════════════════════════════════════

// 座標欄位結構
struct CoordField {
    const char* label;
    int* xPtr;
    int* yPtr;
    ImU32 color;
};

static void ShowCoordRow(const char* fieldName, const char* displayLabel, int* x, int* y, const ImVec4& rowColor) {
    ImGui::PushID(fieldName);

    // 檢查是否為路由目標（在被點擊的輸入框上自動填入）
    if (s_hasRouting && s_routingFieldId == ImGui::GetID(fieldName)) {
        *x = s_routingX;
        *y = s_routingY;
        s_hasRouting = false;
        UIAddLog("[量尺] 填入 %s = (%d, %d)", displayLabel, s_routingX, s_routingY);
    }

    ImGui::SetNextItemWidth(55);
    ImGui::InputInt("X", x, 0, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(55);
    ImGui::InputInt("Y", y, 0, 0);
    ImGui::SameLine();

    // F9 抓取按鈕
    char btnLabel[32];
    snprintf(btnLabel, sizeof(btnLabel), "F9##%s", fieldName);
    if (ImGui::SmallButton(btnLabel)) {
        ImGuiID id = ImGui::GetID(fieldName);
        EnterCoordCaptureMode((int)id);
    }
    ImGui::SameLine();
    ImGui::TextColored(rowColor, "%s", displayLabel);
    ImGui::PopID();
}

static void PageCoords() {

    SupplyCoords* sc = GetSupplyCoords();

    // ══════════════════ 教程橫幅 ════════════════
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.18f, 0.35f, 1.0f));
    ImGui::BeginChild("##Tutorial", ImVec2(0, 68), false);
    ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "  量尺使用教學");
    ImGui::TextColored(ImVec4(0.7f, 0.8f, 0.9f, 1.0f),
        "  1. 點擊输入框右侧的 [F9] 按钮   2. 在遊戲窗口点击目标位置   3. 坐標自動填入");
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f),
        "  提示: 座標為 1024x768 客戶區相對座標，自動保存到 INI");
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // ════════════════ 功能按鈕列 ════════════════
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.85f, 1.0f));
        if (ImGui::Button("儲存至 INI", ImVec2(100, 28))) {
            if (SaveCoordSettings(sc)) {
                UIAddLog("[座標] 已保存至 INI");
            } else {
                UIAddLog("[座標] 保存失敗");
            }
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("從 INI 載入", ImVec2(100, 28))) {
            if (LoadCoordSettings(sc)) {
                UIAddLog("[座標] 已從 INI 載入");
            } else {
                UIAddLog("[座標] 載入失敗，使用預設值");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("回復預設", ImVec2(90, 28))) {
            *sc = SupplyCoords{};
            UIAddLog("[座標] 已回復預設值");
        }

        ImGui::SameLine(280);
        bool capturing = IsCoordCaptureActive();
        if (capturing) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
            ImGui::Text("  [F9 量尺工作中... 點擊遊戲視窗]");
            ImGui::PopStyleColor();
        }
    }

    ImGui::Spacing();

    // ════════════════ 左欄 ════════════════
    ImGui::Columns(2, "##CoordsCols", true);
    ImGui::SetColumnWidth(0, 320);

    {
        ImGui::BeginChild("##NPCSection", ImVec2(0, 0), false);

        // Phase 0: NPC 對話
        if (ImGui::CollapsingHeader("NPC 對話 (Phase 0)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ShowCoordRow("npcLeft",   "NPC 左鍵",   &sc->npcLeftX,   &sc->npcLeftY,   COL_TEXT);
            ShowCoordRow("npcRight",  "NPC 右鍵",   &sc->npcRightX,  &sc->npcRightY,  COL_TEXT);
            ShowCoordRow("buyBtn",    "購買按鈕",   &sc->buyBtnX,     &sc->buyBtnY,    COL_ORANGE);
        }

        ImGui::Spacing();

        // Phase 2: 賣物
        if (ImGui::CollapsingHeader("自動賣物 (Phase 2)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ShowCoordRow("sellTab",   "賣物分頁",   &sc->sellTabX,    &sc->sellTabY,    COL_TEXT);
            ShowCoordRow("sellBtn",  "賣物按鈕",   &sc->sellBtnX,    &sc->sellBtnY,    COL_TEXT);
            ShowCoordRow("sellConf", "賣物確認",   &sc->sellConfirmX, &sc->sellConfirmY, COL_TEXT);
        }

        ImGui::Spacing();

        // 寵物餵食
        if (ImGui::CollapsingHeader("寵物餵食", ImGuiTreeNodeFlags_DefaultOpen)) {
            BotConfig* petCfg = GetBotConfig();

            // 說明
            ImGui::TextColored(COL_ORANGE, "背包布局:");
            ImGui::TextColored(COL_TEXT_DIM, "1格:寵物卡 2格:飼料");
            ImGui::TextColored(COL_TEXT_DIM, "3格:HP 4格:MP 5格:SP 6格:箭/符");

            ImGui::Spacing();

            // 開關
            bool feedEnabled = petCfg->feed_pet.load();
            ImGui::TextColored(COL_TEXT_DIM, "啟用餵食");
            ImGui::SameLine();
            if (ImGui::Checkbox("##FeedPet", &feedEnabled)) {
                petCfg->feed_pet.store(feedEnabled);
            }

            // 間隔
            int interval = petCfg->feed_pet_interval.load();
            ImGui::TextColored(COL_TEXT_DIM, "餵食間隔 (秒)");
            ImGui::PushItemWidth(100);
            if (ImGui::InputInt("##FeedInterval", &interval, 10, 30)) {
                if (interval < 10) interval = 10;
                if (interval > 600) interval = 600;
                petCfg->feed_pet_interval.store(interval);
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ShowCoordRow("petCard",  "寵物卡",    &sc->petCardX,  &sc->petCardY,  COL_TEXT);
            ShowCoordRow("petFood",  "寵物食物",  &sc->petFoodX,  &sc->petFoodY,  COL_TEXT);
        }

        ImGui::EndChild();
    }

    ImGui::NextColumn();

    // ════════════════ 右欄 ════════════════
    {
        ImGui::BeginChild("##BuySection", ImVec2(0, 0), false);

        // Phase 3: 買藥
        if (ImGui::CollapsingHeader("自動買藥 (Phase 3)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ShowCoordRow("consTab",  "消耗品分頁", &sc->consumableTabX, &sc->consumableTabY, COL_TEXT);
            ImGui::Spacing();
            ShowCoordRow("hpItem",   "HP 商品",    &sc->hpItemX,   &sc->hpItemY,   ImColor(255, 80, 80).Value);
            ShowCoordRow("mpItem",   "MP 商品",    &sc->mpItemX,   &sc->mpItemY,   ImColor(80, 80, 255).Value);
            ShowCoordRow("spItem",   "SP 商品",    &sc->spItemX,   &sc->spItemY,   ImColor(80, 255, 80).Value);
            ImGui::Spacing();
            ShowCoordRow("qtyInput", "數量輸入框", &sc->qtyInputX,  &sc->qtyInputY,  COL_TEXT);
            ShowCoordRow("buyConf",  "購買確認",  &sc->buyConfirmX, &sc->buyConfirmY, COL_GREEN);
        }

        ImGui::EndChild();
    }

    ImGui::Columns(1);
}

// ============================================================
// 系統分頁
// ============================================================
static void PageSystem() {
    BotConfig* cfg = GetBotConfig();

    ImGui::Columns(2, "##SystemCols", true);
    ImGui::SetColumnWidth(0, 280);

    // 左列：反PK + 注入
    {
        ImGui::BeginChild("##AntiPk", ImVec2(0, 0), false);
        ImGui::TextColored(COL_ACCENT, "反 PK 設定");
        ImGui::Separator();

        bool antiPk = cfg->anti_pk_enable.load();
        ImGui::Checkbox("啟用反 PK", &antiPk);
        cfg->anti_pk_enable.store(antiPk);

        ImGui::Spacing();
        ImGui::TextColored(COL_ACCENT, "安全碼偵測");
        ImGui::Separator();

        bool safeCode = cfg->safe_code_enable.load();
        ImGui::Checkbox("啟用安全碼偵測", &safeCode);
        cfg->safe_code_enable.store(safeCode);

        ImGui::Spacing();
        ImGui::TextColored(COL_ACCENT, "插件驗證");
        ImGui::Separator();

        static bool s_pluginVerified = false;
        static char s_pluginStatus[256] = {0};
        static bool s_autoVerify = false;

        ImGui::TextColored(s_pluginVerified ? COL_GREEN : COL_RED,
            s_pluginVerified ? "已驗證" : "未驗證");

        ImGui::Checkbox("自動驗證", &s_autoVerify);

        if (ImGui::Button("驗證插件", ImVec2(150, 25))) {
            extern DWORD GetGamePID();
            DWORD pid = GetGamePID();

            if (pid == 0) {
                strcpy_s(s_pluginStatus, "錯誤：未連接遊戲！");
            } else {
                extern bool TryVerifyPlugin(DWORD pid, char* errorMsg, int errorMsgSize);
                char error[256] = {0};

                if (TryVerifyPlugin(pid, error, sizeof(error))) {
                    s_pluginVerified = true;
                    strcpy_s(s_pluginStatus, "驗證成功！");
                    UIAddLog("[Plugin] 插件驗證成功 (PID=%lu)", pid);
                } else {
                    strcpy_s(s_pluginStatus, error);
                    UIAddLog("[Plugin] 驗證失敗: %s", error);
                }
            }
        }

        if (s_pluginStatus[0]) {
            ImGui::TextColored(COL_TEXT_DIM, s_pluginStatus);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 鎖定遊戲視窗
        static bool s_windowLocked = false;
        if (ImGui::Button(s_windowLocked ? "解除鎖定" : "鎖定視窗", ImVec2(150, 25))) {
            GameHandle gh = GetGameHandle();
            if (gh.hWnd) {
                s_windowLocked = !s_windowLocked;
                if (s_windowLocked) {
                    FocusGameWindow(gh.hWnd);
                    UIAddLog("[鎖定] 已鎖定遊戲視窗");
                } else {
                    UIAddLog("[鎖定] 已解除視窗鎖定");
                }
            } else {
                UIAddLog("[鎖定] 錯誤：未連接遊戲！");
            }
        }
        ImGui::SameLine();
        ImGui::TextColored(s_windowLocked ? COL_GREEN : COL_TEXT_DIM,
            s_windowLocked ? "[已鎖定]" : "[未鎖定]");

        ImGui::EndChild();
    }

    ImGui::NextColumn();

    // 右列：Waypoint 巡航 + DLL 日誌
    {
        ImGui::BeginChild("##Waypoint", ImVec2(0, 0), false);
        ImGui::TextColored(COL_ACCENT, "Waypoint 巡航");
        ImGui::Separator();

        bool waypointPatrol = cfg->waypoint_patrol.load();
        ImGui::Checkbox("啟用 Waypoint 巡航", &waypointPatrol);
        cfg->waypoint_patrol.store(waypointPatrol);

        if (waypointPatrol) {
            int waypointRadius = cfg->waypoint_radius.load();
            ImGui::TextColored(COL_TEXT_DIM, "到達半徑 (格)");
            ImGui::PushItemWidth(180);
            if (ImGui::SliderInt("##WaypointRadius", &waypointRadius, 2, 20, "%d 格")) {
                cfg->waypoint_radius.store(waypointRadius);
            }
            ImGui::PopItemWidth();
        }

        ImGui::Spacing();
        ImGui::TextColored(COL_ACCENT, "快捷鍵");
        ImGui::Separator();
        ImGui::TextColored(COL_TEXT_DIM, "F10 - 隱藏 GUI");
        ImGui::TextColored(COL_TEXT_DIM, "F11 - 暫停/繼續");
        ImGui::TextColored(COL_TEXT_DIM, "F12 - 緊急停止");

        ImGui::EndChild();
    }

    ImGui::Columns(1);

    // ════════════════ 即時 DLL 日誌視窗 ════════════════
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(COL_ACCENT, "即時 DLL / NetHook 日誌");
    ImGui::SameLine();
    static bool s_autoScroll = true;
    ImGui::Checkbox("自動滾動", &s_autoScroll);
    ImGui::SameLine();
    if (ImGui::Button("清除", ImVec2(60, 20))) {
        ClearDllLogBuffer();
    }

    // 日誌顯示區域
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.08f, 1.0f));
    ImGui::BeginChild("##DllLogViewer", ImVec2(0, 200), true);

    // 讀取並顯示日誌
    DrawDllLogs(s_autoScroll);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ============================================================
// 必要的函數實現（供其他模組使用）
// ============================================================
static const int MAX_LOG_LINES = 100;
static char s_logBuffer[MAX_LOG_LINES][512];
static int s_logHead = 0;
static int s_logCount = 0;
static CRITICAL_SECTION s_logCs;
static bool s_logCsInited = false;

void UIAddLog(const char* fmt, ...) {
    if (!s_logCsInited) {
        InitializeCriticalSection(&s_logCs);
        s_logCsInited = true;
    }
    EnterCriticalSection(&s_logCs);
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_logBuffer[s_logHead], sizeof(s_logBuffer[0]), fmt, args);
    va_end(args);
    s_logHead = (s_logHead + 1) % MAX_LOG_LINES;
    if (s_logCount < MAX_LOG_LINES) s_logCount++;
    LeaveCriticalSection(&s_logCs);
}

void FlushAllLogs() {
    if (!s_logCsInited) return;
    EnterCriticalSection(&s_logCs);
    for (int i = 0; i < s_logCount; i++) {
        int idx = (s_logHead - s_logCount + i + MAX_LOG_LINES) % MAX_LOG_LINES;
        OutputDebugStringA(s_logBuffer[idx]);
    }
    LeaveCriticalSection(&s_logCs);
}

// ============================================================
// DLL / NetHook 日誌查看器
// ============================================================
#define DLL_LOG_MAX_LINES 200
#define DLL_LOG_LINE_SZ 512

static char s_dllLogBuffer[DLL_LOG_MAX_LINES][DLL_LOG_LINE_SZ];
static int s_dllLogHead = 0;
static int s_dllLogCount = 0;
static CRITICAL_SECTION s_dllLogCs;
static bool s_dllLogCsInited = false;
static bool s_dllLogConnected = false;

static void InitDllLogCs() {
    if (!s_dllLogCsInited) {
        InitializeCriticalSection(&s_dllLogCs);
        s_dllLogCsInited = true;
    }
}

static void AddDllLogLine(const char* line) {
    if (!line) return;
    InitDllLogCs();
    EnterCriticalSection(&s_dllLogCs);
    strncpy_s(s_dllLogBuffer[s_dllLogHead], DLL_LOG_LINE_SZ - 1, line, DLL_LOG_LINE_SZ - 1);
    s_dllLogBuffer[s_dllLogHead][DLL_LOG_LINE_SZ - 1] = '\0';
    s_dllLogHead = (s_dllLogHead + 1) % DLL_LOG_MAX_LINES;
    if (s_dllLogCount < DLL_LOG_MAX_LINES) s_dllLogCount++;
    LeaveCriticalSection(&s_dllLogCs);
}

void ClearDllLogBuffer() {
    InitDllLogCs();
    EnterCriticalSection(&s_dllLogCs);
    s_dllLogHead = 0;
    s_dllLogCount = 0;
    memset(s_dllLogBuffer, 0, sizeof(s_dllLogBuffer));
    LeaveCriticalSection(&s_dllLogCs);
}

void DrawDllLogs(bool autoScroll) {
    static DWORD s_lastReadShmem = 0;
    static DWORD s_lastReadFile = 0;
    static int s_lastInvCount = -1;
    DWORD now = GetTickCount();

    // 每 200ms 讀取一次共享記憶體日誌
    if (now - s_lastReadShmem > 200) {
        s_lastReadShmem = now;

        // 嘗試從共享記憶體讀取日誌
        ShmemLogEntry entries[32];
        int n = NetHookShmem_ReadLogs(entries, 32);
        if (n > 0) {
            InitDllLogCs();
            EnterCriticalSection(&s_dllLogCs);
            for (int i = 0; i < n; i++) {
                ShmemLogEntry& e = entries[i];
                char levelChar = 'I';
                if (e.level == 1) levelChar = 'W';
                else if (e.level == 2) levelChar = 'E';
                e.message[247] = '\0';
                char line[DLL_LOG_LINE_SZ];
                snprintf(line, DLL_LOG_LINE_SZ - 1, "[%c] %s", levelChar, e.message);
                strncpy_s(s_dllLogBuffer[s_dllLogHead], DLL_LOG_LINE_SZ - 1, line, DLL_LOG_LINE_SZ - 1);
                s_dllLogBuffer[s_dllLogHead][DLL_LOG_LINE_SZ - 1] = '\0';
                s_dllLogHead = (s_dllLogHead + 1) % DLL_LOG_MAX_LINES;
                if (s_dllLogCount < DLL_LOG_MAX_LINES) s_dllLogCount++;
            }
            LeaveCriticalSection(&s_dllLogCs);
        }
    }

    // 每秒讀取一次 DLL 日誌檔案
    if (now - s_lastReadFile > 1000) {
        s_lastReadFile = now;

        // 庫存變化提示
        int invCount = NetHookShmem_GetInventoryCount();
        if (invCount != s_lastInvCount) {
            s_lastInvCount = invCount;
            char invLine[DLL_LOG_LINE_SZ];
            snprintf(invLine, DLL_LOG_LINE_SZ - 1, "[INFO] 庫存更新: %d 物品", invCount);
            AddDllLogLine(invLine);
        }

        // 讀取 DLL 日誌檔案
        char dllLogBuf[8192];
        int len = ReadDllLogFile(dllLogBuf, sizeof(dllLogBuf));
        if (len > 0) {
            // 只取最後幾行
            const char* ptr = dllLogBuf + len;
            int lines = 0;
            while (ptr > dllLogBuf && lines < 5) {
                if (*ptr == '\n' || *ptr == '\r') lines++;
                ptr--;
            }
            if (ptr < dllLogBuf + len - 1) ptr++;

            char line[DLL_LOG_LINE_SZ];
            const char* end = dllLogBuf + len;
            while (ptr < end && lines > 0) {
                const char* lineStart = ptr;
                while (ptr < end && *ptr != '\n' && *ptr != '\r') ptr++;
                size_t lineLen = ptr - lineStart;
                if (lineLen > 0 && lineLen < DLL_LOG_LINE_SZ - 1) {
                    memcpy(line, lineStart, lineLen);
                    line[lineLen] = '\0';
                    // 去除空白
                    char* trim = line + lineLen - 1;
                    while (trim >= line && (*trim == ' ' || *trim == '\t' || *trim == '\n' || *trim == '\r')) {
                        *trim = '\0';
                        trim--;
                    }
                    if (strlen(line) > 0) {
                        AddDllLogLine(line);
                    }
                }
                while (ptr < end && (*ptr == '\n' || *ptr == '\r')) ptr++;
            }
        }

        // 連接狀態提示
        bool connected = NetHookShmem_IsConnected();
        if (connected != s_dllLogConnected) {
            s_dllLogConnected = connected;
            char connLine[DLL_LOG_LINE_SZ];
            snprintf(connLine, DLL_LOG_LINE_SZ - 1, "[%s] NetHook 共享記憶體 %s",
                connected ? "INFO" : "WARN", connected ? "已連接" : "未連接");
            AddDllLogLine(connLine);
        }
    }

    // 顯示日誌
    InitDllLogCs();
    EnterCriticalSection(&s_dllLogCs);
    int displayCount = s_dllLogCount;
    LeaveCriticalSection(&s_dllLogCs);

    if (displayCount == 0) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  (無日誌，等待 NetHook DLL 输出...)");
    } else {
        ImGuiListClipper clipper;
        clipper.Begin(displayCount);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                int idx = (s_dllLogHead - s_dllLogCount + i + DLL_LOG_MAX_LINES) % DLL_LOG_MAX_LINES;
                const char* line = s_dllLogBuffer[idx];

                ImVec4 color = COL_TEXT;
                if (strncmp(line, "[E]", 3) == 0) color = COL_RED;
                else if (strncmp(line, "[W]", 3) == 0) color = COL_ORANGE;
                else if (strncmp(line, "[I]", 3) == 0) color = COL_TEXT;

                ImGui::TextColored(color, "%s", line);
            }
        }
        // 自動滾動到底部
        if (autoScroll) ImGui::SetScrollHereY(1.0f);
    }
}

// ============================================================
// 熱鍵系統 (RegisterHotKey - 全局熱鍵)
// ============================================================
#include <process.h>

// 熱鍵 ID（必須 > 0）
static volatile bool s_hotkeyThreadRunning = false;
static HANDLE s_hotkeyThread = NULL;

static DWORD WINAPI HotkeyThreadProc(LPVOID lpParam);
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

// F9 量尺座標抓取公開 API
void EnterCoordCaptureMode(int fieldId) {
    InterlockedExchange(&s_captureMode, 1);
    InterlockedExchange(&s_captureFieldId, fieldId);
    UIAddLog("[量尺] 進入抓取模式 (fieldId=%d)，點擊遊戲視窗", fieldId);
}

void CancelCoordCaptureMode() {
    InterlockedExchange(&s_captureMode, 0);
    InterlockedExchange(&s_captureFieldId, 0);
    if (s_hMouseHookCapture) {
        UnhookWindowsHookEx(s_hMouseHookCapture);
        s_hMouseHookCapture = NULL;
    }
    UIAddLog("[量尺] 已取消抓取模式");
}

bool IsCoordCaptureActive() {
    return InterlockedCompareExchange(&s_captureMode, 0, 0) != 0;
}

void InitHotkeys() {
    // 嘗試找到遊戲窗口（用於座標轉換）- 嘗試多個可能的窗口名
    const wchar_t* windowNames[] = {
        L"亂2 online",     // 繁體
        L"Ran Online",     // 英文
        L"RAN2",           // 簡稱
        L"GAME",           // 其他
    };
    for (int i = 0; i < 4; i++) {
        s_gameHwndForCapture = FindWindowW(NULL, windowNames[i]);
        if (s_gameHwndForCapture) {
            UIAddLog("[熱鍵] 找到遊戲窗口: %S", windowNames[i]);
            break;
        }
    }

    // 使用 RegisterHotKey 註冊全局熱鍵
    // 注意：F10/F11/F12 由 WH_KEYBOARD_LL 鉤子處理（見 LowLevelKeyboardProc）
    // RegisterHotKey 只用於 F9（座標量尺）和 WM_HOTKEY 回調
    HWND hGui = g_hWnd;
    if (hGui) {
        // F9 座標量尺：需要 RegisterHotKey 發送 WM_HOTKEY 訊息
        BOOL okF9 = RegisterHotKey(hGui, HOTKEY_F9, 0, VK_F9);
        // F10/F11/F12 由鍵盤鉤子 LowLevelKeyboardProc 處理，避免衝突不註冊
        UIAddLog("[熱鍵] F9=RegisterHotKey, F10/F11/F12=鍵盤鉤子");
    } else {
        UIAddLog("[熱鍵] GUI 視窗尚未建立，略過 RegisterHotKey");
    }

    // 啟動滑鼠監聽執行緒（用於 F9 量尺抓取模式）
    s_hotkeyThreadRunning = true;
    s_hotkeyThread = CreateThread(NULL, 0, HotkeyThreadProc, NULL, 0, NULL);
    if (!s_hotkeyThread) {
        UIAddLog("[錯誤] 熱鍵執行緒建立失敗");
    } else {
        UIAddLog("[熱鍵] 熱鍵系統已啟動");
    }
}

void ShutdownHotkeys() {
    s_hotkeyThreadRunning = false;

    // 註銷全局熱鍵（只有 F9 需要註銷，F10/F11/F12 由鍵盤鉤子處理）
    HWND hGui = g_hWnd;
    if (hGui) {
        UnregisterHotKey(hGui, HOTKEY_F9);
    }

    if (s_hMouseHookCapture) {
        UnhookWindowsHookEx(s_hMouseHookCapture);
        s_hMouseHookCapture = NULL;
    }

    // 鍵盤鉤子由 HotkeyThreadProc 在退出時自動卸載，這裡再確保一次
    if (s_hKeyboardHook) {
        UnhookWindowsHookEx(s_hKeyboardHook);
        s_hKeyboardHook = NULL;
    }

    if (s_hotkeyThread) {
        WaitForSingleObject(s_hotkeyThread, 2000);
        CloseHandle(s_hotkeyThread);
        s_hotkeyThread = NULL;
    }

    InterlockedExchange(&s_captureMode, 0);
    InterlockedExchange(&s_captureFieldId, 0);

    UIAddLog("[熱鍵] 熱鍵系統已關閉");
}

static DWORD WINAPI HotkeyThreadProc(LPVOID lpParam) {
    // 安裝 WH_KEYBOARD_LL 鍵盤鉤子（遊戲在前台也能收到按鍵）
    s_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    if (!s_hKeyboardHook) {
        UIAddLog("[錯誤] 無法安裝鍵盤鉤子");
    } else {
        UIAddLog("[熱鍵] 鍵盤鉤子已啟動 (F10/F11/F12)");
    }

    // 安裝 WH_MOUSE_LL 滑鼠鉤子（F9 量尺抓取）
    s_hMouseHookCapture = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(NULL), 0);
    if (!s_hMouseHookCapture) {
        UIAddLog("[錯誤] 無法安裝滑鼠鉤子");
    } else {
        UIAddLog("[熱鍵] 滑鼠鉤子已啟動 (F9 量尺)");
    }

    // 訊息迴圈（用於滑鼠抓取模式）
    MSG msg;
    while (s_hotkeyThreadRunning && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 卸載鍵盤鉤子
    if (s_hKeyboardHook) {
        UnhookWindowsHookEx(s_hKeyboardHook);
        s_hKeyboardHook = NULL;
    }
    return 0;
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN) {
            if (InterlockedCompareExchange(&s_captureMode, 0, 0) != 0) {
                // 正在等待點擊 → 攔截
                MSLLHOOKSTRUCT* mhs = (MSLLHOOKSTRUCT*)lParam;
                int scrX = mhs->pt.x;
                int scrY = mhs->pt.y;

                // 嘗試把螢幕座標轉換成遊戲窗口客戶區座標
                HWND hTarget = s_gameHwndForCapture;
                if (!hTarget || !IsWindow(hTarget)) {
                    hTarget = WindowFromPoint(POINT{ scrX, scrY });
                }

                int cliX = scrX;
                int cliY = scrY;
                if (hTarget && IsWindow(hTarget)) {
                    ScreenToClient(hTarget, (POINT*)&cliX);
                }

                // 卸載滑鼠鉤子
                if (s_hMouseHookCapture) {
                    UnhookWindowsHookEx(s_hMouseHookCapture);
                    s_hMouseHookCapture = NULL;
                }
                InterlockedExchange(&s_captureMode, 0);

                // 發送到 GUI 窗口（跨執行緒安全）
                int fieldId = InterlockedCompareExchange(&s_captureFieldId, 0, 0);
                InterlockedExchange(&s_captureFieldId, 0);

                HWND hGui = g_hWnd;
                if (hGui && IsWindow(hGui)) {
                    // wParam = fieldId, lParam = MAKELPARAM(x, y)
                    PostMessageA(hGui, WM_COORDCAPTURE, (WPARAM)fieldId, MAKELPARAM(cliX, cliY));
                }

                return 1; // 攔截這個點擊，不送達遊戲
            }
        }
    }
    return CallNextHookEx(s_hMouseHookCapture, nCode, wParam, lParam);
}

// WH_KEYBOARD_LL 鍵盤鉤子回調（遊戲在前台也能收到按鍵）
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && lParam) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = kb->vkCode;
        bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

        // ✅ 使用時間戳防抖，避免 static 變量狀態不一致
        static DWORD s_lastF10Time = 0;
        static DWORD s_lastF11Time = 0;
        static DWORD s_lastF12Time = 0;
        const DWORD HOTKEY_COOLDOWN = 300; // 300ms 防抖

        DWORD now = GetTickCount();

        // F10: 切換顯示/隱藏 GUI
        if (vkCode == VK_F10) {
            if (isUp) { return 1; }
            if (!isDown) return 1;
            if (now - s_lastF10Time < HOTKEY_COOLDOWN) return 1;
            s_lastF10Time = now;

            // 切換顯示/隱藏
            g_guiVisible = !g_guiVisible;
            // ✅ 重置輸入系統的隱藏標誌，避免衝突
            ResetInputWindowHiddenFlag();
            if (g_guiVisible) {
                ShowWindow(g_hWnd, SW_SHOW);
                SetForegroundWindow(g_hWnd);
                UIAddLog("[熱鍵] F10 顯示 GUI");
            } else {
                ShowWindow(g_hWnd, SW_HIDE);
                UIAddLog("[熱鍵] F10 隱藏 GUI");
            }
            return 1;
        }

        // 檢查工具窗口是否可見且獲得焦點
        bool guiFocused = g_guiVisible && (GetForegroundWindow() == g_hWnd);

        // F11: 暫停/繼續 Bot
        if (vkCode == VK_F11) {
            if (isUp) { return 1; }
            if (!isDown) return 1;
            if (now - s_lastF11Time < HOTKEY_COOLDOWN) return 1;
            s_lastF11Time = now;

            // 如果工具窗口可見且獲得焦點，切換到遊戲後再執行
            if (guiFocused) {
                // 先隱藏工具窗口，切換到遊戲
                g_guiVisible = false;
                ShowWindow(g_hWnd, SW_HIDE);
                // 通知 Bot 切換（不下發到遊戲）
            }

            ToggleBotActive();
            UIAddLog("[熱鍵] F11 ToggleBotActive");
            return 1;  // 攔截按鍵，避免送達遊戲
        }

        // F12: 緊急停止
        if (vkCode == VK_F12) {
            if (isUp) { return 1; }
            if (!isDown) return 1;
            if (now - s_lastF12Time < HOTKEY_COOLDOWN) return 1;
            s_lastF12Time = now;

            // 如果工具窗口可見且獲得焦點，切換到遊戲後再執行
            if (guiFocused) {
                // 先隱藏工具窗口，切換到遊戲
                g_guiVisible = false;
                ShowWindow(g_hWnd, SW_HIDE);
            }

            ForceStopBot();
            UIAddLog("[熱鍵] F12 緊急停止");
            return 1;  // 攔截按鍵，避免送達遊戲
        }
    }
    return CallNextHookEx(s_hKeyboardHook, nCode, wParam, lParam);
}

void SetGameHwndForCalib(HWND hwnd) {
    CoordCalibrator::Instance().SetGameHwnd(hwnd);
}
    