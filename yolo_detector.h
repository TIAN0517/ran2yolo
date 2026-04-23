#pragma once
// ============================================================
// YOLO 物件偵測器 (Win7~Win11 通用)
// ONNX Runtime v1.12.1 - 動態載入
// ============================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>
#include "onnx_loader.h"
#include "visionentity.h"

// ============================================================
// 前向宣告
// ============================================================
struct YoloResult;
struct VisualMonster;

// ============================================================
// YOLO 偵測結果
// ============================================================
struct YoloBox {
    float x1, y1, x2, y2;  // 邊界框 (模型輸入座標系)
    float confidence;         // 信心度
    int classId;            // 類別 ID
    float classScore;       // 類別分數
};

// ============================================================
// YoloDetector 類別
// ============================================================
class YoloDetector {
public:
    // 模型輸入尺寸 (YOLOv8 預設)
    static const int DEFAULT_INPUT_W = 640;
    static const int DEFAULT_INPUT_H = 640;

    // 預設閾值
    static const float DEFAULT_CONF_THRESH;
    static const float DEFAULT_NMS_THRESH;

    YoloDetector();
    ~YoloDetector();

    // 初始化模型
    // modelPath: ONNX 模型路徑 (相對於 exe 目錄或絕對路徑)
    // 返回: 是否成功
    bool Init(const wchar_t* modelPath);

    // 初始化 (ANSI 版本)
    bool Init(const char* modelPath);

    // 釋放資源
    void Destroy();

    // 檢查是否已初始化
    bool IsReady() const { return m_session != NULL; }

    // 執行推論
    // pixels: BGRA 格式截圖 (由 screenshot.cpp 的 CaptureGameWindow 取得)
    // imgW, imgH: 截圖尺寸
    // results: 輸出結果
    // maxResults: 最大輸出數量
    // 返回: 找到的物件數量
    int Detect(const uint8_t* pixels, int imgW, int imgH,
               YoloBox* results, int maxResults);

    // 執行推論 (使用內部緩衝區)
    int Detect(const std::vector<uint8_t>& pixels, int imgW, int imgH,
               std::vector<YoloBox>& results);

    // 轉換 YoloBox 到 VisualMonster
    // gameW, gameH: 遊戲客戶區尺寸 (通常 1024x768)
    void ConvertToVisualMonster(const YoloBox& box, int imgW, int imgH,
                                int gameW, int gameH, VisualMonster& out);

    // 設定閾值
    void SetConfidenceThreshold(float thresh) { m_confThresh = thresh; }
    void SetNmsThreshold(float thresh) { m_nmsThresh = thresh; }

    // 獲取最後推論時間 (毫秒)
    float GetLastInferenceTime() const { return m_lastInferenceMs; }

    // 獲取模型輸入尺寸
    int GetInputWidth() const { return m_inputW; }
    int GetInputHeight() const { return m_inputH; }

private:
    // 內部初始化 (C API)
    bool InitInternal(const char* modelPath);

    // 影像前處理: BGRA -> NCHW float tensor
    void Preprocess(const uint8_t* src, int srcW, int srcH,
                    float* dst, int dstW, int dstH);

    // 影像前處理 (in-place, 使用內部緩衝區)
    void PreprocessInPlace(const uint8_t* src, int srcW, int srcH);

    // NMS 後處理
    int Postprocess(float* output, int outputSize, int numBoxes,
                    YoloBox* results, int maxResults);

    // 解析 YOLOv8 輸出格式
    // 假設輸出為 [1, 84, 8400] (YOLOv8n) 或 [1, 84, 15600] (YOLOv8s)
    int ParseYolov8Output(float* output, int outSize, int numProposals,
                          YoloBox* results, int maxResults);

    // 計算 IoU
    float ComputeIoU(const YoloBox& a, const YoloBox& b);

    // 座標從模型輸入座標轉換回原始圖片座標
    void ScaleCoords(float& x1, float& y1, float& x2, float& y2,
                     int padX, int padY, float scale, int origW, int origH);

    // ONNX Runtime C API 句柄
    OrtEnv* m_env;
    OrtSession* m_session;
    OrtAllocator* m_allocator;
    char** m_inputNames;
    char** m_outputNames;
    int m_numOutputNames;

    // 模型參數
    int m_inputW;
    int m_inputH;
    int m_numProposals;    // 輸出的候選框數量
    int m_numClasses;      // 類別數量 (通常 80 for COCO)

    // 閾值
    float m_confThresh;
    float m_nmsThresh;

    // 效能
    float m_lastInferenceMs;

    // 內部緩衝區 (避免重複配置)
    std::vector<float> m_inputTensor;    // NCHW format
    std::vector<float> m_outputTensor;   // 模型輸出

    // 推論選項
    OrtRunOptions* m_runOptions;

    // 是否初始化
    bool m_initialized;
};

// ============================================================
// 便利函式: 將 YoloDetector 結果轉換為 VisualMonster 陣列
// ============================================================
int ConvertYoloToVisualMonsters(YoloDetector* detector,
                                 const std::vector<YoloBox>& boxes,
                                 int imgW, int imgH,
                                 int gameW, int gameH,
                                 VisualMonster* outMonsters,
                                 int maxMonsters);
