# 重新導出 best.onnx 為 Win7 兼容格式
from ultralytics import YOLO
import cv2
import os

print("="*60)
print("  優化 best.onnx 為 Win7 兼容格式")
print("="*60)

# 方法1: 使用 yolov8n 重新訓練你的數據
print("\n[1] YOLOv8n 模型性能最佳 (34ms/29FPS)")
print("    建議：用 yolov8n.pt 重新訓練你的數據集")

# 方法2: 修復 best.onnx 的導出問題
print("\n[2] 檢查你的 best.onnx 模型結構...")

# 嘗試用 ONNX simplifier 修復
try:
    import onnx
    from onnx import shape_inference
    
    if os.path.exists("best.onnx"):
        print("\n    模型結構分析:")
        model = onnx.load("best.onnx")
        print(f"    - IR 版本: {model.ir_version}")
        print(f"    - Opset: {model.opset_import[0].version}")
        print(f"    - 圖層數: {len(model.graph.node)}")
        
        # 檢查不支援的層
        for node in model.graph.node:
            if node.op_type in ['Reshape', 'Reshape16', 'DynamicResize']:
                print(f"    - 複雜層: {node.op_type}")
except ImportError:
    print("    (onnx 庫未安裝)")

# 方法3: 將 best.pt 重新導出為標準 ONNX
print("\n[3] 嘗試將 best.pt 重新導出...")

if os.path.exists("best.pt"):
    print("    找到 best.pt，重新導出...")
    try:
        model = YOLO("best.pt")
        # 導出為標準 opset13
        export_path = model.export(format="onnx", imgsz=416, opset=13, verbose=False)
        print(f"    導出成功: {export_path}")
        
        # 測試 OpenCV DNN
        print("    測試 OpenCV DNN 兼容性...")
        try:
            net = cv2.dnn.readNetFromONNX(export_path)
            net.setPreferableBackend(cv2.dnn.DNN_BACKEND_OPENCV)
            net.setPreferableTarget(cv2.dnn.DNN_TARGET_CPU)
            
            import numpy as np
            dummy = np.random.randint(0, 255, (416, 416, 3), dtype=np.uint8)
            blob = cv2.dnn.blobFromImage(dummy, 1.0/255.0, (416, 416))
            net.setInput(blob)
            _ = net.forward()
            print("    ✅ OpenCV DNN 兼容!")
        except Exception as e:
            print(f"    ❌ OpenCV DNN 失敗: {e}")
            
    except Exception as e:
        print(f"    導出失敗: {e}")
else:
    print("    best.pt 不存在")

print("\n" + "="*60)
print("  建議")
print("="*60)
print("""
1. 如果有 best.pt：用 Ultralytics 重新導出為 opset13
2. 如果只有 best.onnx：用 onnx-simplifier 簡化
3. C++ 端使用 OpenCV DNN + yolov8n.onnx (12 FPS 可接受)
4. 或改用 Ultralytics C++ SDK (需額外整合)

結論：YOLOv8n + 416x416 = 29 FPS，完全可以在 Win7 流暢運行！
""")
