#pragma once
// ============================================================
// ONNX Runtime 動態載入器 (Win7~Win11 通用)
// 支援 x86 和 x64 自動偵測
// ============================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

// ============================================================
// 偵測當前進程架構
// ============================================================
#ifdef _WIN64
    #define ONNX_ARCH x64
    #define ONNX_ARCH_STR "x64"
#else
    #define ONNX_ARCH x86
    #define ONNX_ARCH_STR "x86"
#endif

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
// ONNX Runtime 載入器 (自動偵測架構)
// ============================================================
class OnnxLoader {
public:
    OnnxLoader() : m_dll(NULL), m_loaded(false), m_arch(ONNX_ARCH) {}

    ~OnnxLoader() { Unload(); }

    // 載入 ONNX Runtime DLL (自動偵測架構)
    bool Load(const char* modelDir = NULL) {
        if (m_loaded) return true;
        Unload();

        // 根據架構選擇 DLL 名稱
        const char* dllName = (m_arch == x86) ? "onnxruntime.dll" : "onnxruntime.dll";

        // 搜尋路徑列表 (根據架構優先)
        const char* searchPaths[] = {
            "",                          // 同目錄
            ".\\",
            "models\\",
            ".\\onnxruntime\\",
            "..\\onnxruntime\\",
            ".\\onnxruntime-x86\\",     // x86 專用目錄
            "..\\onnxruntime-x86\\",
            ".\\onnxruntime-x64\\",     // x64 專用目錄
            "..\\onnxruntime-x64\\",
        };

        HMODULE foundDll = NULL;

        // 嘗試載入
        for (int s = 0; s < 9 && !foundDll; s++) {
            std::string fullPath = std::string(searchPaths[s]) + dllName;
            foundDll = LoadLibraryA(fullPath.c_str());
        }

        // 如果沒找到，嘗試系統搜尋
        if (!foundDll) {
            foundDll = GetModuleHandleA(dllName);
        }

        if (!foundDll) {
            printf("[OnnxLoader] ONNX Runtime DLL not found (arch: %s)\n", ONNX_ARCH_STR);
            printf("[OnnxLoader] Please run setup_onnx_runtime.ps1 to download ONNX Runtime\n");
            printf("[OnnxLoader] URL: https://github.com/microsoft/onnxruntime/releases\n");
            return false;
        }

        m_dll = foundDll;

        // 載入函數指標
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
        printf("[OnnxLoader] ONNX Runtime loaded (arch: %s)\n", ONNX_ARCH_STR);
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
    const char* GetArch() const { return ONNX_ARCH_STR; }

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
    enum Arch { x86, x64 };
    HMODULE m_dll;
    bool m_loaded;
    Arch m_arch;
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
