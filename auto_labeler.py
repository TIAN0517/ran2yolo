import cv2
import numpy as np
import mss
import time
import os
import win32gui

# 設定資料夾路徑
DATASET_DIR = "training_data/images"
LABEL_DIR = "training_data/labels"
os.makedirs(DATASET_DIR, exist_ok=True)
os.makedirs(LABEL_DIR, exist_ok=True)

# 怪物血條特徵參數 (與 visionentity.cpp 相同)
HP_R_THRESH = 200
HP_G_THRESH = 80
HP_B_THRESH = 80

def find_game_window():
    """尋找遊戲視窗"""
    titles = ["亂2 online", "Ran Online", "RAN2", "Game", "RAN Online"]
    for title in titles:
        hwnd = win32gui.FindWindow(None, title)
        if hwnd != 0:
            print(f"找到視窗: {title}")
            return hwnd
    return 0

def get_window_rect(hwnd):
    """取得視窗範圍"""
    rect = win32gui.GetClientRect(hwnd)
    point = win32gui.ClientToScreen(hwnd, (rect[0], rect[1]))
    return {
        "top": point[1],
        "left": point[0],
        "width": rect[2] - rect[0],
        "height": rect[3] - rect[1]
    }

def detect_monsters(img_bgr):
    """基於血條顏色偵測怪物，回傳 YOLO 格式的 bounding boxes"""
    boxes = []
    h, w, _ = img_bgr.shape

    b_channel = img_bgr[:, :, 0]
    g_channel = img_bgr[:, :, 1]
    r_channel = img_bgr[:, :, 2]

    # 建立血條遮罩 (R > 200, G < 80, B < 80)
    mask = (r_channel > HP_R_THRESH) & (g_channel < HP_G_THRESH) & (b_channel < HP_B_THRESH)
    mask = mask.astype(np.uint8) * 255

    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    for cnt in contours:
        x, y, bw, bh = cv2.boundingRect(cnt)
        if bw < 3 or bw > 50 or bh < 3 or bh > 20:
            continue

        # 估算怪物邊界框
        monster_width = bw * 4
        monster_height = bw * 8
        monster_x_center = x + (bw / 2)
        monster_y_center = y + bh + (monster_height / 2)

        # 轉為 YOLO 相對座標 (0.0 ~ 1.0)
        norm_x = monster_x_center / w
        norm_y = monster_y_center / h
        norm_w = monster_width / w
        norm_h = monster_height / h

        if 0 < norm_x < 1 and 0 < norm_y < 1 and norm_w > 0 and norm_h > 0:
            boxes.append((0, norm_x, norm_y, norm_w, norm_h))

    return boxes

def main():
    print("等待尋找遊戲視窗...")
    hwnd = find_game_window()
    if not hwnd:
        print("找不到遊戲視窗！請先開啟遊戲。")
        return

    print("找到視窗！開始自動截圖與標註... (按 Ctrl+C 停止)")

    with mss.mss() as sct:
        img_count = 0
        while True:
            try:
                rect = get_window_rect(hwnd)
                sct_img = sct.grab(rect)
                img = np.array(sct_img)
                img_bgr = cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)

                boxes = detect_monsters(img_bgr)

                if boxes:
                    timestamp = int(time.time() * 1000)
                    img_name = f"frame_{timestamp}.jpg"
                    txt_name = f"frame_{timestamp}.txt"

                    cv2.imwrite(os.path.join(DATASET_DIR, img_name), img_bgr)
                    with open(os.path.join(LABEL_DIR, txt_name), "w") as f:
                        for b in boxes:
                            f.write(f"{b[0]} {b[1]:.6f} {b[2]:.6f} {b[3]:.6f} {b[4]:.6f}\n")

                    img_count += 1
                    print(f"已儲存第 {img_count} 張樣本: {img_name}, 發現 {len(boxes)} 隻怪物")

                time.sleep(2)

            except KeyboardInterrupt:
                print(f"\n自動收集結束！共收集 {img_count} 張訓練樣本。")
                break
            except Exception as e:
                print(f"發生錯誤: {e}")
                time.sleep(1)

if __name__ == "__main__":
    main()
