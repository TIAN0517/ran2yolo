# ============================================================
# 簡單標注工具 - RAN2 YOLO 訓練數據製作
# 優化版：繁體中文 + 自動儲存 + 進度追蹤
# ============================================================
import os
import sys
import cv2
import numpy as np
from pathlib import Path

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox, scrolledtext
    from PIL import Image, ImageTk
except ImportError:
    print("需要安裝依賴: pip install pillow opencv-python")
    sys.exit(1)

# ============================================================
# 設定
# ============================================================
PROJECT_DIR = Path(__file__).parent
IMAGES_DIR = PROJECT_DIR / "training_data" / "images"
LABELS_DIR = PROJECT_DIR / "training_data" / "labels"

# 類別定義（繁體中文說明）
CLASSES = [
    "怪物",      # 0 - monster
    "玩家",      # 1 - player
    "NPC",       # 2 - npc
    "商店NPC"    # 3 - npc_shop
]

CLASS_COLORS = [
    (0, 255, 0),     # 綠色 - 怪物
    (255, 0, 0),     # 紅色 - 玩家
    (0, 255, 255),   # 黃色 - NPC
    (255, 0, 255)    # 紫色 - 商店NPC
]

CLASS_HOTKEYS = ["1", "2", "3", "4"]

IMAGES_DIR.mkdir(parents=True, exist_ok=True)
LABELS_DIR.mkdir(parents=True, exist_ok=True)

# ============================================================
# 標注工具類別
# ============================================================
class Labeler:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("RAN2 標注工具 - YOLO 訓練數據")
        self.root.geometry("1400x950")

        self.images = []
        self.current_idx = 0
        self.annotations = []
        self.drawing = False
        self.start_x = 0
        self.start_y = 0
        self.current_class = 0
        self.auto_save = tk.BooleanVar(value=True)

        self.create_widgets()
        self.load_images()
        self.update_stats()

    def create_widgets(self):
        # ===== 頂部工具列 =====
        toolbar = tk.Frame(self.root, bg="#2b2b2b", padx=5, pady=5)
        toolbar.pack(side=tk.TOP, fill=tk.X)

        # 左側按鈕群組
        btn_group = tk.Frame(toolbar, bg="#2b2b2b")
        btn_group.pack(side=tk.LEFT)

        tk.Button(btn_group, text="📂 開啟資料夾", command=self.open_folder,
                  bg="#4a4a4a", fg="white", width=12).pack(side=tk.LEFT, padx=2)
        tk.Button(btn_group, text="⬅️ 上一張 (A)", command=self.prev_image,
                  bg="#4a4a4a", fg="white", width=12).pack(side=tk.LEFT, padx=2)
        tk.Button(btn_group, text="➡️ 下一張 (D)", command=self.next_image,
                  bg="#4a4a4a", fg="white", width=12).pack(side=tk.LEFT, padx=2)
        tk.Button(btn_group, text="💾 儲存 (S)", command=self.save_annotation,
                  bg="#2e7d32", fg="white", width=10).pack(side=tk.LEFT, padx=2)
        tk.Button(btn_group, text="↩️ 復原 (Z)", command=self.undo,
                  bg="#4a4a4a", fg="white", width=10).pack(side=tk.LEFT, padx=2)
        tk.Button(btn_group, text="🗑️ 清空", command=self.clear_all,
                  bg="#c62828", fg="white", width=8).pack(side=tk.LEFT, padx=2)

        # 右側自動儲存選項
        tk.Checkbutton(toolbar, text="自動儲存", variable=self.auto_save,
                       bg="#2b2b2b", fg="white", selectcolor="#4a4a4a").pack(side=tk.RIGHT, padx=10)

        # ===== 類別選擇區 =====
        class_panel = tk.Frame(self.root, bg="#1e1e1e", padx=10, pady=10)
        class_panel.pack(side=tk.TOP, fill=tk.X)

        tk.Label(class_panel, text="選擇類別：", bg="#1e1e1e", fg="white",
                 font=("微軟正黑體", 12, "bold")).pack(side=tk.LEFT, padx=10)

        self.class_buttons = []
        class_names = [
            ("怪物 [1]", 0, "#1b5e20"),      # 深綠
            ("玩家 [2]", 1, "#b71c1c"),      # 深紅
            ("NPC [3]", 2, "#f57f17"),       # 深黃
            ("商店 [4]", 3, "#6a1b9a")       # 深紫
        ]

        for name, idx, color in class_names:
            btn = tk.Button(class_panel, text=name, width=10, height=2,
                          bg=color, fg="white", font=("微軟正黑體", 11, "bold"),
                          command=lambda i=idx: self.select_class(i))
            btn.pack(side=tk.LEFT, padx=5)
            self.class_buttons.append(btn)

        # 當前類別提示
        self.class_label = tk.Label(class_panel, text="目前：怪物", bg="#1e1e1e",
                                     fg="#1b5e20", font=("微軟正黑體", 14, "bold"))
        self.class_label.pack(side=tk.LEFT, padx=20)

        # ===== 說明面板 =====
        help_frame = tk.Frame(self.root, bg="#1e1e1e", padx=10, pady=5)
        help_frame.pack(side=tk.TOP, fill=tk.X)

        help_text = "操作說明：滑鼠左鍵拖曳 = 畫框 | 右鍵點擊 = 刪除框 | A/D = 上/下一張 | Z = 復原"
        tk.Label(help_frame, text=help_text, bg="#1e1e1e", fg="#888888",
                 font=("微軟正黑體", 9)).pack(side=tk.LEFT)

        # ===== 圖片顯示區 =====
        self.canvas = tk.Canvas(self.root, bg="#1a1a1a", cursor="cross")
        self.canvas.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.canvas.bind("<ButtonPress-1>", self.on_press)
        self.canvas.bind("<B1-Motion>", self.on_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_release)
        self.canvas.bind("<ButtonPress-3>", self.on_right_click)

        # ===== 底部狀態列 =====
        bottom = tk.Frame(self.root, bg="#2b2b2b", padx=10, pady=8)
        bottom.pack(side=tk.BOTTOM, fill=tk.X)

        self.status = tk.Label(bottom, text="尚未載入圖片", anchor=tk.W,
                               bg="#2b2b2b", fg="#00ff00", font=("Consolas", 11))
        self.status.pack(side=tk.LEFT, fill=tk.X, expand=True)

        # ===== 快捷鍵綁定 =====
        self.root.bind("<Key-a>", lambda e: self.prev_image())
        self.root.bind("<Key-d>", lambda e: self.next_image())
        self.root.bind("<Key-s>", lambda e: self.save_annotation())
        self.root.bind("<Key-z>", lambda e: self.undo())
        self.root.bind("<Key-1>", lambda e: self.select_class(0))
        self.root.bind("<Key-2>", lambda e: self.select_class(1))
        self.root.bind("<Key-3>", lambda e: self.select_class(2))
        self.root.bind("<Key-4>", lambda e: self.select_class(3))
        self.root.bind("<Left>", lambda e: self.prev_image())
        self.root.bind("<Right>", lambda e: self.next_image())

    def open_folder(self):
        folder = filedialog.askdirectory(initialdir=str(IMAGES_DIR))
        if folder:
            self.load_folder(folder)

    def load_folder(self, folder):
        self.images = sorted([
            f for f in Path(folder).glob("*")
            if f.suffix.lower() in ['.jpg', '.png', '.jpeg', '.bmp']
        ])
        if self.images:
            self.current_idx = 0
            self.load_image()
            self.update_stats()
        else:
            messagebox.showinfo("提示", "資料夾中沒有圖片")

    def load_images(self):
        if IMAGES_DIR.exists():
            self.load_folder(str(IMAGES_DIR))

    def load_image(self):
        if not self.images:
            return

        img_path = self.images[self.current_idx]
        self.current_image = cv2.imread(str(img_path))
        if self.current_image is None:
            self.status.config(text=f"❌ 無法載入圖片: {img_path.name}")
            return

        self.current_image_rgb = cv2.cvtColor(self.current_image, cv2.COLOR_BGR2RGB)
        self.load_annotation()
        self.show_image()
        self.update_status()

    def show_image(self):
        if not hasattr(self, 'current_image_rgb'):
            return

        show_img = self.current_image_rgb.copy()
        h, w = show_img.shape[:2]

        # 計算縮放比例
        canvas_w = self.canvas.winfo_width() or 1300
        canvas_h = self.canvas.winfo_height() or 800
        self.scale = min(canvas_w / w, canvas_h / h, 1.5)
        new_w, new_h = int(w * self.scale), int(h * self.scale)

        # 繪製標注框
        for ann in self.annotations:
            x1, y1, x2, y2, cls_id = ann
            color = CLASS_COLORS[cls_id]
            cv2.rectangle(show_img, (x1, y1), (x2, y2), color, 2)
            label = CLASSES[cls_id]
            cv2.putText(show_img, label, (x1, max(y1-5, 10)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)

        resized = cv2.resize(show_img, (new_w, new_h))
        self.img_tk = ImageTk.PhotoImage(Image.fromarray(resized))

        self.canvas.delete("all")
        self.canvas.create_image(canvas_w//2, canvas_h//2, anchor=tk.CENTER, image=self.img_tk)

    def load_annotation(self):
        self.annotations = []
        if not self.images:
            return

        label_path = LABELS_DIR / (self.images[self.current_idx].stem + ".txt")
        if label_path.exists():
            with open(label_path, 'r', encoding='utf-8') as f:
                for line in f:
                    parts = line.strip().split()
                    if len(parts) >= 5:
                        cls_id = int(parts[0])
                        cx, cy, nw, nh = float(parts[1]), float(parts[2]), float(parts[3]), float(parts[4])
                        h, w = self.current_image.shape[:2]
                        x1 = int((cx - nw/2) * w)
                        y1 = int((cy - nh/2) * h)
                        x2 = int((cx + nw/2) * w)
                        y2 = int((cy + nh/2) * h)
                        self.annotations.append((x1, y1, x2, y2, cls_id))

    def save_annotation(self):
        if not self.images or not hasattr(self, 'current_image'):
            return

        img_path = self.images[self.current_idx]
        label_path = LABELS_DIR / (img_path.stem + ".txt")
        h, w = self.current_image.shape[:2]

        with open(label_path, 'w', encoding='utf-8') as f:
            for ann in self.annotations:
                x1, y1, x2, y2, cls_id = ann
                cx = (x1 + x2) / 2 / w
                cy = (y1 + y2) / 2 / h
                nw = (x2 - x1) / w
                nh = (y2 - y1) / h
                f.write(f"{cls_id} {cx:.6f} {cy:.6f} {nw:.6f} {nh:.6f}\n")

        self.status.config(text=f"✅ 已儲存: {label_path.name}", fg="#00ff00")
        self.root.after(1500, lambda: self.update_status())
        self.update_stats()

    def select_class(self, idx):
        self.current_class = idx
        colors = ["#1b5e20", "#b71c1c", "#f57f17", "#6a1b9a"]
        for i, btn in enumerate(self.class_buttons):
            btn.config(bg=colors[i])
        self.class_buttons[idx].config(bg="#4caf50")
        self.class_label.config(text=f"目前：{CLASSES[idx]}", fg=CLASS_COLORS[idx][::-1])

    def on_press(self, event):
        self.drawing = True
        self.start_x = int(event.x / self.scale)
        self.start_y = int(event.y / self.scale)

    def on_drag(self, event):
        if self.drawing:
            self.canvas.delete("draw_box")
            self.canvas.create_rectangle(
                self.start_x * self.scale,
                self.start_y * self.scale,
                event.x, event.y,
                outline="white", width=2, tags="draw_box"
            )

    def on_release(self, event):
        if self.drawing:
            self.drawing = False
            end_x = int(event.x / self.scale)
            end_y = int(event.y / self.scale)

            x1, x2 = min(self.start_x, end_x), max(self.start_x, end_x)
            y1, y2 = min(self.start_y, end_y), max(self.start_y, end_y)

            if x2 - x1 > 5 and y2 - y1 > 5:
                self.annotations.append((x1, y1, x2, y2, self.current_class))
                self.show_image()

                if self.auto_save.get():
                    self.save_annotation()

    def on_right_click(self, event):
        x = int(event.x / self.scale)
        y = int(event.y / self.scale)
        for i, ann in enumerate(self.annotations):
            x1, y1, x2, y2, _ = ann
            if x1 <= x <= x2 and y1 <= y <= y2:
                self.annotations.pop(i)
                self.show_image()
                if self.auto_save.get():
                    self.save_annotation()
                break

    def prev_image(self):
        if self.current_idx > 0:
            self.current_idx -= 1
            self.load_image()

    def next_image(self):
        if self.images and self.current_idx < len(self.images) - 1:
            self.current_idx += 1
            self.load_image()

    def undo(self):
        if self.annotations:
            self.annotations.pop()
            self.show_image()
            if self.auto_save.get():
                self.save_annotation()

    def clear_all(self):
        if messagebox.askyesno("確認", "確定要清空所有標注嗎？"):
            self.annotations = []
            self.show_image()
            if self.auto_save.get():
                self.save_annotation()

    def update_status(self):
        if not self.images:
            self.status.config(text="尚未載入圖片", fg="#888888")
            return

        total = len(self.images)
        current = self.current_idx + 1
        ann_count = len(self.annotations)
        cls_name = CLASSES[self.current_class]

        text = f"第 {current}/{total} 張 | 標注: {ann_count} 個 | 類別: {cls_name} | {self.images[self.current_idx].name}"
        self.status.config(text=text, fg="#00ff00")

    def update_stats(self):
        """更新整體統計"""
        total_images = len(list(IMAGES_DIR.glob("*.jpg"))) + len(list(IMAGES_DIR.glob("*.png")))
        total_labels = len(list(LABELS_DIR.glob("*.txt")))

        # 統計各類別數量
        class_counts = [0] * len(CLASSES)
        for label_file in LABELS_DIR.glob("*.txt"):
            with open(label_file, 'r', encoding='utf-8', errors='ignore') as f:
                for line in f:
                    parts = line.strip().split()
                    if parts:
                        try:
                            cls_id = int(parts[0])
                            if 0 <= cls_id < len(CLASSES):
                                class_counts[cls_id] += 1
                        except:
                            pass

        # 顯示在狀態列
        stats_text = f"圖片: {total_images} | 標籤: {total_labels} | "
        for i, cnt in enumerate(class_counts):
            stats_text += f"{CLASSES[i]}:{cnt} "
        self.status.config(text=stats_text, fg="#00ff00")

# ============================================================
# 主程式
# ============================================================
if __name__ == "__main__":
    print("=" * 50)
    print(" RAN2 標注工具 - YOLO 訓練數據")
    print("=" * 50)
    print(f"圖片資料夾: {IMAGES_DIR}")
    print(f"標籤資料夾: {LABELS_DIR}")
    print()
    print("類別說明：")
    for i, cls in enumerate(CLASSES):
        print(f"  [{i+1}] {cls}")
    print()
    print("快捷鍵：")
    print("  A/D 或 左右方向鍵 - 上/下一張")
    print("  S - 儲存")
    print("  Z - 復原")
    print("  1-4 - 切換類別")
    print("  滑鼠左鍵拖曳 - 畫框")
    print("  右鍵點擊 - 刪除框")
    print()
    print("啟動中...")
    app = Labeler()
    app.run()
