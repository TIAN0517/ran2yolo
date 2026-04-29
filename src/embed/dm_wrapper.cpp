// ============================================================
// dm_wrapper.cpp
// DM Plugin C++ Wrapper Implementation
// ============================================================

#include "dm_wrapper.h"
#include "dm_plugin.h"
#include <cstdio>

namespace DMWrapper {
    // Global instance
    DMWrapperClass g_dm_instance;

    // This is the one referenced by extern DMWrapper::DMWrapperClass g_dm
    DMWrapperClass g_dm;

    bool DMWrapperClass::Init() {
        if (m_inited) return true;
        m_inited = DM_Init() ? true : false;
        return m_inited;
    }

    void DMWrapperClass::Destroy() {
        DM_Destroy();
        m_inited = false;
        m_hwnd = NULL;
    }

    bool DMWrapperClass::IsInited() {
        return m_inited && DM_IsInited();
    }

    HWND DMWrapperClass::GetBindWindow() {
        return m_hwnd;
    }

    bool DMWrapperClass::BindWindow(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) return false;
        m_hwnd = hwnd;
        // Attach to window
        DM_SetWindowState(hwnd, 1);
        return true;
    }

    bool DMWrapperClass::SetPath(const char* path) {
        // Path is set in dm_visual.cpp via SetPath
        return true;
    }

    bool DMWrapperClass::FindPic(int x1, int y1, int x2, int y2,
                                  const char* pic_name, float sim, int dir,
                                  int* out_x, int* out_y) {
        if (!m_inited || !m_hwnd) return false;
        const char* out_name = nullptr;
        return DM_FindPic(m_hwnd, x1, y1, x2, y2, pic_name, nullptr, sim, dir, out_x, out_y, &out_name);
    }

    PicResult DMWrapperClass::FindPicResult(int x1, int y1, int x2, int y2,
                                             const char* pic_name, float sim) {
        PicResult result;
        if (!m_inited || !m_hwnd) return result;

        int out_x = -1, out_y = -1;
        const char* out_name = nullptr;
        result.found = DM_FindPic(m_hwnd, x1, y1, x2, y2, pic_name, nullptr, sim, 0, &out_x, &out_y, &out_name);
        if (result.found) {
            result.x = out_x;
            result.y = out_y;
            result.name = out_name ? out_name : pic_name;
        }
        return result;
    }

    bool DMWrapperClass::FindColor(int x1, int y1, int x2, int y2,
                                    COLORREF color, float sim, int dir,
                                    int* out_x, int* out_y) {
        if (!m_inited || !m_hwnd) return false;
        return DM_FindColor(m_hwnd, x1, y1, x2, y2, color, sim, dir, out_x, out_y);
    }

    bool DMWrapperClass::MoveTo(int x, int y) {
        if (!m_inited || !m_hwnd) return false;
        return DM_MoveTo(m_hwnd, x, y);
    }

    bool DMWrapperClass::LeftClick() {
        if (!m_inited || !m_hwnd) return false;
        return DM_LeftClick(m_hwnd);
    }

    bool DMWrapperClass::RightClick() {
        if (!m_inited || !m_hwnd) return false;
        return DM_RightClick(m_hwnd);
    }

    bool DMWrapperClass::KeyDown(DWORD key) {
        if (!m_inited || !m_hwnd) return false;
        return DM_KeyDown(m_hwnd, key);
    }

    bool DMWrapperClass::KeyUp(DWORD key) {
        if (!m_inited || !m_hwnd) return false;
        return DM_KeyUp(m_hwnd, key);
    }

    bool DMWrapperClass::KeyPress(DWORD key) {
        if (!m_inited || !m_hwnd) return false;
        return DM_KeyPress(m_hwnd, key);
    }

    bool DMWrapperClass::GetClientSize(int* out_w, int* out_h) {
        if (!m_inited || !m_hwnd) return false;
        return DM_GetClientSize(m_hwnd, out_w, out_h);
    }

} // namespace DMWrapper