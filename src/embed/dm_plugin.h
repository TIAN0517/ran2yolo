#pragma once
#ifndef _DM_PLUGIN_H_
#define _DM_PLUGIN_H_

#include <windows.h>

#ifdef DM_PLUGIN_EXPORTS
#define DM_API __declspec(dllexport)
#else
#define DM_API
#endif

// ============================================================
// 大漠插件兼容介面
// 實現背景/前台圖色識別、後台鍵鼠操作
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// 核心綁定函數
// ============================================================

// 設置窗口句柄並獲取窗口信息
DM_API HWND __stdcall DM_FindWindow(const char* lpClassName, const char* lpWindowName);

// 附著到窗口（獲取進程權限）
DM_API BOOL __stdcall DM_SetWindowState(HWND hwnd, DWORD flag);

// 枚舉窗口
DM_API HWND __stdcall DM_EnumWindow(HWND parent, const char* proc_name, const char* title, DWORD filter);

// ============================================================
// 圖色操作
// ============================================================

// 後台截圖（使用 PrintWindow，支持背景窗口）
DM_API BOOL __stdcall DM_CaptureWindow(HWND hwnd, const char* file_name);

// 内存版截圖（RGB數組）
DM_API BOOL __stdcall DM_CaptureWindowEx(HWND hwnd, int x, int y, int w, int h, BYTE* out_rgb, int* out_size);

// 按顔色找怪（返回第一個匹配點）
DM_API BOOL __stdcall DM_FindColor(HWND hwnd, int x1, int y1, int x2, int y2,
    COLORREF color, float sim, int dir, int* out_x, int* out_y);

// 多顔色查找
DM_API int __stdcall DM_FindColorEx(HWND hwnd, int x1, int y1, int x2, int y2,
    const char* color_format, float sim, int dir, int** out_points, int max_count);

// 找圖（在區域內搜索 BMP 圖）
DM_API BOOL __stdcall DM_FindPic(HWND hwnd, int x1, int y1, int x2, int y2,
    const char* pic_name, const char* delta_color, float sim,
    int dir, int* out_x, int* out_y, const char** out_pic_name);

// 找圖擴展（返回所有匹配位置）
DM_API int __stdcall DM_FindPicEx(HWND hwnd, int x1, int y1, int x2, int y2,
    const char* pic_name, const char* delta_color, float sim,
    int dir, int** out_results, int max_count);

// ============================================================
//  OCR 文字識別
// ============================================================

DM_API const char* __stdcall DM_OCRExt(HWND hwnd, int x1, int y1, int x2, int y2,
    const char* color, float sim);

// ============================================================
// 後台鍵鼠操作
// ============================================================

// 後台鍵盤按下/釋放
DM_API BOOL __stdcall DM_KeyDown(HWND hwnd, DWORD key);
DM_API BOOL __stdcall DM_KeyUp(HWND hwnd, DWORD key);
DM_API BOOL __stdcall DM_KeyPress(HWND hwnd, DWORD key);

// 後台鼠標操作
DM_API BOOL __stdcall DM_MoveTo(HWND hwnd, int x, int y);
DM_API BOOL __stdcall DM_LeftClick(HWND hwnd);
DM_API BOOL __stdcall DM_LeftDown(HWND hwnd);
DM_API BOOL __stdcall DM_LeftUp(HWND hwnd);
DM_API BOOL __stdcall DM_RightClick(HWND hwnd);
DM_API BOOL __stdcall DM_RightDown(HWND hwnd);
DM_API BOOL __stdcall DM_RightUp(HWND hwnd);

// 後台滾輪
DM_API BOOL __stdcall DM_WheelDown(HWND hwnd);
DM_API BOOL __stdcall DM_WheelUp(HWND hwnd);

// ============================================================
// 窗口信息
// ============================================================

// 獲取窗口客戶區大小
DM_API BOOL __stdcall DM_GetClientSize(HWND hwnd, int* out_w, int* out_h);

// 獲取窗口在屏幕上的位置
DM_API BOOL __stdcall DM_GetWindowRect(HWND hwnd, int* out_x, int* out_y, int* out_w, int* out_h);

// 獲取窗口標題
DM_API const char* __stdcall DM_GetWindowTitle(HWND hwnd);

// 獲取窗口類名
DM_API const char* __stdcall DM_GetWindowClass(HWND hwnd);

// 窗口是否存在
DM_API BOOL __stdcall DM_WindowExists(HWND hwnd);

// 激活窗口
DM_API BOOL __stdcall DM_SetForegroundWindow(HWND hwnd);

// ============================================================
// 內部函數（C++ 調用）
// ============================================================

// 內存圖像緩衝區（供外部讀取截圖結果）
struct DMImageBuffer {
    int width;
    int height;
    BYTE* rgb;
    int rgb_size;
    DWORD capture_time;
};

// 初始化 DM 插件
DM_API BOOL __stdcall DM_Init();

// 釋放資源
DM_API void DM_Destroy();

// 檢查是否已初始化
DM_API BOOL __stdcall DM_IsInited();

// 最後錯誤信息
DM_API const char* __stdcall DM_GetLastError();

// 設置調試模式
DM_API void DM_SetDebug(BOOL enable);

// 獲取最後一次截圖的圖像緩衝區
DM_API const DMImageBuffer* DM_GetLastCapture();

// 顔色比較（sim 相似度 0.0-1.0）
DM_API BOOL __stdcall DM_CompareColor(COLORREF c1, COLORREF c2, float sim);

// 在內存緩衝區中查找顔色
DM_API BOOL __stdcall DM_FindColorInBuffer(const DMImageBuffer* buf, int x1, int y1, int x2, int y2,
    COLORREF color, float sim, int dir, int* out_x, int* out_y);

// 在內存緩衝區中多顔色查找
DM_API int __stdcall DM_FindColorExInBuffer(const DMImageBuffer* buf, int x1, int y1, int x2, int y2,
    const char* color_format, float sim, int dir, int** out_points, int max_count);

// 獲取像素顔色
DM_API COLORREF __stdcall DM_GetPixelColor(HWND hwnd, int x, int y);

// 獲取內存緩衝區像素顔色
DM_API COLORREF __stdcall DM_GetPixelColorFromBuffer(const DMImageBuffer* buf, int x, int y);

#ifdef __cplusplus
}
#endif

#endif // _DM_PLUGIN_H_
