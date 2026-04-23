#pragma once
// ============================================================
// ONNX Runtime 動態載入器 (Win7~Win11 通用)
// 避免靜態 link 導致 Win7 載入失敗
// ============================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

// ============================================================
// ONNX Runtime C API 指標類型
// ============================================================
typedef struct OrtEnv OrtEnv;
typedef struct OrtStatus OrtStatus;
typedef struct OrtSessionOptions OrtSessionOptions;
typedef struct OrtSession OrtSession;
typedef struct OrtRunOptions OrtRunOptions;
typedef struct OrtAllocator OrtAllocator;
typedef struct OrtMemoryInfo OrtMemoryInfo;
typedef struct OrtValue OrtValue;
typedef struct OrtTypeInfo OrtTypeInfo;
typedef struct OrtTensorTypeAndShapeInfo OrtTensorTypeAndShapeInfo;
typedef enum OrtLoggingLevel : int OrtLoggingLevel;

// ============================================================
// 常數定義
// ============================================================
#ifndef ORT_LOGGING_LEVEL_WARNING
#define ORT_LOGGING_LEVEL_WARNING ((OrtLoggingLevel)3)
#endif
#ifndef ORT_LOGGING_LEVEL_ERROR
#define ORT_LOGGING_LEVEL_ERROR ((OrtLoggingLevel)4)
#endif

#ifndef OrtMemTypeDefault
#define OrtMemTypeDefault 0
#endif

#ifndef OrtArenaAllocator
#define OrtArenaAllocator 1
#endif

#ifndef ORT_ENABLE_ALL
#define ORT_ENABLE_ALL 99U
#endif

#ifndef ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
#define ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT 1
#endif

#ifndef ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED
#define ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED 0
#endif

// ============================================================
// 函數指標類型
// ============================================================
typedef OrtStatus* (*OrtCreateEnvFn)(OrtLoggingLevel, const char*, OrtEnv**);
typedef void (*OrtReleaseEnvFn)(OrtEnv*);
typedef OrtSessionOptions* (*OrtCreateSessionOptionsFn)(void);
typedef void (*OrtReleaseSessionOptionsFn)(OrtSessionOptions*);
typedef OrtStatus* (*OrtSetSessionGraphOptimizationLevelFn)(OrtSessionOptions*, unsigned int);
typedef OrtStatus* (*OrtSetIntraOpNumThreadsFn)(OrtSessionOptions*, int);
typedef OrtStatus* (*OrtSetInterOpNumThreadsFn)(OrtSessionOptions*, int);
typedef OrtStatus* (*OrtCreateSessionFn)(OrtEnv*, const char*, const OrtSessionOptions*, OrtSession**);
typedef void (*OrtReleaseSessionFn)(OrtSession*);
typedef const char* (*OrtGetErrorMessageFn)(OrtStatus*);
typedef void (*OrtReleaseStatusFn)(OrtStatus*);
typedef OrtStatus* (*OrtGetAllocatorFn)(OrtSession*, OrtAllocator**);
typedef void (*OrtReleaseAllocatorFn)(OrtAllocator*);
typedef OrtStatus* (*OrtAllocatorFreeFn)(OrtAllocator*, void*);
typedef OrtStatus* (*OrtRunFn)(OrtSession*, const OrtRunOptions*, const char* const*, OrtValue* const*, size_t, const char* const*, OrtValue**, OrtRunOptions*);
typedef OrtStatus* (*OrtGetTensorDataFn)(OrtValue*, float**, size_t**, int*);
typedef OrtStatus* (*OrtValueGetCountFn)(OrtValue*, size_t*);
typedef OrtStatus* (*OrtReleaseValueFn)(OrtValue*);
typedef OrtStatus* (*OrtCreateTensorWithDataAsOrtValueFn)(const OrtAllocator*, const void*, size_t, const size_t*, size_t, int, OrtValue**);
typedef OrtStatus* (*OrtCreateMemoryInfoFn)(const char*, int, int, int, OrtMemoryInfo**);
typedef void (*OrtReleaseMemoryInfoFn)(OrtMemoryInfo*);
typedef OrtStatus* (*OrtSessionGetInputCountFn)(OrtSession*, size_t*);
typedef OrtStatus* (*OrtSessionGetOutputCountFn)(OrtSession*, size_t*);
typedef OrtStatus* (*OrtSessionGetInputNameFn)(OrtSession*, size_t, OrtAllocator*, char**);
typedef OrtStatus* (*OrtSessionGetOutputNameFn)(OrtSession*, size_t, OrtAllocator*, char**);
typedef OrtStatus* (*OrtSessionGetInputTypeInfoFn)(OrtSession*, size_t, OrtValue**);
typedef OrtStatus* (*OrtCastTypeInfoToTensorInfoFn)(const OrtValue*, const OrtTensorTypeAndShapeInfo**);
typedef void (*OrtReleaseTypeInfoFn)(OrtTypeInfo*);
typedef OrtStatus* (*OrtGetDimensionsCountFn)(const OrtTensorTypeAndShapeInfo*, size_t*);
typedef OrtStatus* (*OrtGetDimensionsFn)(const OrtTensorTypeAndShapeInfo*, int64_t*, size_t);
typedef OrtStatus* (*OrtCreateRunOptionsFn)(OrtRunOptions**);
typedef void (*OrtReleaseRunOptionsFn)(OrtRunOptions*);
typedef OrtStatus* (*OrtRunOptionsSetRunLogSeverityFn)(OrtRunOptions*, int);

// ============================================================
// ONNX Runtime 載入器
// ============================================================
class OnnxLoader {
public:
    OnnxLoader() : m_dll(NULL), m_loaded(false) {}

    ~OnnxLoader() { Unload(); }

    // 載入 ONNX Runtime DLL
    bool Load(const char* modelDir = NULL) {
        if (m_loaded) return true;
        Unload();

        const char* dllNames[] = {
            "onnxruntime.dll",
            "..\\onnxruntime-win-x64-1.17.3\\onnxruntime.dll"
        };

        const char* searchPaths[] = {
            "",
            modelDir ? modelDir : "",
            ".\\",
            "..\\",
            "models\\",
            ".\\onnxruntime-win-x64-1.17.3\\",
            "..\\onnxruntime-win-x64-1.17.3\\"
        };

        bool found = false;
        for (int p = 0; p < 2 && !found; p++) {
            for (int s = 0; s < 7 && !found; s++) {
                std::string dllPath = searchPaths[s];
                if (!dllPath.empty() && dllPath.back() != '\\' && dllPath.back() != '/') {
                    dllPath += "\\";
                }
                std::string fullPath = dllPath + dllNames[p];
                m_dll = LoadLibraryA(fullPath.c_str());
                if (m_dll) found = true;
            }
        }

        if (!found) {
            m_dll = GetModuleHandleA("onnxruntime.dll");
            if (m_dll) found = true;
        }

        if (!found) {
            printf("[OnnxLoader] ONNX Runtime DLL not found, YOLO disabled\n");
            return false;
        }

        #define LOAD_PROC(type, name) \
            name = (type)GetProcAddress(m_dll, #name); \
            if (!name) { printf("[OnnxLoader] Missing export: " #name "\n"); Unload(); return false; }

        LOAD_PROC(OrtCreateEnvFn, OrtCreateEnv);
        LOAD_PROC(OrtReleaseEnvFn, OrtReleaseEnv);
        LOAD_PROC(OrtCreateSessionOptionsFn, OrtCreateSessionOptions);
        LOAD_PROC(OrtReleaseSessionOptionsFn, OrtReleaseSessionOptions);
        LOAD_PROC(OrtSetSessionGraphOptimizationLevelFn, OrtSetSessionGraphOptimizationLevel);
        LOAD_PROC(OrtSetIntraOpNumThreadsFn, OrtSetIntraOpNumThreads);
        LOAD_PROC(OrtSetInterOpNumThreadsFn, OrtSetInterOpNumThreads);
        LOAD_PROC(OrtCreateSessionFn, OrtCreateSession);
        LOAD_PROC(OrtReleaseSessionFn, OrtReleaseSession);
        LOAD_PROC(OrtGetErrorMessageFn, OrtGetErrorMessage);
        LOAD_PROC(OrtReleaseStatusFn, OrtReleaseStatus);
        LOAD_PROC(OrtGetAllocatorFn, OrtGetAllocator);
        LOAD_PROC(OrtReleaseAllocatorFn, OrtReleaseAllocator);
        LOAD_PROC(OrtAllocatorFreeFn, OrtAllocatorFree);
        LOAD_PROC(OrtRunFn, OrtRun);
        LOAD_PROC(OrtGetTensorDataFn, OrtGetTensorData);
        LOAD_PROC(OrtValueGetCountFn, OrtValueGetCount);
        LOAD_PROC(OrtReleaseValueFn, OrtReleaseValue);
        LOAD_PROC(OrtCreateTensorWithDataAsOrtValueFn, OrtCreateTensorWithDataAsOrtValue);
        LOAD_PROC(OrtCreateMemoryInfoFn, OrtCreateMemoryInfo);
        LOAD_PROC(OrtReleaseMemoryInfoFn, OrtReleaseMemoryInfo);
        LOAD_PROC(OrtSessionGetInputCountFn, OrtSessionGetInputCount);
        LOAD_PROC(OrtSessionGetOutputCountFn, OrtSessionGetOutputCount);
        LOAD_PROC(OrtSessionGetInputNameFn, OrtSessionGetInputName);
        LOAD_PROC(OrtSessionGetOutputNameFn, OrtSessionGetOutputName);
        LOAD_PROC(OrtSessionGetInputTypeInfoFn, OrtSessionGetInputTypeInfo);
        LOAD_PROC(OrtCastTypeInfoToTensorInfoFn, OrtCastTypeInfoToTensorInfo);
        LOAD_PROC(OrtReleaseTypeInfoFn, OrtReleaseTypeInfo);
        LOAD_PROC(OrtGetDimensionsCountFn, OrtGetDimensionsCount);
        LOAD_PROC(OrtGetDimensionsFn, OrtGetDimensions);
        LOAD_PROC(OrtCreateRunOptionsFn, OrtCreateRunOptions);
        LOAD_PROC(OrtReleaseRunOptionsFn, OrtReleaseRunOptions);
        LOAD_PROC(OrtRunOptionsSetRunLogSeverityFn, OrtRunOptionsSetRunLogSeverity);

        #undef LOAD_PROC

        m_loaded = true;
        printf("[OnnxLoader] ONNX Runtime loaded successfully\n");
        return true;
    }

    // 卸載 DLL
    void Unload() {
        if (m_dll && m_dll != GetModuleHandleA("onnxruntime.dll")) {
            FreeLibrary(m_dll);
        }
        m_dll = NULL;
        m_loaded = false;

        #define CLEAR_PROC(name) name = nullptr;
        CLEAR_PROC(OrtCreateEnv);
        CLEAR_PROC(OrtReleaseEnv);
        CLEAR_PROC(OrtCreateSessionOptions);
        CLEAR_PROC(OrtReleaseSessionOptions);
        CLEAR_PROC(OrtSetSessionGraphOptimizationLevel);
        CLEAR_PROC(OrtSetIntraOpNumThreads);
        CLEAR_PROC(OrtSetInterOpNumThreads);
        CLEAR_PROC(OrtCreateSession);
        CLEAR_PROC(OrtReleaseSession);
        CLEAR_PROC(OrtGetErrorMessage);
        CLEAR_PROC(OrtReleaseStatus);
        CLEAR_PROC(OrtGetAllocator);
        CLEAR_PROC(OrtReleaseAllocator);
        CLEAR_PROC(OrtAllocatorFree);
        CLEAR_PROC(OrtRun);
        CLEAR_PROC(OrtGetTensorData);
        CLEAR_PROC(OrtValueGetCount);
        CLEAR_PROC(OrtReleaseValue);
        CLEAR_PROC(OrtCreateTensorWithDataAsOrtValue);
        CLEAR_PROC(OrtCreateMemoryInfo);
        CLEAR_PROC(OrtReleaseMemoryInfo);
        CLEAR_PROC(OrtSessionGetInputCount);
        CLEAR_PROC(OrtSessionGetOutputCount);
        CLEAR_PROC(OrtSessionGetInputName);
        CLEAR_PROC(OrtSessionGetOutputName);
        CLEAR_PROC(OrtSessionGetInputTypeInfo);
        CLEAR_PROC(OrtCastTypeInfoToTensorInfo);
        CLEAR_PROC(OrtReleaseTypeInfo);
        CLEAR_PROC(OrtGetDimensionsCount);
        CLEAR_PROC(OrtGetDimensions);
        CLEAR_PROC(OrtCreateRunOptions);
        CLEAR_PROC(OrtReleaseRunOptions);
        CLEAR_PROC(OrtRunOptionsSetRunLogSeverity);
        #undef CLEAR_PROC
    }

    bool IsLoaded() const { return m_loaded; }

    // 函數指標
    OrtCreateEnvFn OrtCreateEnv = nullptr;
    OrtReleaseEnvFn OrtReleaseEnv = nullptr;
    OrtCreateSessionOptionsFn OrtCreateSessionOptions = nullptr;
    OrtReleaseSessionOptionsFn OrtReleaseSessionOptions = nullptr;
    OrtSetSessionGraphOptimizationLevelFn OrtSetSessionGraphOptimizationLevel = nullptr;
    OrtSetIntraOpNumThreadsFn OrtSetIntraOpNumThreads = nullptr;
    OrtSetInterOpNumThreadsFn OrtSetInterOpNumThreads = nullptr;
    OrtCreateSessionFn OrtCreateSession = nullptr;
    OrtReleaseSessionFn OrtReleaseSession = nullptr;
    OrtGetErrorMessageFn OrtGetErrorMessage = nullptr;
    OrtReleaseStatusFn OrtReleaseStatus = nullptr;
    OrtGetAllocatorFn OrtGetAllocator = nullptr;
    OrtReleaseAllocatorFn OrtReleaseAllocator = nullptr;
    OrtAllocatorFreeFn OrtAllocatorFree = nullptr;
    OrtRunFn OrtRun = nullptr;
    OrtGetTensorDataFn OrtGetTensorData = nullptr;
    OrtValueGetCountFn OrtValueGetCount = nullptr;
    OrtReleaseValueFn OrtReleaseValue = nullptr;
    OrtCreateTensorWithDataAsOrtValueFn OrtCreateTensorWithDataAsOrtValue = nullptr;
    OrtCreateMemoryInfoFn OrtCreateMemoryInfo = nullptr;
    OrtReleaseMemoryInfoFn OrtReleaseMemoryInfo = nullptr;
    OrtSessionGetInputCountFn OrtSessionGetInputCount = nullptr;
    OrtSessionGetOutputCountFn OrtSessionGetOutputCount = nullptr;
    OrtSessionGetInputNameFn OrtSessionGetInputName = nullptr;
    OrtSessionGetOutputNameFn OrtSessionGetOutputName = nullptr;
    OrtSessionGetInputTypeInfoFn OrtSessionGetInputTypeInfo = nullptr;
    OrtCastTypeInfoToTensorInfoFn OrtCastTypeInfoToTensorInfo = nullptr;
    OrtReleaseTypeInfoFn OrtReleaseTypeInfo = nullptr;
    OrtGetDimensionsCountFn OrtGetDimensionsCount = nullptr;
    OrtGetDimensionsFn OrtGetDimensions = nullptr;
    OrtCreateRunOptionsFn OrtCreateRunOptions = nullptr;
    OrtReleaseRunOptionsFn OrtReleaseRunOptions = nullptr;
    OrtRunOptionsSetRunLogSeverityFn OrtRunOptionsSetRunLogSeverity = nullptr;

private:
    HMODULE m_dll;
    bool m_loaded;
};

// ============================================================
// 全域 ONNX 載入器
// ============================================================
extern OnnxLoader g_onnxLoader;

// ============================================================
// 巨集：用於在未載入時安全返回
// ============================================================
#define ONNX_CHECK_LOADED() \
    if (!g_onnxLoader.IsLoaded()) { \
        printf("[YoloDetector] ONNX Runtime not loaded\n"); \
        return false; \
    }