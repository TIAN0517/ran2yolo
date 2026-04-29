# ============================================================
# screenshot_bypass.py
# 亂2 Online 反作弊繞道截圖實現
# 方案1: PrintWindow + PW_RENDERFULLCONTENT
# 方案2: OBS 虛擬攝影機 + OpenCV
# ============================================================

import ctypes
import ctypes.wintypes as wintypes
from ctypes import windll, byref, c_void_p, POINTER, Structure
import time
import numpy as np
import cv2

# Windows API 常量
PW_RENDERFULLCONTENT = 3
PW_CLIENTONLY = 2
SRCCOPY = 0x00CC0020

# 載入必要的 DLL
user32 = windll.user32
gdi32 = windll.gdi32
dwmapi = windll.dwmapi

# 結構體定義
class RECT(Structure):
    _fields_ = [
        ("left", wintypes.LONG),
        ("top", wintypes.LONG),
        ("right", wintypes.LONG),
        ("bottom", wintypes.LONG),
    ]

class BITMAPINFOHEADER(Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]

class BITMAPINFO(Structure):
    _fields_ = [
        ("bmiHeader", BITMAPINFOHEADER),
        ("bmiColors", wintypes.DWORD * 3),
    ]


# ============================================================
# 方案1: PrintWindow + PW_RENDERFULLCONTENT
# ============================================================
class PrintWindowCapture:
    """使用 PrintWindow API 截圖，支持 Win8.1+ 的 PW_RENDERFULLCONTENT"""

    @staticmethod
    def capture(hwnd, width=0, height=0):
        """
        截圖指定視窗

        Args:
            hwnd: 視窗句柄
            width: 指定寬度 (0=自動)
            height: 指定高度 (0=自動)

        Returns:
            numpy array (BGR格式) 或 None
        """
        if not hwnd:
            return None

        # 獲取客戶區大小
        rc = RECT()
        if not user32.GetClientRect(hwnd, byref(rc)):
            return None

        w = width if width > 0 else (rc.right - rc.left)
        h = height if height > 0 else (rc.bottom - rc.top)

        if w <= 0 or h <= 0:
            return None

        # 創建內存 DC
        hdc_window = user32.GetDC(hwnd)
        hdc_mem = gdi32.CreateCompatibleDC(hdc_window)
        hBitmap = gdi32.CreateCompatibleBitmap(hdc_window, w, h)
        hOldBitmap = gdi32.SelectObject(hdc_mem, hBitmap)

        try:
            # 嘗試使用 PW_RENDERFULLCONTENT (值=3) 捕獲硬體加速內容
            # 這是 Win8.1+ 的擴展標誌，可以穿透 DWM 加速
            result = user32.PrintWindow(hwnd, hdc_mem, PW_RENDERFULLCONTENT)

            if not result:
                # Fallback: 標準 PrintWindow
                result = user32.PrintWindow(hwnd, hdc_mem, PW_CLIENTONLY)

            if not result:
                return None

            # 轉換 HBITMAP 到 numpy array
            bmi = BITMAPINFO()
            bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
            bmi.bmiHeader.biWidth = w
            bmi.bmiHeader.biHeight = -h  # 頂向下
            bmi.bmiHeader.biPlanes = 1
            bmi.bmiHeader.biBitCount = 32
            bmi.bmiHeader.biCompression = 0  # BI_RGB

            # 分配內存
            buffer = np.zeros(h * w * 4, dtype=np.uint8)

            # 獲取像素數據
            scan_result = gdi32.GetDIBits(
                hdc_window, hBitmap, 0, h,
                buffer.ctypes.data_as(ctypes.POINTER(ctypes.c_ubyte)),
                byref(bmi), 0  # DIB_RGB_COLORS
            )

            if not scan_result:
                return None

            # 轉換為 OpenCV 格式 (BGRA -> BGR)
            img = buffer.reshape((h, w, 4))
            img = cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)

            return img

        finally:
            # 清理資源
            gdi32.SelectObject(hdc_mem, hOldBitmap)
            gdi32.DeleteObject(hBitmap)
            gdi32.DeleteDC(hdc_mem)
            user32.ReleaseDC(hwnd, hdc_window)

    @staticmethod
    def is_supported():
        """檢查是否支持 PW_RENDERFULLCONTENT"""
        return True  # Win8.1+ 支援


# ============================================================
# 方案2: OBS 虛擬攝影機 + OpenCV VideoCapture
# ============================================================
class OBSVirtualCameraCapture:
    """使用 OBS 虛擬攝影機截圖"""

    def __init__(self, device_index=None):
        """
        初始化 OBS 虛擬攝影機捕獲

        Args:
            device_index: 設備索引 (None=自動選擇)
        """
        self.cap = None
        self.initialized = False

        if device_index is not None:
            self.cap = cv2.VideoCapture(device_index)
            if self.cap.isOpened():
                self._configure()
        else:
            # 自動選擇設備
            self._auto_detect()

    def _auto_detect(self):
        """自動檢測 OBS 虛擬攝影機"""
        # OBS Virtual Camera 通常是 index 1 或更高
        # 嘗試不同的視頻設備
        for i in range(10):
            cap = cv2.VideoCapture(i)
            if cap.isOpened():
                # 測試讀取一幀
                ret, frame = cap.read()
                if ret and frame is not None and frame.size > 0:
                    # 檢查解析度是否符合遊戲 (1024x768 或附近)
                    h, w = frame.shape[:2]
                    if 600 <= w <= 1920 and 400 <= h <= 1080:
                        self.cap = cap
                        self._configure()
                        return

                    # 嘗試設置為遊戲解析度
                    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1024)
                    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 768)
                    ret, frame = cap.read()
                    if ret and frame is not None:
                        self.cap = cap
                        self._configure()
                        return

                cap.release()

        self.initialized = False

    def _configure(self):
        """配置視頻捕獲參數"""
        if self.cap and self.cap.isOpened():
            # 設置解析度
            self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1024)
            self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 768)
            self.cap.set(cv2.CAP_PROP_FPS, 30)
            self.initialized = True

    def capture(self):
        """捕獲一幀"""
        if not self.initialized or not self.cap:
            return None

        ret, frame = self.cap.read()
        if ret:
            return frame
        return None

    def is_available(self):
        """檢查是否可用"""
        return self.initialized and self.cap is not None

    def release(self):
        """釋放資源"""
        if self.cap:
            self.cap.release()
            self.cap = None
        self.initialized = False

    @staticmethod
    def list_devices():
        """列舉可用的視頻設備"""
        devices = []
        for i in range(10):
            cap = cv2.VideoCapture(i)
            if cap.isOpened():
                w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
                h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
                devices.append(f"Camera {i}: {w}x{h}")
                cap.release()
        return devices

    def __del__(self):
        self.release()


# ============================================================
# 方案3: DirectX GDI 截圖
# ============================================================
class DirectXCapture:
    """使用 GDI 直接截圖"""

    @staticmethod
    def capture(hwnd):
        """截圖指定視窗"""
        if not hwnd:
            return None

        # 獲取客戶區
        rc = RECT()
        if not user32.GetClientRect(hwnd, byref(rc)):
            return None

        w = rc.right - rc.left
        h = rc.bottom - rc.top

        if w <= 0 or h <= 0:
            return None

        # 使用桌面 DC 截圖
        hdc_screen = user32.GetDC(None)
        hdc_mem = gdi32.CreateCompatibleDC(hdc_screen)
        hBitmap = gdi32.CreateCompatibleBitmap(hdc_screen, w, h)
        hOldBitmap = gdi32.SelectObject(hdc_mem, hBitmap)

        try:
            # 打印客戶區
            user32.PrintWindow(hwnd, hdc_mem, PW_CLIENTONLY)

            # 轉換為 numpy array
            bmi = BITMAPINFO()
            bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
            bmi.bmiHeader.biWidth = w
            bmi.bmiHeader.biHeight = -h
            bmi.bmiHeader.biPlanes = 1
            bmi.bmiHeader.biBitCount = 32
            bmi.bmiHeader.biCompression = 0

            buffer = np.zeros(h * w * 4, dtype=np.uint8)
            gdi32.GetDIBits(
                hdc_screen, hBitmap, 0, h,
                buffer.ctypes.data_as(ctypes.POINTER(ctypes.c_ubyte)),
                byref(bmi), 0
            )

            img = buffer.reshape((h, w, 4))
            img = cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)

            return img

        finally:
            gdi32.SelectObject(hdc_mem, hOldBitmap)
            gdi32.DeleteObject(hBitmap)
            gdi32.DeleteDC(hdc_mem)
            user32.ReleaseDC(None, hdc_screen)


# ============================================================
# 統一截圖介面
# ============================================================
class UnifiedCapture:
    """統一截圖介面，自動選擇最佳方案"""

    METHOD_AUTO = "auto"
    METHOD_PRINTWINDOW = "printwindow"
    METHOD_OBS = "obs"
    METHOD_DIRECTX = "directx"

    def __init__(self, method=METHOD_AUTO):
        """
        初始化統一截圖器

        Args:
            method: 截圖方法 ("auto", "printwindow", "obs", "directx")
        """
        self.method = method
        self.initialized = False
        self.obs_capture = None

        self._init()

    def _init(self):
        """初始化"""
        if self.method == self.METHOD_AUTO:
            # 自動選擇：優先 PrintWindow
            self.method = self.METHOD_PRINTWINDOW
            self.initialized = True

            # 測試 OBS 是否可用
            self.obs_capture = OBSVirtualCameraCapture()
            if not self.obs_capture.is_available():
                self.obs_capture = None

        elif self.method == self.METHOD_OBS:
            self.obs_capture = OBSVirtualCameraCapture()
            self.initialized = self.obs_capture.is_available()

        else:
            self.initialized = True

    def capture(self, hwnd=None):
        """
        截圖

        Args:
            hwnd: 視窗句柄 (PrintWindow/DirectX 需要)

        Returns:
            numpy array (BGR格式) 或 None
        """
        if not self.initialized:
            return None

        if self.method == self.METHOD_PRINTWINDOW:
            img = PrintWindowCapture.capture(hwnd)
            if img is not None:
                return img

            # Fallback 到 OBS
            if self.obs_capture:
                return self.obs_capture.capture()

        elif self.method == self.METHOD_OBS:
            if self.obs_capture:
                return self.obs_capture.capture()

        elif self.method == self.METHOD_DIRECTX:
            return DirectXCapture.capture(hwnd)

        return None

    def is_available(self):
        """檢查是否可用"""
        return self.initialized

    def release(self):
        """釋放資源"""
        if self.obs_capture:
            self.obs_capture.release()


# ============================================================
# 便捷函數
# ============================================================
def find_game_window():
    """查找亂2 Online 遊戲視窗"""
    game_titles = ["亂2 Online", "RAN2 Online", "RanClient", "乱2 Online"]

    for title in game_titles:
        hwnd = user32.FindWindowW(None, title)
        if hwnd:
            return hwnd

    return None


def capture_game(method="auto"):
    """
    便捷函數：直接截圖遊戲

    Args:
        method: 截圖方法

    Returns:
        numpy array (BGR格式) 或 None
    """
    hwnd = find_game_window()
    if not hwnd:
        print("找不到遊戲視窗")
        return None

    capturer = UnifiedCapture(method)
    return capturer.capture(hwnd)


# ============================================================
# 測試代碼
# ============================================================
if __name__ == "__main__":
    print("=== 亂2 Online 反作弊繞道截圖測試 ===\n")

    # 查找遊戲視窗
    hwnd = find_game_window()
    if hwnd:
        print(f"找到遊戲視窗: {hwnd}")
    else:
        print("找不到遊戲視窗")
        exit(1)

    # 測試 PrintWindow
    print("\n=== 測試 PrintWindow ===")
    img = PrintWindowCapture.capture(hwnd)
    if img is not None:
        print(f"成功! 尺寸: {img.shape}")
        cv2.imwrite("test_printwindow.png", img)
        print("已保存: test_printwindow.png")
    else:
        print("失敗")

    # 測試 OBS
    print("\n=== 測試 OBS Virtual Camera ===")
    print("可用設備:", OBSVirtualCameraCapture.list_devices())
    obs = OBSVirtualCameraCapture()
    if obs.is_available():
        img = obs.capture()
        if img is not None:
            print(f"成功! 尺寸: {img.shape}")
            cv2.imwrite("test_obs.png", img)
            print("已保存: test_obs.png")
    else:
        print("OBS 不可用")

    # 測試統一介面
    print("\n=== 測試統一介面 (自動選擇) ===")
    capturer = UnifiedCapture()
    print(f"使用方案: {capturer.method}")
    img = capturer.capture(hwnd)
    if img is not None:
        print(f"成功! 尺寸: {img.shape}")
        cv2.imwrite("test_unified.png", img)
        print("已保存: test_unified.png")
    else:
        print("失敗")

    # 釋放資源
    capturer.release()
