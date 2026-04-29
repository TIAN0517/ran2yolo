// ============================================================
// 授權管理工具 GUI - 現代化 UI 重構版
// ============================================================
#include "license_admin_gui.h"
#include "offline_license.h"
#include "resource.h"
#include <stdio.h>
#include <string>
#include <vector>
#include <ShlObj.h>
#include <Commdlg.h>
#include <Objbase.h>
#include <windows.h>
#include <commctrl.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Comctl32.lib")

#define MAX4096 4096

// ============================================================
// 配色方案 - 現代深色主題
// ============================================================
namespace Colors {
    const DWORD BG_DARK      = RGB(18, 18, 24);      // 深色背景
    const DWORD BG_CARD      = RGB(28, 28, 36);      // 卡片背景
    const DWORD BG_INPUT     = RGB(22, 22, 30);      // 輸入框背景
    const DWORD BORDER       = RGB(60, 60, 80);      // 邊框
    const DWORD BORDER_FOCUS = RGB(100, 140, 255);   // 聚焦邊框
    const DWORD TEXT_MAIN    = RGB(240, 240, 250);   // 主文字
    const DWORD TEXT_SEC     = RGB(160, 160, 180);   // 次要文字
    const DWORD TEXT_MUTED   = RGB(100, 100, 120);   // 弱化文字
    const DWORD ACCENT_BLUE  = RGB(70, 130, 255);    // 強調藍
    const DWORD ACCENT_GREEN = RGB(80, 200, 120);    // 成功綠
    const DWORD ACCENT_RED   = RGB(255, 100, 100);  // 錯誤紅
    const DWORD ACCENT_ORANGE= RGB(255, 180, 80);    // 警告橙
}

// ============================================================
// 全域狀態
// ============================================================
HWND g_adminHwnd = NULL;
static std::vector<std::wstring> s_logLines;
static HWND s_hwndLog = NULL;
static HWND s_hwndStatus = NULL;
static HWND s_hwndProgress = NULL;
static HWND s_hwndKeyOutput = NULL;
static bool s_initialized = false;

// 控制項 ID
enum CTRL_ID {
    ID_TITLE_BANNER = 100,
    ID_SEP_TITLE,
    ID_LABEL_HWID, ID_EDIT_HWID,
    ID_LABEL_DAYS, ID_EDIT_DAYS,
    ID_LABEL_PERMS, ID_EDIT_PERMS,
    ID_BTN_GENKEY, ID_BTN_ISSUE, ID_BTN_EXPORT, ID_BTN_COPY,
    ID_SEP_ACTION,
    ID_LABEL_TOKEN, ID_KEY_OUTPUT,
    ID_SEP_LOG,
    ID_LABEL_LOG,
    ID_LOG_LIST,
    ID_STATUS_BAR,
    ID_ICON_HWID, ID_ICON_DAYS, ID_ICON_PERMS, ID_ICON_KEY
};

// ============================================================
// 字體
// ============================================================
static HFONT g_fontTitle = NULL;
static HFONT g_fontSubtitle = NULL;
static HFONT g_fontBody = NULL;
static HFONT g_fontMono = NULL;
static HFONT g_fontButton = NULL;
static HFONT g_fontCaption = NULL;

static void InitFonts() {
    if (g_fontTitle) return;

    LOGFONTW lf = {};

    // 標題字體
    lf.lfHeight = -28;
    lf.lfWeight = FW_BOLD;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    g_fontTitle = CreateFontIndirectW(&lf);

    // 副標題
    lf.lfHeight = -14;
    lf.lfWeight = FW_NORMAL;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    g_fontSubtitle = CreateFontIndirectW(&lf);

    // 正文
    lf.lfHeight = -13;
    lf.lfWeight = FW_NORMAL;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    g_fontBody = CreateFontIndirectW(&lf);

    // 等寬字體 (輸入/輸出)
    lf.lfHeight = -13;
    lf.lfWeight = FW_NORMAL;
    wcscpy_s(lf.lfFaceName, L"Consolas");
    g_fontMono = CreateFontIndirectW(&lf);

    // 按鈕
    lf.lfHeight = -14;
    lf.lfWeight = FW_SEMIBOLD;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    g_fontButton = CreateFontIndirectW(&lf);

    // 標籤
    lf.lfHeight = -12;
    lf.lfWeight = FW_MEDIUM;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    g_fontCaption = CreateFontIndirectW(&lf);
}

static void SetControlFont(HWND hwnd, HFONT font) {
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

static void SetControlColors(HWND hwnd, DWORD bgColor, DWORD textColor) {
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
}

// ============================================================
// 自訂按鈕結構
// ============================================================
struct ButtonStyle {
    int id;
    const wchar_t* text;
    int x, y, w, h;
    DWORD bgColor;
    DWORD hoverColor;
    DWORD textColor;
    bool isPrimary;
};

static const ButtonStyle g_buttons[] = {
    { ID_BTN_GENKEY,  L"產生金鑰對",  0, 0, 130, 42, Colors::ACCENT_BLUE,   RGB(90, 160, 255), RGB(255,255,255), true  },
    { ID_BTN_ISSUE,   L"發    卡",    0, 0, 130, 42, Colors::ACCENT_GREEN,  RGB(100, 220, 140), RGB(255,255,255), true  },
    { ID_BTN_EXPORT,  L"匯    出",    0, 0, 100, 36, Colors::BG_CARD,        RGB(50, 50, 70),   Colors::TEXT_MAIN, false },
    { ID_BTN_COPY,    L"復    製",    0, 0, 100, 36, Colors::BG_CARD,        RGB(50, 50, 70),   Colors::TEXT_MAIN, false },
};

// ============================================================
// 工具函式
// ============================================================
static void AddLog(const wchar_t* msg) {
    s_logLines.push_back(msg);
    if (s_hwndLog) {
        SendMessageW(s_hwndLog, LB_ADDSTRING, 0, (LPARAM)msg);
        SendMessageW(s_hwndLog, LB_SETCURSEL, s_logLines.size() - 1, 0);
    }
}

static void AddLogFmt(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list args;
    va_start(args, fmt);
    vswprintf_s(buf, fmt, args);
    va_end(args);
    AddLog(buf);
}

static void SetStatus(const wchar_t* msg) {
    if (s_hwndStatus) {
        SetWindowTextW(s_hwndStatus, msg);
    }
}

static std::wstring GetDlgTextW(HWND hwnd, int id) {
    wchar_t buf[4096] = {};
    GetDlgItemTextW(hwnd, id, buf, sizeof(buf) / sizeof(wchar_t));
    return std::wstring(buf);
}

static void ShowMsg(HWND parent, const wchar_t* title, const wchar_t* msg, bool isError = false) {
    MessageBoxW(parent, msg, title, MB_OK | (isError ? MB_ICONERROR : MB_ICONINFORMATION));
}

static std::string WideToUtf8(const wchar_t* wstr) {
    if (!wstr || !wstr[0]) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, NULL, NULL);
    return result;
}

static std::wstring Utf8ToWide(const char* str) {
    if (!str || !str[0]) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    if (len <= 0) return L"";
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str, -1, &result[0], len);
    return result;
}

static bool GetModuleDirW(wchar_t* outDir, size_t outSize) {
    if (!outDir || outSize < MAX_PATH) return false;
    DWORD len = GetModuleFileNameW(NULL, outDir, (DWORD)outSize);
    if (len == 0 || len >= outSize) return false;
    wchar_t* slash = wcsrchr(outDir, L'\\');
    if (!slash) slash = wcsrchr(outDir, L'/');
    if (!slash) return false;
    *slash = L'\0';
    return true;
}

static bool WriteUtf8TextFileW(const wchar_t* path, const wchar_t* text) {
    if (!path || !text || !text[0]) return false;
    std::string utf8 = WideToUtf8(text);
    FILE* f = _wfopen(path, L"wb");
    if (!f) return false;
    fwrite(utf8.data(), 1, utf8.size(), f);
    fwrite("\r\n", 1, 2, f);
    fclose(f);
    return true;
}

static void SaveIssuedTokenToModuleDir(const wchar_t* tokenW) {
    wchar_t dir[MAX_PATH] = {};
    if (!GetModuleDirW(dir, sizeof(dir) / sizeof(dir[0]))) {
        AddLog(L"⚠ 無法取得程式目錄，未自動保存卡密");
        return;
    }

    wchar_t tokenPath[MAX_PATH] = {};
    wchar_t lastPath[MAX_PATH] = {};
    swprintf_s(tokenPath, L"%s\\license_token.dat", dir);
    swprintf_s(lastPath, L"%s\\last_license.txt", dir);

    if (WriteUtf8TextFileW(tokenPath, tokenW) && WriteUtf8TextFileW(lastPath, tokenW)) {
        AddLogFmt(L"✓ 已自動保存: %s", tokenPath);
    } else {
        AddLog(L"⚠ 自動保存卡密失敗，請手動匯出");
    }
}

static void ClearKeyOutput() {
    if (s_hwndKeyOutput) {
        SetWindowTextW(s_hwndKeyOutput, L"");
    }
}

// ============================================================
// 功能函式
// ============================================================
static void OnGenKey(HWND hwnd) {
    wchar_t path[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"選擇金鑰儲存目錄";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = NULL;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;

    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);

    AddLog(L"");
    AddLog(L"┌────────────────────────────────────────");
    AddLog(L"│  產生金鑰對");
    AddLog(L"└────────────────────────────────────────");
    SetStatus(L"產生金鑰對中...");

    std::wstring privPath = std::wstring(path) + L"\\license_private.blob";
    std::wstring pubPath = std::wstring(path) + L"\\license_public.blob";

    char err[256] = {};
    if (!OfflineLicenseGenerateKeyPair(
            WideToUtf8(privPath.c_str()).c_str(),
            WideToUtf8(pubPath.c_str()).c_str(),
            2048, err, sizeof(err))) {
        AddLogFmt(L"✗ 失敗: %s", Utf8ToWide(err).c_str());
        ShowMsg(hwnd, L"錯誤", Utf8ToWide(err).c_str(), true);
        SetStatus(L"失敗");
        return;
    }

    AddLogFmt(L"✓ 公鑰: %s", pubPath.c_str());
    AddLogFmt(L"✓ 私鑰: %s", privPath.c_str());
    AddLog(L"└────────────────────────────────────────");
    ShowMsg(hwnd, L"成功",
        L"金鑰對已產生!\n\n"
        L"請將 license_public.blob 放到 JyTrainer.exe 同目錄",
        false);
    SetStatus(L"完成 - 公鑰已就緒");
}

static void OnIssue(HWND hwnd) {
    std::wstring hwidW = GetDlgTextW(hwnd, ID_EDIT_HWID);
    std::wstring daysStrW = GetDlgTextW(hwnd, ID_EDIT_DAYS);
    std::wstring permsW = GetDlgTextW(hwnd, ID_EDIT_PERMS);

    if (hwidW.empty()) {
        ShowMsg(hwnd, L"輸入錯誤", L"請輸入 HWID", true);
        return;
    }

    int days = _wtoi(daysStrW.c_str());
    if (days <= 0) {
        ShowMsg(hwnd, L"輸入錯誤", L"天數必須 > 0", true);
        return;
    }

    // 讀取私鑰
    wchar_t privPath[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Private Key\0license_private.blob\0All Files\0*.*\0\0";
    ofn.lpstrFile = privPath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_DONTADDTORECENT;
    ofn.lpstrTitle = L"選擇私鑰檔案";

    if (!GetOpenFileNameW(&ofn)) return;

    AddLog(L"");
    AddLog(L"┌────────────────────────────────────────");
    AddLog(L"│  發放卡密");
    AddLog(L"└────────────────────────────────────────");
    AddLogFmt(L"  HWID: %s", hwidW.c_str());
    AddLogFmt(L"  天數: %d", days);
    AddLogFmt(L"  權限: %s", permsW.c_str());
    SetStatus(L"發卡中...");

    char err[256] = {};
    char token[MAX4096] = {};
    if (!OfflineLicenseIssueToken(
            WideToUtf8(privPath).c_str(),
            WideToUtf8(hwidW.c_str()).c_str(),
            days,
            WideToUtf8(permsW.c_str()).c_str(),
            token, sizeof(token), err, sizeof(err))) {
        AddLogFmt(L"✗ 失敗: %s", Utf8ToWide(err).c_str());
        ShowMsg(hwnd, L"錯誤", Utf8ToWide(err).c_str(), true);
        SetStatus(L"失敗");
        return;
    }

    // 顯示卡密
    std::wstring tokenW = Utf8ToWide(token);
    SetDlgItemTextW(hwnd, ID_KEY_OUTPUT, tokenW.c_str());
    AddLog(L"");
    AddLog(L"✓ 發卡成功!");
    AddLogFmt(L"  卡密: %s", tokenW.c_str());
    AddLog(L"└────────────────────────────────────────");
    SaveIssuedTokenToModuleDir(tokenW.c_str());
    SetStatus(L"發卡成功");
}

static void OnExport(HWND hwnd) {
    wchar_t tokenW[MAX4096] = {};
    GetDlgItemTextW(hwnd, ID_KEY_OUTPUT, tokenW, sizeof(tokenW) / sizeof(wchar_t));

    if (!tokenW[0]) {
        ShowMsg(hwnd, L"無卡密", L"先生成一張卡", true);
        return;
    }

    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Text Files\0*.txt\0All Files\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_DONTADDTORECENT;
    ofn.lpstrTitle = L"匯出卡密";
    swprintf_s(path, L"license_%s.txt", GetDlgTextW(hwnd, ID_EDIT_HWID).c_str());

    if (!GetSaveFileNameW(&ofn)) return;

    FILE* f = _wfopen(path, L"w, ccs=UTF-8");
    if (!f) {
        ShowMsg(hwnd, L"錯誤", L"無法寫入檔案", true);
        return;
    }
    fputws(tokenW, f);
    fputc(L'\n', f);
    fclose(f);

    AddLogFmt(L"✓ 已匯出到: %s", path);
    ShowMsg(hwnd, L"成功", L"已匯出卡密!");
}

static void OnCopy(HWND hwnd) {
    wchar_t tokenW[MAX4096] = {};
    GetDlgItemTextW(hwnd, ID_KEY_OUTPUT, tokenW, sizeof(tokenW) / sizeof(wchar_t));

    if (!tokenW[0]) {
        ShowMsg(hwnd, L"無卡密", L"先生成一張卡", true);
        return;
    }

    if (OpenClipboard(hwnd)) {
        EmptyClipboard();

        // UTF-8 格式
        std::string tokenUtf8 = WideToUtf8(tokenW);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, tokenUtf8.size() + 1);
        if (hMem) {
            char* pMem = (char*)GlobalLock(hMem);
            memcpy(pMem, tokenUtf8.c_str(), tokenUtf8.size() + 1);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        }

        // Unicode 格式
        HGLOBAL hWide = GlobalAlloc(GMEM_MOVEABLE, (wcslen(tokenW) + 1) * sizeof(wchar_t));
        if (hWide) {
            wchar_t* pWide = (wchar_t*)GlobalLock(hWide);
            wcscpy_s(pWide, wcslen(tokenW) + 1, tokenW);
            GlobalUnlock(hWide);
            SetClipboardData(CF_UNICODETEXT, hWide);
        }

        CloseClipboard();
        AddLog(L"✓ 已複製到剪貼簿!");
    }
}

// ============================================================
// 建立自訂控制項
// ============================================================
static HWND CreateLabel(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    HWND hwnd = CreateWindowExW(0, L"Static", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, (HMENU)id, NULL, NULL);
    SetControlFont(hwnd, g_fontCaption);
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_fontCaption, TRUE);
    return hwnd;
}

static HWND CreateEdit(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h, bool readOnly = false) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
    if (readOnly) style |= ES_READONLY;

    HWND hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"Edit", text,
        style,
        x, y, w, h, parent, (HMENU)id, NULL, NULL);
    SetControlFont(hwnd, g_fontMono);
    return hwnd;
}

static HWND CreateButton(HWND parent, const ButtonStyle* btn, int x, int y) {
    HWND hwnd = CreateWindowExW(0, L"BUTTON", btn->text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
        x, y, btn->w, btn->h,
        parent, (HMENU)btn->id, NULL, NULL);
    SetControlFont(hwnd, g_fontButton);
    return hwnd;
}

static HWND CreateSeparator(HWND parent, int x, int y, int w) {
    HWND hwnd = CreateWindowExW(0, L"Static", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        x, y, w, 2, parent, NULL, NULL, NULL);
    return hwnd;
}

static HWND CreateCard(HWND parent, int x, int y, int w, int h) {
    HWND hwnd = CreateWindowExW(0, L"Static", L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        x, y, w, h, parent, NULL, NULL, NULL);
    return hwnd;
}

// ============================================================
// 建立控制項
// ============================================================
static void CreateControls(HWND hwnd) {
    InitFonts();

    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right;
    int H = rc.bottom;
    int pad = 20;  // 內邊距
    int cardPad = 16;  // 卡片內邊距

    // ----------------------------------------
    // 標題區域
    // ----------------------------------------
    int y = pad;

    // 主標題
    HWND hTitle = CreateWindowExW(0, L"Static", L"RAN2 Bot 授權管理工具",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, y, W, 36, hwnd, NULL, NULL, NULL);
    SetControlFont(hTitle, g_fontTitle);

    y += 40;
    // 副標題
    HWND hSubtitle = CreateWindowExW(0, L"Static",
        L"離線授權 • 一卡一機 • 安全可靠",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, y, W, 20, hwnd, NULL, NULL, NULL);
    SetControlFont(hSubtitle, g_fontSubtitle);
    SendMessageW(hSubtitle, WM_SETFONT, (WPARAM)g_fontSubtitle, TRUE);

    y += 35;

    // 分隔線
    CreateSeparator(hwnd, pad, y, W - pad * 2);
    y += 16;

    // ----------------------------------------
    // 輸入區域 - 三列佈局
    // ----------------------------------------
    int inputLabelW = 60;
    int inputEditW = W - pad * 2 - inputLabelW - cardPad;
    int inputH = 28;
    int inputGap = 12;

    // HWID
    CreateLabel(hwnd, ID_LABEL_HWID, L"HWID:", pad, y, inputLabelW, 20);
    HWND hEditHwid = CreateEdit(hwnd, ID_EDIT_HWID, L"", pad + inputLabelW, y - 4, inputEditW, inputH);
    y += inputH + inputGap;

    // 天數 + 權限 (同一行)
    int halfW = (W - pad * 2 - inputLabelW) / 2 - 6;
    CreateLabel(hwnd, ID_LABEL_DAYS, L"天數:", pad, y, inputLabelW, 20);
    HWND hEditDays = CreateEdit(hwnd, ID_EDIT_DAYS, L"30", pad + inputLabelW, y - 4, halfW, inputH);

    CreateLabel(hwnd, ID_LABEL_PERMS, L"權限:", pad + inputLabelW + halfW + 12, y, 40, 20);
    HWND hEditPerms = CreateEdit(hwnd, ID_EDIT_PERMS, L"basic", pad + inputLabelW + halfW + 52, y - 4, halfW, inputH);
    y += inputH + inputGap + 8;

    // ----------------------------------------
    // 按鈕區域
    // ----------------------------------------
    CreateSeparator(hwnd, pad, y, W - pad * 2);
    y += 16;

    int btnY = y;
    int btnGap = 12;
    int totalBtnW = 130 + btnGap + 130 + btnGap + 100 + btnGap + 100;
    int btnStartX = (W - totalBtnW) / 2;

    HWND hBtnGen = CreateButton(hwnd, &g_buttons[0], btnStartX, btnY);
    HWND hBtnIssue = CreateButton(hwnd, &g_buttons[1], btnStartX + 130 + btnGap, btnY);
    HWND hBtnExport = CreateButton(hwnd, &g_buttons[2], btnStartX + 130 + 130 + btnGap * 2 + 30, btnY + 3);
    HWND hBtnCopy = CreateButton(hwnd, &g_buttons[3], btnStartX + 130 + 130 + 100 + btnGap * 3 + 30, btnY + 3);
    y += 50;

    // ----------------------------------------
    // 卡密輸出區域
    // ----------------------------------------
    y += 8;
    CreateLabel(hwnd, ID_LABEL_TOKEN, L"卡密輸出:", pad, y, 80, 20);
    y += 24;

    s_hwndKeyOutput = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", L"",
        WS_CHILD | WS_VISIBLE | ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        pad, y, W - pad * 2, 80, hwnd, (HMENU)ID_KEY_OUTPUT, NULL, NULL);
    SetControlFont(s_hwndKeyOutput, g_fontMono);

    y += 88;

    // ----------------------------------------
    // 日誌區域
    // ----------------------------------------
    CreateSeparator(hwnd, pad, y, W - pad * 2);
    y += 12;

    CreateLabel(hwnd, ID_LABEL_LOG, L"操作日誌:", pad, y, 80, 20);
    y += 24;

    s_hwndLog = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"ListBox", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_USETABSTOPS,
        pad, y, W - pad * 2, H - y - 50,
        hwnd, (HMENU)ID_LOG_LIST, NULL, NULL);
    SetControlFont(s_hwndLog, g_fontMono);

    // 狀態列
    s_hwndStatus = CreateWindowExW(0, L"Static", L"就緒",
        WS_CHILD | WS_VISIBLE | SS_LEFT | WS_BORDER,
        0, H - 32, W, 32, hwnd, NULL, NULL, NULL);
    SendMessageW(s_hwndStatus, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

    // ----------------------------------------
    // 初始化日誌
    // ----------------------------------------
    AddLog(L"╔════════════════════════════════════════╗");
    AddLog(L"║      RAN2 Bot 授權管理工具 v2.0          ║");
    AddLog(L"╚════════════════════════════════════════╝");
    AddLog(L"");
    AddLog(L"使用說明:");
    AddLog(L"1. 點擊「產生金鑰對」建立 RSA 金鑰");
    AddLog(L"2. 將 license_public.blob 放到 JyTrainer 目錄");
    AddLog(L"3. 填入玩家 HWID、天數、權限");
    AddLog(L"4. 點擊「發卡」產生授權卡密");
    AddLog(L"5. 使用「複製」或「匯出」保存卡密");
    AddLog(L"");
    AddLog(L"權限等級: basic | premium | ultimate");
    AddLog(L"");
    SetStatus(L"就緒 - 請開始操作");
}

// ============================================================
// 繪製消息處理
// ============================================================
static LRESULT OnCtrlColor(HDC hdc, HWND hwnd) {
    DWORD bg = Colors::BG_DARK;
    SetBkColor(hdc, bg);
    SetTextColor(hdc, Colors::TEXT_MAIN);

    static HBRUSH hBrush = NULL;
    if (!hBrush) hBrush = CreateSolidBrush(bg);
    return (LRESULT)hBrush;
}

static LRESULT OnDrawItem(WPARAM wParam, LPARAM lParam) {
    return 0;
}

// ============================================================
// 視窗處理
// ============================================================
LRESULT CALLBACK AdminWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateControls(hWnd);
        s_initialized = true;
        return 0;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        return OnCtrlColor((HDC)wParam, (HWND)lParam);

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (code == BN_CLICKED) {
            switch (id) {
            case ID_BTN_GENKEY: OnGenKey(hWnd); break;
            case ID_BTN_ISSUE:  OnIssue(hWnd);  break;
            case ID_BTN_EXPORT: OnExport(hWnd); break;
            case ID_BTN_COPY:   OnCopy(hWnd);   break;
            }
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, CreateSolidBrush(Colors::BG_DARK));
        return 1;
    }
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
// 初始化
// ============================================================
bool InitLicenseAdminGui(HINSTANCE hInstance, int nCmdShow) {
    // 初始化通用控制項
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = AdminWndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(Colors::BG_DARK);
    wcex.lpszClassName = L"LicenseAdminWindow";
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExW(&wcex)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    // 視窗大小 (520x600)
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = 520;
    int winH = 620;
    int winX = (screenW - winW) / 2;
    int winY = (screenH - winH) / 2;

    g_adminHwnd = CreateWindowExW(
        0,
        L"LicenseAdminWindow",
        L"RAN2 Bot 授權管理工具",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        winX, winY, winW, winH,
        NULL, NULL, hInstance, NULL);

    if (!g_adminHwnd) return false;

    ShowWindow(g_adminHwnd, nCmdShow);
    UpdateWindow(g_adminHwnd);
    return true;
}
