#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "offline_license.h"
#include "resource.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

static void PrintUsage() {
    printf("JyTrainer License Admin\n");
    printf("\n");
    printf("用法:\n");
    printf("  license_admin genkey [output_dir]\n");
    printf("  license_admin issue <private_key.blob> <hwid> [days] [permissions]\n");
    printf("\n");
    printf("範例:\n");
    printf("  license_admin genkey keys\n");
    printf("  license_admin issue keys\\license_private.blob 1a2b3c... 30 basic\n");
}

static std::string JoinPath(const char* dir, const char* file) {
    std::string out = dir ? dir : ".";
    if (!out.empty() && out[out.size() - 1] != '\\' && out[out.size() - 1] != '/') {
        out += "\\";
    }
    out += file;
    return out;
}

static void ApplyConsoleIcon() {
    HWND console = GetConsoleWindow();
    HMODULE self = GetModuleHandleA(NULL);
    if (!console || !self) return;

    HICON bigIcon = (HICON)LoadImageA(
        self,
        MAKEINTRESOURCEA(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR);
    HICON smallIcon = (HICON)LoadImageA(
        self,
        MAKEINTRESOURCEA(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR);

    if (bigIcon) SendMessageA(console, WM_SETICON, ICON_BIG, (LPARAM)bigIcon);
    if (smallIcon) SendMessageA(console, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
}

static void TrimRight(char* text) {
    if (!text) return;
    size_t len = strlen(text);
    while (len > 0) {
        char ch = text[len - 1];
        if (ch != '\r' && ch != '\n' && ch != ' ' && ch != '\t') break;
        text[--len] = '\0';
    }
}

static void TrimLeft(char* text) {
    if (!text || !text[0]) return;
    size_t start = 0;
    while (text[start] == ' ' || text[start] == '\t') ++start;
    if (start > 0) {
        memmove(text, text + start, strlen(text + start) + 1);
    }
}

static void Trim(char* text) {
    TrimRight(text);
    TrimLeft(text);
}

static bool ReadLine(const char* prompt, char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    if (prompt && prompt[0]) {
        printf("%s", prompt);
        fflush(stdout);
    }
    if (!fgets(out, (int)outSize, stdin)) {
        out[0] = '\0';
        return false;
    }
    Trim(out);
    return true;
}

static void WaitForEnter(const char* prompt) {
    char dummy[8] = {0};
    ReadLine(prompt ? prompt : "按 Enter 繼續...", dummy, sizeof(dummy));
}

static int ParseDaysOrDefault(const char* text, int defaultValue) {
    if (!text || !text[0]) return defaultValue;
    char* endPtr = NULL;
    long value = strtol(text, &endPtr, 10);
    if (endPtr == text || value <= 0) return defaultValue;
    if (value > 36500) value = 36500;
    return (int)value;
}

static int DoGenerateKeys(const char* outDir) {
    const char* targetDir = (outDir && outDir[0]) ? outDir : ".";
    CreateDirectoryA(targetDir, NULL);
    std::string privPath = JoinPath(targetDir, "license_private.blob");
    std::string pubPath = JoinPath(targetDir, "license_public.blob");
    char err[256] = {0};
    if (!OfflineLicenseGenerateKeyPair(privPath.c_str(), pubPath.c_str(), 1024, err, sizeof(err))) {
        printf("產生金鑰失敗: %s\n", err);
        return 2;
    }
    printf("已產生金鑰:\n");
    printf("  私鑰: %s\n", privPath.c_str());
    printf("  公鑰: %s\n", pubPath.c_str());
    printf("\n");
    printf("請只把公鑰放到輔助工具同目錄，私鑰請留在發卡機。\n");
    return 0;
}

static int DoIssueLicense(const char* privateKey, const char* hwid, int days, const char* permissions) {
    char token[2048] = {0};
    char err[256] = {0};
    if (!OfflineLicenseIssueToken(privateKey, hwid, days, permissions, token, sizeof(token), err, sizeof(err))) {
        printf("發卡失敗: %s\n", err);
        return 3;
    }

    printf("授權已生成:\n");
    printf("%s\n", token);

    FILE* outFile = NULL;
    if (fopen_s(&outFile, "last_license.txt", "wb") == 0 && outFile) {
        fwrite(token, 1, strlen(token), outFile);
        fwrite("\r\n", 1, 2, outFile);
        fclose(outFile);
        printf("\n已另存到: last_license.txt\n");
    }
    return 0;
}

static int RunInteractive() {
    for (;;) {
        printf("\n");
        printf("========================================\n");
        printf(" JyTrainer 離線發卡工具\n");
        printf("========================================\n");
        printf("1. 產生金鑰\n");
        printf("2. 發一張授權卡\n");
        printf("3. 離開\n");
        printf("\n");

        char choice[32] = {0};
        if (!ReadLine("請輸入選項 (1-3): ", choice, sizeof(choice))) {
            return 1;
        }

        if (strcmp(choice, "1") == 0) {
            char outDir[MAX_PATH] = {0};
            ReadLine("輸出目錄 (直接 Enter = 當前資料夾): ", outDir, sizeof(outDir));
            int rc = DoGenerateKeys(outDir[0] ? outDir : ".");
            printf("\n");
            if (rc == 0) {
                printf("接下來把 license_public.blob 放到 JyTrainer.exe 同目錄即可。\n");
            }
            WaitForEnter("按 Enter 回到主選單...");
            continue;
        }

        if (strcmp(choice, "2") == 0) {
            char privateKey[MAX_PATH] = {0};
            char hwid[256] = {0};
            char daysText[32] = {0};
            char permissions[256] = {0};

            ReadLine("私鑰路徑 (直接 Enter = license_private.blob): ", privateKey, sizeof(privateKey));
            ReadLine("玩家 HWID: ", hwid, sizeof(hwid));
            if (!hwid[0]) {
                printf("HWID 不能空白。\n");
                WaitForEnter("按 Enter 回到主選單...");
                continue;
            }
            ReadLine("天數 (直接 Enter = 30): ", daysText, sizeof(daysText));
            ReadLine("權限 (直接 Enter = basic): ", permissions, sizeof(permissions));

            int rc = DoIssueLicense(
                privateKey[0] ? privateKey : "license_private.blob",
                hwid,
                ParseDaysOrDefault(daysText, 30),
                permissions[0] ? permissions : "basic");
            WaitForEnter("按 Enter 回到主選單...");
            if (rc != 0) continue;
            continue;
        }

        if (strcmp(choice, "3") == 0 || _stricmp(choice, "q") == 0 || _stricmp(choice, "quit") == 0) {
            return 0;
        }

        printf("無效選項，請重新輸入。\n");
        WaitForEnter("按 Enter 回到主選單...");
    }
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    ApplyConsoleIcon();

    if (argc < 2) {
        return RunInteractive();
    }

    if (_stricmp(argv[1], "genkey") == 0) {
        return DoGenerateKeys((argc >= 3) ? argv[2] : "keys");
    }

    if (_stricmp(argv[1], "issue") == 0) {
        if (argc < 4) {
            PrintUsage();
            return 1;
        }
        return DoIssueLicense(
            argv[2],
            argv[3],
            (argc >= 5) ? ParseDaysOrDefault(argv[4], 30) : 30,
            (argc >= 6) ? argv[5] : "basic");
    }

    PrintUsage();
    return 1;
}
