# ============================================================
# YOLO 訓練數據收集工具 (Python 版本)
# 功能：
#   1. 自動偵測戰鬥（怪物血條），戰鬥時自動截圖 100 張
#   2. 手動熱鍵截圖（F9）
#   3. 自動生成 YOLO 格式標籤
# ============================================================
import cv2
import numpy as np
import win32gui
import win32ui
import win32con
import win32api
import time
import os
import sys
from collections import deque

# ============================================================
# 設定
# ============================================================
TRAIN_DATA_DIR = "training_data"
IMAGES_DIR = os.path.join(TRAIN_DATA_DIR, "images")
LABELS_DIR = os.path.join(TRAIN_DATA_DIR, "labels")
MODEL_NAME = "best.onnx"
AUTO_CAPTURE_COUNT = 100
COMBAT_MONSTER_THRESHOLD = 1
AUTO_CAPTURE_INTERVAL = 0.1  # 秒
COMBAT_CHECK_INTERVAL = 0.2   # 秒

# 血條識別參數
HP_MIN_WIDTH = 3
HP_MAX_WIDTH = 6
HP_MIN_HEIGHT = 8
HP_MAX_HEIGHT = 15
HP_R_THRESH = 200
HP_G_THRESH = 80
HP_B_THRESH = 80

# ============================================================
# 全域狀態
# ============================================================
frame_count = 0
auto_captured_count = 0
in_combat = False
running = True
yolo_model = None
last_combat_check = 0
last_capture_time = 0
f9_pressed = False
f10_pressed = False
f11_pressed = False

# ============================================================


# 確保目錄存在
def ensure_dirs():
    os.makedirs(IMAGES_DIR, exist_ok=True)
    os.makedirs(LABELS_DIR, exist_ok=True)


# 找遊戲視窗
def find_game_window():
    titles = [
        "亂2 Online", "亂2online", "乱2 Online", "乱2online",
        "Ran2 Online", "Ran2", "RAN2", "Ran-2 Online",
        "Game", "RAN Online"
    ]

    def callback(hwnd, windows):
        if win32gui.IsWindowVisible(hwnd):
            title = win32gui.GetWindowText(hwnd)
            for t in windows:
                if t in title:
                    return hwnd
        return None

    for title in titles:
        hwnd = win32gui.FindWindow(None, title)
        if hwnd and win32gui.IsWindowVisible(hwnd):
            print(f"[視窗] 找到: {title}")
            return hwnd

    return None


# 截圖函數
def capture_window(hwnd):
    if not hwnd:
        return None

    try:
        left, top, right, bottom = win32gui.GetClientRect(hwnd)
        w = right - left
        h = bottom - top

        if w <= 0 or h <= 0:
            return None

        hwnd_dc = win32gui.GetDC(hwnd)
        mfc_dc = win32ui.CreateDCFromHandle(hwnd_dc)
        save_dc = mfc_dc.CreateCompatibleDC()

        bitmap = win32ui.CreateBitmap()
        bitmap.CreateCompatibleBitmap(mfc_dc, w, h)
        save_dc.SelectObject(bitmap)

        result = save_dc.BitBlt((0, 0), (w, h), mfc_dc, (0, 0), win32con.SRCCOPY)

        bmpinfo = bitmap.GetInfo()
        bmpstr = bitmap.GetBitmapBits(True)

        img = np.frombuffer(bmpstr, dtype=np.uint8)
        img = img.reshape((h, w, 4))
        img = cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)  # OpenCV 用 BGR

        win32gui.DeleteObject(bitmap.GetHandle())
        save_dc.DeleteDC()
        mfc_dc.DeleteDC()
        win32gui.ReleaseDC(hwnd, hwnd_dc)

        return img
    except Exception as e:
        print(f"[錯誤] 截圖失敗: {e}")
        return None


# 檢查像素是否為 HP 條顏色
def is_hp_bar_color(pixel):
    r, g, b = pixel[2], pixel[1], pixel[0]
    return (r > HP_R_THRESH and g < HP_G_THRESH and b < HP_B_THRESH)


# 估算 HP 百分比
def estimate_hp_percent(pixels, w, h, bar_x, bar_y, bar_width):
    filled_width = 0
    for x in range(bar_x - bar_width // 2, bar_x + bar_width // 2):
        if x < 0 or x >= w:
            break
        if is_hp_bar_color(pixels[bar_y, x]):
            filled_width += 1
        else:
            break

    if bar_width == 0:
        return 100
    pct = (filled_width * 100) // bar_width
    return max(0, min(100, pct))


# 檢查血條下方是否有怪物身體
def has_monster_body(pixels, w, h, bar_x, bar_y, bar_width):
    check_top = bar_y + 2
    check_bottom = bar_y + 22
    check_left = bar_x - bar_width
    check_right = bar_x + bar_width

    body_pixels = 0
    for y in range(check_top, min(check_bottom + 1, h)):
        for x in range(max(check_left, 0), min(check_right + 1, w)):
            pixel = pixels[y, x]
            r, g, b = pixel[2], pixel[1], pixel[0]

            # 排除純黑/純白/HP條
            if r < 30 and g < 30 and b < 30:
                continue
            if r > 240 and g > 240 and b > 240:
                continue
            if is_hp_bar_color(pixel):
                continue

            body_pixels += 1
            if body_pixels >= 8:
                return True
    return False


# 偵測戰鬥（掃描怪物血條）
def detect_combat(img):
    if img is None:
        return False

    h, w = img.shape[:2]
    candidates = []

    scan_top = 50
    scan_bottom = h // 2 + 50

    # 滑動視窗掃描
    for y in range(scan_top, scan_bottom - HP_MAX_HEIGHT, 2):
        for x in range(10, w - 10, 2):
            if not is_hp_bar_color(img[y, x]):
                continue

            # 計算血條高度
            bar_height = 0
            for dy in range(0, HP_MAX_HEIGHT):
                if y + dy >= h:
                    break
                if is_hp_bar_color(img[y + dy, x]):
                    bar_height += 1
                else:
                    break

            if bar_height < HP_MIN_HEIGHT or bar_height > HP_MAX_HEIGHT:
                continue

            # 計算血條寬度
            bar_width = 1
            for dx in range(1, HP_MAX_WIDTH):
                if x + dx >= w:
                    break
                if is_hp_bar_color(img[y, x + dx]):
                    bar_width += 1
                else:
                    break

            if bar_width < HP_MIN_WIDTH or bar_width > HP_MAX_WIDTH:
                continue

            # 檢查血條下方是否有怪物身體
            if not has_monster_body(img, w, h, x, y + bar_height, bar_width):
                continue

            # 去重
            is_dup = False
            for cx, cy, _, _ in candidates:
                if abs(x - cx) < 10 and abs(y - cy) < 10:
                    is_dup = True
                    break

            if not is_dup:
                candidates.append((x, y, bar_width, bar_height))

    return len(candidates) >= COMBAT_MONSTER_THRESHOLD


# 獲取怪物候選位置（用於自動標註）
def get_monster_candidates(img):
    if img is None:
        return []

    h, w = img.shape[:2]
    candidates = []

    scan_top = 50
    scan_bottom = h // 2 + 50

    for y in range(scan_top, scan_bottom - HP_MAX_HEIGHT, 2):
        for x in range(10, w - 10, 2):
            if not is_hp_bar_color(img[y, x]):
                continue

            # 計算血條高度
            bar_height = 0
            for dy in range(0, HP_MAX_HEIGHT):
                if y + dy >= h:
                    break
                if is_hp_bar_color(img[y + dy, x]):
                    bar_height += 1
                else:
                    break

            if bar_height < HP_MIN_HEIGHT or bar_height > HP_MAX_HEIGHT:
                continue

            # 計算血條寬度
            bar_width = 1
            for dx in range(1, HP_MAX_WIDTH):
                if x + dx >= w:
                    break
                if is_hp_bar_color(img[y, x + dx]):
                    bar_width += 1
                else:
                    break

            if bar_width < HP_MIN_WIDTH or bar_width > HP_MAX_WIDTH:
                continue

            if not has_monster_body(img, w, h, x, y + bar_height, bar_width):
                continue

            # 去重
            is_dup = False
            for cx, cy, _, _ in candidates:
                if abs(x - cx) < 10 and abs(y - cy) < 10:
                    is_dup = True
                    break

            if not is_dup:
                candidates.append((x, y, bar_width, bar_height))

    return candidates


# 保存訓練數據（自動標註版）
def save_training_data(img, is_auto=False):
    global frame_count, auto_captured_count

    if img is None:
        return False

    frame_count += 1
    ensure_dirs()

    # 保存圖片
    img_path = os.path.join(IMAGES_DIR, f"frame_{frame_count:06d}.jpg")
    ok = cv2.imwrite(img_path, img, [cv2.IMWRITE_JPEG_QUALITY, 95])

    if ok:
        if is_auto:
            auto_captured_count += 1
            print(f"[AUTO] 截圖 #{auto_captured_count}/{AUTO_CAPTURE_COUNT}: {img_path}")
        else:
            print(f"[F9] 截圖 #{frame_count}: {img_path}")

        # ✅ 自動生成 YOLO 標註（用像素血條掃描）
        label_path = os.path.join(LABELS_DIR, f"frame_{frame_count:06d}.txt")
        candidates = get_monster_candidates(img)

        h, w = img.shape[:2]

        with open(label_path, 'w') as f:
            for x, y, bar_w, bar_h in candidates:
                # 根據血條位置估算怪物邊界框
                # 血條在怪物頭頂，血條下方是怪物身體
                # 假設怪物高度約為血條高度的 8-12 倍
                monster_h = bar_h * 10
                monster_w = bar_w * 8

                # 怪物中心在血條下方
                cx = (x + bar_w // 2) / w
                cy = (y + bar_h + monster_h // 2) / h
                nw = monster_w / w
                nh = monster_h / h

                # 限制範圍
                cx = max(0, min(1, cx))
                cy = max(0, min(1, cy))
                nw = max(0.01, min(1, nw))
                nh = max(0.01, min(1, nh))

                # 類別 0 = monster
                f.write(f"0 {cx:.6f} {cy:.6f} {nw:.6f} {nh:.6f}\n")

        print(f"[自動標註] #{frame_count}: {len(candidates)} 個怪物")
    else:
        print(f"[錯誤] 截圖 #{frame_count} 失敗!")
        return False

    return True


# YOLO 偵測
def detect_with_yolo(img, debug_path=None):
    if yolo_model is None:
        return []

    blob = cv2.dnn.blobFromImage(img, 1/255.0, (640, 640), (0,0,0), True, False)
    yolo_model.setInput(blob)
    outputs = yolo_model.forward(yolo_model.getUnconnectedOutLayersNames())

    results = []
    if outputs:
        data = outputs[0][0]
        for det in data:
            conf = det[4]
            if conf < 0.35:
                continue

            # 找到最大置信度類別
            max_conf = 0
            best_class = 0
            for c in range(4):  # 4 個類別
                if det[5 + c] > max_conf:
                    max_conf = det[5 + c]
                    best_class = c

            if max_conf < 0.35:
                continue

            x, y, w, h = det[0], det[1], det[2], det[3]
            x1 = x - w/2
            y1 = y - h/2

            # 轉換為 YOLO 格式（歸一化）
            img_h, img_w = img.shape[:2]
            cx = (x1 + w/2) / img_w
            cy = (y1 + h/2) / img_h
            nw = w / img_w
            nh = h / img_h

            results.append((best_class, cx, cy, nw, nh, max_conf))

            # 調試：繪製檢測框
            if debug_path:
                draw_x = int(x1)
                draw_y = int(y1)
                draw_w = int(w)
                draw_h = int(h)
                cv2.rectangle(img, (draw_x, draw_y), (draw_x + draw_w, draw_y + draw_h), (0, 255, 0), 2)
                cv2.putText(img, f"{best_class}:{max_conf:.2f}", (draw_x, draw_y - 5),
                           cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)

        if debug_path:
            cv2.imwrite(debug_path, img)

    return results


# 嘗試加載 YOLO 模型
def load_yolo_model():
    model_path = os.path.join(os.path.dirname(__file__), MODEL_NAME)
    if os.path.exists(model_path):
        try:
            net = cv2.dnn.readNetFromONNX(model_path)
            net.setPreferableBackend(cv2.dnn.DNN_BACKEND_OPENCV)
            net.setPreferableTarget(cv2.dnn.DNN_TARGET_CPU)
            print(f"[YOLO] 模型加載成功: {MODEL_NAME}")
            return net
        except Exception as e:
            print(f"[YOLO] 模型加載失敗: {e}")
    else:
        print(f"[YOLO] 未找到模型: {model_path}")
    return None


# 檢查按鍵狀態
def check_hotkeys():
    global f9_pressed, f10_pressed, f11_pressed, running, in_combat, auto_captured_count

    # F9 - 手動截圖
    if win32api.GetAsyncKeyState(0x78) & 0x8000:  # VK_F9 = 0x78
        if not f9_pressed:
            f9_pressed = True
            return 'f9'
    else:
        f9_pressed = False

    # F10 - 切換自動模式
    if win32api.GetAsyncKeyState(0x79) & 0x8000:  # VK_F10 = 0x79
        if not f10_pressed:
            f10_pressed = True
            return 'f10'
    else:
        f10_pressed = False

    # F11 - 重置計數
    if win32api.GetAsyncKeyState(0x7A) & 0x8000:  # VK_F11 = 0x7A
        if not f11_pressed:
            f11_pressed = True
            return 'f11'
    else:
        f11_pressed = False

    # ESC - 退出
    if win32api.GetAsyncKeyState(0x1B) & 0x8000:  # VK_ESCAPE
        running = False
        return 'esc'

    return None


# 主循環
def main():
    global running, in_combat, auto_captured_count, yolo_model, last_combat_check, last_capture_time

    print("=" * 50)
    print("  YOLO 訓練數據收集工具 v2.0")
    print("  自動戰鬥截圖版")
    print("=" * 50)
    print()

    # 初始化
    ensure_dirs()
    yolo_model = load_yolo_model()

    auto_mode = False
    game_hwnd = None

    print()
    print("========== 操作說明 ==========")
    print("F9   - 手動截圖")
    print("F10  - 切換手動自動連拍模式")
    print("F11  - 重置自動截圖計數")
    print("ESC  - 退出程式")
    print()
    print("【自動模式】")
    print("  - 工具會自動偵測遊戲畫面中的怪物血條")
    print("  - 偵測到戰鬥時自動截圖 100 張")
    print("  - 截圖完成後按 F11 重置")
    print()
    print("提示: 請先切換到遊戲視窗")
    print("=" * 50)
    print()

    last_combat_check = time.time()
    last_capture_time = time.time()

    while running:
        try:
            now = time.time()

            # 檢查熱鍵
            key = check_hotkeys()
            if key == 'f9':
                if not game_hwnd:
                    game_hwnd = find_game_window()
                if game_hwnd:
                    img = capture_window(game_hwnd)
                    if img is not None:
                        save_training_data(img, False)
                else:
                    print("[F9] 找不到遊戲視窗！")

            elif key == 'f10':
                auto_mode = not auto_mode
                print(f"[F10] 自動模式: {'開啟' if auto_mode else '關閉'}")

            elif key == 'f11':
                auto_captured_count = 0
                in_combat = False
                print("[F11] 重置自動截圖計數")

            elif key == 'esc':
                print("[ESC] 退出程式")
                break

            # 自動模式：持續偵測戰鬥並截圖
            if (auto_mode or (yolo_model is not None)) and auto_captured_count < AUTO_CAPTURE_COUNT:
                if not game_hwnd:
                    game_hwnd = find_game_window()

                if game_hwnd:
                    # 檢查戰鬥狀態
                    if now - last_combat_check >= COMBAT_CHECK_INTERVAL:
                        last_combat_check = now
                        img = capture_window(game_hwnd)
                        if img is not None:
                            combat = detect_combat(img)

                            if combat and not in_combat:
                                print("[戰鬥] 偵測到戰鬥，開始自動截圖...")
                                in_combat = True
                            elif not combat and in_combat:
                                print("[戰鬥] 戰鬥結束")
                                in_combat = False

                            # 自動截圖
                            if in_combat or auto_mode:
                                interval = 0.1 if in_combat else 0.5
                                if now - last_capture_time >= interval:
                                    last_capture_time = now
                                    save_training_data(img, True)

            # 完成提示
            if auto_captured_count >= AUTO_CAPTURE_COUNT:
                if int(now) % 5 == 0:  # 每5秒提示一次
                    print(f"[完成] 已自動截圖 {AUTO_CAPTURE_COUNT} 張！按 F11 重置。")
                time.sleep(1)

            time.sleep(0.01)

        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"[錯誤] {e}")
            time.sleep(0.1)

    print()
    print(f"[系統] 收集完成！")
    print(f"[系統] 共保存 {frame_count} 張截圖")
    print(f"[系統] 自動截圖 {auto_captured_count} 張")
    print(f"[提示] 請用 labelImg 標註: {IMAGES_DIR}")
    print()


if __name__ == "__main__":
    main()
