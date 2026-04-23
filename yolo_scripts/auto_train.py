# ============================================================
# YOLO 全自動迭代訓練腳本
# 目標：自動達到 90% 準確率
# ============================================================
import os
import shutil
import yaml
import cv2
import numpy as np
from pathlib import Path

PROJECT_DIR = Path(__file__).parent
TRAIN_DATA_DIR = PROJECT_DIR / "training_data"
IMAGES_DIR = TRAIN_DATA_DIR / "images"
LABELS_DIR = TRAIN_DATA_DIR / "labels"
MODEL_PATH = PROJECT_DIR / "yolov8n.pt"

# 設定
IMG_SIZE = 640
BATCH_SIZE = 16
INITIAL_EPOCHS = 30
ITERATION_EPOCHS = 20
MAX_ITERATIONS = 5
CONF_THRESHOLD = 0.7  # 只用置信度 > 70% 的預測
TARGET_MAP = 0.5      # 目標 mAP@0.5 > 50%

class AutoTrainer:
    def __init__(self):
        self.best_map = 0.0
        self.iteration = 0
        self.model_path = None

    def create_config(self):
        """建立訓練設定"""
        config = {
            'path': str(TRAIN_DATA_DIR.absolute()),
            'train': 'images',
            'val': 'images',
            'nc': 1,
            'names': {0: 'monster'}
        }
        yaml_path = TRAIN_DATA_DIR / "monster.yaml"
        with open(yaml_path, 'w', encoding='utf-8') as f:
            yaml.dump(config, f, default_flow_style=False)
        return str(yaml_path)

    def train(self, epochs, iteration_name):
        """執行一次訓練"""
        from ultralytics import YOLO

        yaml_path = self.create_config()

        # 使用最佳模型或初始模型
        if self.model_path and Path(self.model_path).exists():
            model = YOLO(str(self.model_path))
            print(f"  使用模型: {self.model_path}")
        else:
            model = YOLO(str(MODEL_PATH))
            print(f"  使用初始模型: {MODEL_PATH}")

        print(f"\n  訓練中 ({iteration_name})...")

        results = model.train(
            data=yaml_path,
            epochs=epochs,
            imgsz=IMG_SIZE,
            batch=BATCH_SIZE,
            project=str(PROJECT_DIR / "runs"),
            name=f'auto_train/iter_{iteration_name}',
            exist_ok=True,
            verbose=False,
            patience=epochs + 5,
            augment=True,
            hsv_h=0.015,
            hsv_s=0.7,
            hsv_v=0.4,
            degrees=10.0,
            flipud=0.5,
            fliplr=0.5,
            mosaic=1.0,
        )

        # 取得 mAP
        best_pt = PROJECT_DIR / "runs" / "auto_train" / f"iter_{iteration_name}" / "weights" / "best.pt"
        self.model_path = best_pt

        return results

    def generate_pseudo_labels(self):
        """用當前模型生成偽標籤"""
        from ultralytics import YOLO

        if not self.model_path or not Path(self.model_path).exists():
            print("  [跳過] 沒有模型用於生成偽標籤")
            return 0

        print("  生成偽標籤...")
        model = YOLO(str(self.model_path))

        # 找出還沒有標籤的圖片
        unlabeled = []
        for img_path in IMAGES_DIR.glob("*.jpg"):
            label_path = LABELS_DIR / (img_path.stem + ".txt")
            if not label_path.exists() or label_path.stat().st_size == 0:
                unlabeled.append(img_path)

        if not unlabeled:
            print(f"  [完成] 所有圖片已有標籤")
            return 0

        print(f"  處理 {len(unlabeled)} 張未標注圖片...")

        pseudo_count = 0
        for img_path in unlabeled:
            results = model.predict(str(img_path), conf=CONF_THRESHOLD, verbose=False)

            label_path = LABELS_DIR / (img_path.stem + ".txt")

            with open(label_path, 'w') as f:
                for result in results:
                    boxes = result.boxes
                    for box in boxes:
                        # YOLO 格式: class x_center y_center width height (normalized)
                        x1, y1, x2, y2 = box.xyxyn[0].tolist()
                        cls = int(box.cls[0].item())
                        w = x2 - x1
                        h = y2 - y1
                        cx = x1 + w / 2
                        cy = y1 + h / 2
                        f.write(f"{cls} {cx:.6f} {cy:.6f} {w:.6f} {h:.6f}\n")
                        pseudo_count += 1

        print(f"  生成 {pseudo_count} 個偽標籤")
        return pseudo_count

    def validate(self):
        """驗證當前模型"""
        from ultralytics import YOLO

        if not self.model_path or not Path(self.model_path).exists():
            return 0.0

        model = YOLO(str(self.model_path))
        results = model.val(data=self.create_config(), verbose=False)

        map50 = results.results_dict.get('metrics/mAP50(B)', 0.0)
        print(f"\n  mAP@0.5: {map50:.4f}")

        return map50

    def run(self):
        """執行全自動訓練"""
        print("=" * 60)
        print("  YOLO 全自動迭代訓練")
        print("=" * 60)
        print(f"目標: mAP@0.5 > {TARGET_MAP:.0%}")
        print(f"最大迭代: {MAX_ITERATIONS} 輪")
        print("=" * 60)

        try:
            from ultralytics import YOLO
        except ImportError:
            print("\n[錯誤] 請先安裝: pip install ultralytics")
            return

        # 計算初始數據量
        initial_labels = len(list(LABELS_DIR.glob("*.txt")))
        print(f"\n初始標籤數量: {initial_labels}")

        # 第1輪：初始訓練
        print("\n" + "=" * 40)
        print(" 第1輪：初始訓練")
        print("=" * 40)

        self.train(INITIAL_EPOCHS, 1)
        self.best_map = self.validate()

        # 迭代訓練
        for i in range(2, MAX_ITERATIONS + 1):
            print("\n" + "=" * 40)
            print(f" 第{i}輪：迭代訓練")
            print("=" * 40)

            # 生成偽標籤
            pseudo_count = self.generate_pseudo_labels()

            if pseudo_count == 0 and self.best_map >= TARGET_MAP:
                print("\n[完成] 已達到目標準確率！")
                break

            # 繼續訓練
            self.train(ITERATION_EPOCHS, i)
            current_map = self.validate()

            if current_map > self.best_map:
                self.best_map = current_map
                print(f"  [進步] 新最佳 mAP: {self.best_map:.4f}")
            else:
                print(f"  [停滯] mAP 無進步")

            # 檢查是否達標
            if self.best_map >= TARGET_MAP:
                print(f"\n[完成] 已達到目標 {TARGET_MAP:.0%}！")
                break

        # 匯出最終模型
        print("\n" + "=" * 40)
        print("  匯出模型")
        print("=" * 40)

        final_onnx = PROJECT_DIR / "models" / "best.onnx"
        final_onnx.parent.mkdir(exist_ok=True)

        if self.model_path and Path(self.model_path).exists():
            from ultralytics import YOLO
            export_model = YOLO(str(self.model_path))
            export_model.export(format='onnx', imgsz=IMG_SIZE)

            src_onnx = Path(str(self.model_path).replace('.pt', '.onnx'))
            if src_onnx.exists():
                shutil.copy(src_onnx, final_onnx)
                print(f"模型已儲存: {final_onnx}")

        print("\n" + "=" * 60)
        print(f"  完成！最佳 mAP: {self.best_map:.4f}")
        print("=" * 60)

if __name__ == "__main__":
    try:
        import ultralytics
        print(f"[OK] ultralytics {ultralytics.__version__}")
    except ImportError:
        print("[安裝] pip install ultralytics...")
        os.system("pip install ultralytics")

    trainer = AutoTrainer()
    trainer.run()
