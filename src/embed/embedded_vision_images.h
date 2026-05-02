// embedded_vision_images.h - 嵌入式視覺圖片（預留，未使用）
#pragma once
namespace EmbeddedVision {
    struct EmbeddedImg { const char* name; const unsigned char* data; int size; };
    static const EmbeddedImg* g_VisionImages = nullptr;
}
