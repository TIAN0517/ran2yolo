// ============================================================
// dm_wrapper.h
// DM Plugin C++ Wrapper
// ============================================================

#pragma once
#ifndef DM_WRAPPER_H
#define DM_WRAPPER_H

#include <windows.h>

namespace DMWrapper {
    struct PicResult {
        bool found = false;
        int x = -1;
        int y = -1;
        const char* name = "";
    };

    class DMWrapperClass {
    public:
        bool Init();
        void Destroy();
        bool IsInited();
        HWND GetBindWindow();
        bool BindWindow(HWND hwnd);
        bool SetPath(const char* path);
        bool FindPic(int x1, int y1, int x2, int y2, const char* pic_name, float sim, int dir, int* out_x, int* out_y);
        PicResult FindPicResult(int x1, int y1, int x2, int y2, const char* pic_name, float sim);
        bool FindColor(int x1, int y1, int x2, int y2, COLORREF color, float sim, int dir, int* out_x, int* out_y);
        bool MoveTo(int x, int y);
        bool LeftClick();
        bool RightClick();
        bool KeyDown(DWORD key);
        bool KeyUp(DWORD key);
        bool KeyPress(DWORD key);
        bool GetClientSize(int* out_w, int* out_h);

    private:
        bool m_inited = false;
        HWND m_hwnd = NULL;
    };
}

// Global instance
namespace DMWrapper { extern DMWrapperClass g_dm; }

#endif // DM_WRAPPER_H
