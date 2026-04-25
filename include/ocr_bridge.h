#pragma once

#include <string>
#include <vector>
#include <cstdint>

/// OCR 结果结构
struct OCRResult {
    std::string text;           // 识别的文字
    float confidence;           // 置信度 (0-1)
    int x, y, width, height;    // 文字区域坐标
};

/// 使用 macOS Vision framework 进行 OCR
/// @param image_data 图像数据 (BGRA 格式)
/// @param width 图像宽度
/// @param height 图像高度
/// @param recognize_japanese 是否识别日文 (默认 true)
/// @return OCR 结果列表
std::vector<OCRResult> PerformVisionOCR(
    const uint8_t* image_data,
    int width,
    int height,
    bool recognize_japanese = true
);

/// 从帧缓冲数据创建 CGImage 并进行 OCR
std::vector<OCRResult> PerformOCROnBGRA(
    const std::vector<uint8_t>& bgra_data,
    int width,
    int height
);