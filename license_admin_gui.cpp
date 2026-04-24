// ============================================================
// 授權管理工具 GUI - Unicode 重構版
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

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Ole32.lib")

#define MAX4096 4096

HWND g_adminHwnd = NULL;

// 登入日誌
static std::vector<std::wstring> s_logLines;
static HWND s_hwndLog = NULL;

// 狀態
static HWND s_hwndStatus = NULL;
static bool s_initialized = false;

#define IDC_BTN_GENKEY     1001
#define IDC_BTN_ISSUE      1002
#define IDC_BTN_EXPORT     1003
#define IDC_BTN_COPY       1004
#define IDC_EDIT_HWID      2001
#define IDC_EDIT_DAYS      2002
#define IDC_EDIT_PERMS     2003
#define IDC_EDIT_KEYOUT    2004
#define IDC_EDIT_LOG       2005
#define IDC_STATIC_TITLE  3001
#define IDC_STATIC_LOG     3003

// ============================================================
// 字體
// ============================================================
static HFONT g_fontTitle = NULL;
static HFONT g_fontMono = NULL;
static HFONT g_fontButton = NULL;

static void InitFonts() {
    if (g_fontTitle) return;

    LOGFONTW lf = {};
    lf.lfHeight = -22;
    lf.lfWeight = FW_BOLD;
    wcscpy_s(lf.lfFaceName, L"Microsoft JhengHei UI");
    g_fontTitle = CreateFontIndirectW(&lf);

    lf.lfHeight = -14;
    lf.lfWeight = FW_NORMAL;
    wcscpy_s(lf.lfFaceName, L"Microsoft JhengHei UI");
    g_fontMono = CreateFontIndirectW(&lf);

    lf.lfHeight = -14;
    lf.lfWeight = FW_SEMIBOLD;
    wcscpy_s(lf.lfFaceName, L"Microsoft JhengHei UI");
    g_fontButton = CreateFontIndirectW(&lf);
}

static void SetControlFont(HWND hwnd, HFONT font) {
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

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
        AddLog(L"警告: 無法取得程式目錄，未自動保存卡密");
        return;
    }

    wchar_t tokenPath[MAX_PATH] = {};
    wchar_t lastPath[MAX_PATH] = {};
    swprintf_s(tokenPath, L"%s\\license_token.dat", dir);
    swprintf_s(lastPath, L"%s\\last_license.txt", dir);

    if (WriteUtf8TextFileW(tokenPath, tokenW) && WriteUtf8TextFileW(lastPath, tokenW)) {
        AddLog((L"已自動保存: " + std::wstring(tokenPath)).c_str());
    } else {
        AddLog(L"警告: 自動保存卡密失敗，請手動匯出");
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
    bi.ulFlags = BIF_RETURNONLYFSDIRS;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;

    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);

    AddLog(L"=== 產生金鑰對 ===");
    SetStatus(L"產生金鑰對中...");

    std::wstring privPath = std::wstring(path) + L"\\license_private.blob";
    std::wstring pubPath = std::wstring(path) + L"\\license_public.blob";

    char err[256] = {};
    if (!OfflineLicenseGenerateKeyPair(
            WideToUtf8(privPath.c_str()).c_str(),
            WideToUtf8(pubPath.c_str()).c_str(),
            2048, err, sizeof(err))) {
        AddLog((L"失敗: " + Utf8ToWide(err)).c_str());
        ShowMsg(hwnd, L"錯誤", Utf8ToWide(err).c_str(), true);
        SetStatus(L"失敗");
        return;
    }

    AddLog((L"成功! 公鑰: " + pubPath).c_str());
    AddLog((L"成功! 私鑰: " + privPath).c_str());
    ShowMsg(hwnd, L"成功", L"金鑰對已產生!\n\n請將 license_public.blob 放到 JyTrainer.exe 同目錄");
    SetStatus(L"完成");
}

static void OnIssue(HWND hwnd) {
    std::wstring hwidW = GetDlgTextW(hwnd, IDC_EDIT_HWID);
    std::wstring daysStrW = GetDlgTextW(hwnd, IDC_EDIT_DAYS);
    std::wstring permsW = GetDlgTextW(hwnd, IDC_EDIT_PERMS);

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
    ofn.Flags = OFN_FILEMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return;

    AddLog(L"=== 發卡 ===");
    SetStatus(L"發卡中...");

    char err[256] = {};
    char token[MAX4096] = {};
    if (!OfflineLicenseIssueToken(
            WideToUtf8(privPath).c_str(),
            WideToUtf8(hwidW.c_str()).c_str(),
            days,
            WideToUtf8(permsW.c_str()).c_str(),
            token, sizeof(token), err, sizeof(err))) {
        AddLog((L"失敗: " + Utf8ToWide(err)).c_str());
        ShowMsg(hwnd, L"錯誤", Utf8ToWide(err).c_str(), true);
        SetStatus(L"失敗");
        return;
    }

    // 顯示卡密
    std::wstring tokenW = Utf8ToWide(token);
    SetDlgItemTextW(hwnd, IDC_EDIT_KEYOUT, tokenW.c_str());
    AddLog((L"成功! 卡密: " + tokenW).c_str());
    SaveIssuedTokenToModuleDir(tokenW.c_str());
    SetStatus(L"發卡成功");
}

static void OnExport(HWND hwnd) {
    wchar_t tokenW[MAX4096] = {};
    GetDlgItemTextW(hwnd, IDC_EDIT_KEYOUT, tokenW, sizeof(tokenW) / sizeof(wchar_t));

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
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (!GetSaveFileNameW(&ofn)) return;

    FILE* f = _wfopen(path, L"w");
    if (!f) {
        ShowMsg(hwnd, L"錯誤", L"無法寫入檔案", true);
        return;
    }
    fputws(tokenW, f);
    fputc(L'\n', f);
    fclose(f);

    AddLog((L"已匯出到: " + std::wstring(path)).c_str());
    ShowMsg(hwnd, L"成功", L"已匯出卡密!");
}

static void OnCopy(HWND hwnd) {
    wchar_t tokenW[MAX4096] = {};
    GetDlgItemTextW(hwnd, IDC_EDIT_KEYOUT, tokenW, sizeof(tokenW) / sizeof(wchar_t));

    if (!tokenW[0]) {
        ShowMsg(hwnd, L"無卡密", L"先生成一張卡", true);
        return;
    }

    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (wcslen(tokenW) + 1) * sizeof(wchar_t));
        if (h) {
            wchar_t* p = (wchar_t*)GlobalLock(h);
            wcscpy_s(p, wcslen(tokenW) + 1, tokenW);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        }
        CloseClipboard();
        AddLog(L"已複製到剪貼簿!");
    }
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

    // 標題
    HWND hTitle = CreateWindowExW(0, L"Static", L"RAN2 Bot 授權管理工具",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, 10, W, 40, hwnd, (HMENU)IDC_STATIC_TITLE, NULL, NULL);
    SetControlFont(hTitle, g_fontTitle);

    // HWID 輸入
    int y = 70;
    CreateWindowExW(0, L"Static", L"玩家 HWID:",
        WS_CHILD | WS_VISIBLE,
        20, y, 80, 25, hwnd, NULL, NULL, NULL);
    HWND hEditHwid = CreateWindowExW(0, L"Edit", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        100, y, W - 120, 25, hwnd, (HMENU)IDC_EDIT_HWID, NULL, NULL);
    SetControlFont(hEditHwid, g_fontMono);

    // 天數輸入
    y += 35;
    CreateWindowExW(0, L"Static", L"天數:",
        WS_CHILD | WS_VISIBLE,
        20, y, 80, 25, hwnd, NULL, NULL, NULL);
    HWND hEditDays = CreateWindowExW(0, L"Edit", L"30",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER,
        100, y, 100, 25, hwnd, (HMENU)IDC_EDIT_DAYS, NULL, NULL);
    SetControlFont(hEditDays, g_fontMono);

    // 權限輸入
    CreateWindowExW(0, L"Static", L"權限:",
        WS_CHILD | WS_VISIBLE,
        220, y, 50, 25, hwnd, NULL, NULL, NULL);
    HWND hEditPerms = CreateWindowExW(0, L"Edit", L"basic",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        270, y, W - 290, 25, hwnd, (HMENU)IDC_EDIT_PERMS, NULL, NULL);
    SetControlFont(hEditPerms, g_fontMono);

    // 按鈕列
    y += 40;
    HWND hBtnGen = CreateWindowExW(0, L"BUTTON", L"1. 產生金鑰",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, y, 110, 35, hwnd, (HMENU)IDC_BTN_GENKEY, NULL, NULL);
    HWND hBtnIssue = CreateWindowExW(0, L"BUTTON", L"2. 發卡",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        140, y, 110, 35, hwnd, (HMENU)IDC_BTN_ISSUE, NULL, NULL);
    HWND hBtnExport = CreateWindowExW(0, L"BUTTON", L"匯出",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        260, y, 80, 35, hwnd, (HMENU)IDC_BTN_EXPORT, NULL, NULL);
    HWND hBtnCopy = CreateWindowExW(0, L"BUTTON", L"複製",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        350, y, 80, 35, hwnd, (HMENU)IDC_BTN_COPY, NULL, NULL);
    SetControlFont(hBtnGen, g_fontButton);
    SetControlFont(hBtnIssue, g_fontButton);
    SetControlFont(hBtnExport, g_fontButton);
    SetControlFont(hBtnCopy, g_fontButton);

    // 卡密輸出
    y += 45;
    CreateWindowExW(0, L"Static", L"卡密:",
        WS_CHILD | WS_VISIBLE,
        20, y, 80, 25, hwnd, NULL, NULL, NULL);
    HWND hEditKey = CreateWindowExW(0, L"Edit", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_MULTILINE | WS_VSCROLL,
        20, y + 25, W - 40, 80, hwnd, (HMENU)IDC_EDIT_KEYOUT, NULL, NULL);
    SetControlFont(hEditKey, g_fontMono);

    // 日誌標題
    y += 120;
    CreateWindowExW(0, L"Static", L"操作日誌:",
        WS_CHILD | WS_VISIBLE,
        20, y, 100, 25, hwnd, (HMENU)IDC_STATIC_LOG, NULL, NULL);

    // 日誌列表
    s_hwndLog = CreateWindowExW(0, L"ListBox", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        20, y + 25, W - 40, H - y - 60, hwnd, (HMENU)IDC_EDIT_LOG, NULL, NULL);
    SetControlFont(s_hwndLog, g_fontMono);

    // 初始化日誌
    AddLog(L"========================================");
    AddLog(L"      RAN2 Bot 授權管理工具");
    AddLog(L"========================================");
    AddLog(L"");
    AddLog(L"操作指引:");
    AddLog(L"1. 點擊「產生金鑰」建立金鑰對");
    AddLog(L"2. 選擇存放目錄後完成金鑰生成");
    AddLog(L"3. 將 license_public.blob 放到 JyTrainer 目錄");
    AddLog(L"4. 點擊「發卡」產生卡密");
    AddLog(L"5. 使用「複製」或「匯出」保存卡密");
    AddLog(L"");

    // 狀態列
    s_hwndStatus = CreateWindowExW(0, L"Static", L"就緒",
        WS_CHILD | WS_VISIBLE | SS_LEFT | WS_BORDER,
        0, H - 30, W, 30, hwnd, NULL, NULL, NULL);
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

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (code == BN_CLICKED) {
            switch (id) {
            case IDC_BTN_GENKEY: OnGenKey(hWnd); break;
            case IDC_BTN_ISSUE:  OnIssue(hWnd);  break;
            case IDC_BTN_EXPORT: OnExport(hWnd); break;
            case IDC_BTN_COPY:   OnCopy(hWnd);   break;
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
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
// 初始化
// ============================================================
bool InitLicenseAdminGui(HINSTANCE hInstance, int nCmdShow) {
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = AdminWndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(RGB(30, 30, 40));
    wcex.lpszClassName = L"LicenseAdminWindow";
    RegisterClassExW(&wcex);

    g_adminHwnd = CreateWindowExW(
        0, L"LicenseAdminWindow", L"RAN2 Bot 授權管理工具",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 480,
        NULL, NULL, hInstance, NULL);

    if (!g_adminHwnd) return false;

    ShowWindow(g_adminHwnd, nCmdShow);
    UpdateWindow(g_adminHwnd);
    return true;
}
