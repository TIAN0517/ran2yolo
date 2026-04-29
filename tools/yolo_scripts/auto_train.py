from ultralytics import YOLO
import shutil
import os

def main():
    print("=== 開始自動訓練 YOLOv8 模型 ===")

    # 載入預訓練模型 (第一次執行會自動下載)
    model = YOLO("yolov8n.pt")

    # 訓練
    results = model.train(
        data="training_data/dataset.yaml",
        epochs=50,
        imgsz=640,
        batch=8,
        name="ran2_yolo"
    )

    print("\n=== 訓練完成！開始匯出 ONNX 格式 ===")

    # 匯出成 ONNX
    export_path = model.export(format="onnx", opset=12, simplify=True)

    print(f"\nONNX 模型已匯出至: {export_path}")

    # 自動複製到專案的 models 資料夾
    project_models_dir = "models"
    os.makedirs(project_models_dir, exist_ok=True)

    dest_path = os.path.join(project_models_dir, "best.onnx")
    shutil.copy(export_path, dest_path)

    print(f"已自動將模型複製到: {dest_path}")
    print("現在您可以直接開啟 JyTrainer 測試最新的 YOLO 模型了！")

if __name__ == "__main__":
    main()
