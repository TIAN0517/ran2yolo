#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// ============================================================
// Remote config updater - HTTP download for offsets.dat
// Uses WinINet API (Win7 native, no third-party deps)
// ============================================================
namespace ConfigUpdater {

    // Configuration for the updater (loaded from updater.ini)
    struct UpdateConfig {
        char url[512];              // HTTP URL for offsets.dat
        char localPath[MAX_PATH];   // Local save path, default "offsets.dat"
        char versionUrl[512];       // Version check URL (version.txt)
        char localVersionFile[MAX_PATH]; // Local version file path
    };

    // Initialize updater, load config from updater.ini or defaults
    // configPath: path to updater.ini, NULL = auto-detect next to exe
    bool Init(const char* configPath = NULL);

    // Check if remote has a newer version
    // Returns true if a new version is available
    bool CheckForUpdate();

    // Download the latest offset file and save locally
    // Returns true on success
    bool DownloadUpdate();

    // One-click update: check -> download -> reload offsets
    // Returns: 0=already up-to-date, 1=updated successfully,
    //         -1=check failed, -2=download failed, -3=reload failed
    int UpdateAndReload();

    // Get last status message (for GUI display)
    const char* GetLastStatus();

    // Get remote version string
    const char* GetRemoteVersion();

    // Get local version string
    const char* GetLocalVersion();
}
