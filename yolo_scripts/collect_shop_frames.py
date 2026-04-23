# ============================================================
# 商店截圖收集腳本
# 每秒截圖一次，自動保存到 training_data
# 按 ESC 鍵停止
# ============================================================
import time
import os
import sys
import cv2
import numpy as np
from pathlib import Path
import win32gui
import win32ui
import win32con
from ctypes import windll

# 設定
PROJECT_DIR = Path(__file__).parent
OUTPUT_DIR = PROJECT_DIR / "training_data" / "images"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# 截圖間隔（秒）
INTERVAL = 1.0

# 遊戲視窗標題
GAME_TITLES = [
    "亂2 online", "亂2online", "亂 2 online",
    "Lineage II", "RAN Online", "RAN2 Online",
    "Game", None  # None = 嘗試找第一個大窗口
]

def find_game_window():
    """找到遊戲視窗"""
    print("\n" + "=" * 50)
    print("  尋找遊戲視窗...")
    print("=" * 50)

    found = None

    for title in GAME_TITLES:
        if title is None:
            # 嘗試枚舉所有窗口
            def callback(hwnd, results):
                if win32gui.IsWindowVisible(hwnd):
                    title = win32gui.GetWindowText(hwnd)
                    if title and len(title) > 3:
                        rect = win32gui.GetWindowRect(hwnd)
                        w = rect[2] - rect[0]
                        h = rect[3] - rect[1]
                        if w > 500 and h > 400:  # 過濾小窗口
                            results.append((hwnd, title, w, h))
            windows = []
            win32gui.EnumWindows(callback, windows)
            if windows:
                # 選擇最大的窗口
                windows.sort(key=lambda x: x[2] * x[3], reverse=True)
                found = windows[0]
                print(f"  找到最大窗口: {found[1]} ({found[2]}x{found[3]})")
                break
        else:
            hwnd = win32gui.FindWindow(None, title)
            if hwnd:
                rect = win32gui.GetWindowRect(hwnd)
                w = rect[2] - rect[0]
                h = rect[3] - rect[1]
                found = (hwnd, title, w, h)
                print(f"  找到視窗: {title} ({w}x{h})")
                break

    if found:
        print(f"  ✅ 使用視窗: {found[1]}")
        return found[0]
    else:
        print("  ❌ 找不到遊戲視窗！")
        return None

def capture_window(hwnd):
    """截取指定窗口的內容"""
    try:
        left, top, right, bottom = win32gui.GetClientRect(hwnd)
        # 轉換為螢幕座標
        left, top = win32gui.ClientToScreen(hwnd, (left, top))
        right, bottom = win32gui.ClientToScreen(hwnd, (right, bottom))

        width = right - left
        height = bottom - top

        if width <= 0 or height <= 0:
            return None

        # 創建設備上下文
        hwndDC = win32gui.GetWindowDC(hwnd)
        mfcDC = win32ui.CreateDCFromHandle(hwndDC)
        saveDC = mfcDC.CreateCompatibleDC()

        # 創建 bitmap
        saveBitMap = win32ui.CreateBitmap()
        saveBitMap.CreateCompatibleBitmap(mfcDC, width, height)
        saveDC.SelectObject(saveBitMap)

        # 截圖
        result = windll.user32.PrintWindow(hwnd, saveDC.GetSafeHdc(), 2)

        if result == 0:
            # fallback: 直接拷貝窗口
            saveDC.BitBlt((0, 0), (width, height), mfcDC, (0, 0), win32con.SRCCOPY)

        # 轉換為 numpy array
        bmpinfo = saveBitMap.GetInfo()
        bmpstr = saveBitMap.GetBitmapBits(True)
        img = np.frombuffer(bmpstr, dtype=np.uint8)
        img.shape = (height, width, 4)

        # 釋放資源
        win32gui.DeleteObject(saveBitMap.GetHandle())
        saveDC.DeleteDC()
        mfcDC.DeleteDC()
        win32gui.ReleaseDC(hwnd, hwndDC)

        # 轉換 BGR
        img = cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)

        return img

    except Exception as e:
        print(f"  截圖失敗: {e}")
        return None

def main():
    print("\n" + "=" * 60)
    print("  RAN2 商店截圖收集腳本")
    print("=" * 60)
    print()
    print("  用法：")
    print("  1. 進入遊戲並打開商店介面")
    print("  2. 移動角色讓 NPC 箭頭在畫面中")
    print("  3. 這個腳本會每秒截圖一次")
    print("  4. 按 ESC 鍵停止")
    print()
    print(f"  輸出目錄: {OUTPUT_DIR}")
    print(f"  截圖間隔: {INTERVAL} 秒")
    print("=" * 60)

    # 找遊戲視窗
    hwnd = find_game_window()
    if not hwnd:
        print("\n按任意鍵結束...")
        input()
        return

    print("\n" + "=" * 50)
    print("  開始截圖！")
    print("  進入商店、打開背包、對著 NPC...")
    print("  按 ESC 鍵停止")
    print("=" * 50)

    # 找現有最大編號
    existing = list(OUTPUT_DIR.glob("shop_*.jpg"))
    if existing:
        nums = [int(f.stem.split('_')[1]) for f in existing]
        start_num = max(nums) + 1
    else:
        start_num = 1

    print(f"\n  從 #{start_num} 開始編號")
    print()

    count = 0
    last_save = 0

    try:
        while True:
            # 檢查 ESC 鍵
            if windll.user32.GetAsyncKeyState(0x1B) & 0x8000:  # VK_ESCAPE
                print("\n\n  🛑 停止截圖")
                break

            # 截圖
            img = capture_window(hwnd)
            if img is not None:
                filename = f"shop_{start_num:06d}.jpg"
                filepath = OUTPUT_DIR / filename

                cv2.imwrite(str(filepath), img, [cv2.IMWRITE_JPEG_QUALITY, 90])

                # 創建空白標籤檔
                label_path = PROJECT_DIR / "training_data" / "labels" / f"shop_{start_num:06d}.txt"
                label_path.touch()

                count += 1
                start_num += 1

                # 每10張顯示進度
                if count % 10 == 0:
                    elapsed = time.time() - last_save if last_save else INTERVAL
                    print(f"  📸 已截圖 {count} 張 | 最近: {filename} | 速度: {1/elapsed:.1f}/秒")

                last_save = time.time()

            time.sleep(INTERVAL)

    except KeyboardInterrupt:
        print("\n\n  🛑 Ctrl+C 停止")

    print()
    print("=" * 50)
    print(f"  完成！共截圖 {count} 張")
    print(f"  保存位置: {OUTPUT_DIR}")
    print("=" * 50)
    print()
    print("  下一步：")
    print("  1. 用 simple_labeler.py 標註收集的圖片")
    print("  2. 記得標註正確的類別 (玩家=1, NPC=2, 商店=3)")
    print()

if __name__ == "__main__":
    main()
