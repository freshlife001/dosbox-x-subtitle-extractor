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

/// OCR 类型
enum class OCRType {
#ifdef __APPLE__
    Vision,     // macOS Vision framework (only on macOS)
#endif
    Ollama,     // Ollama VLM (cross-platform)
#ifdef USE_PADDLEOCR
    PaddleOCR   // PaddleOCR v5
#endif
};

#ifdef __APPLE__
/// 使用 macOS Vision framework 进行 OCR (macOS only)
std::vector<OCRResult> PerformVisionOCR(
    const uint8_t* image_data,
    int width,
    int height,
    bool recognize_japanese = true
);
#endif

/// 使用 Ollama VLM 进行 OCR (cross-platform)
/// @param model_name Ollama 模型名称（如 "glm-ocr:latest", "llava"）
/// @param ollama_url Ollama API 地址（默认 "http://localhost:11434"）
std::vector<OCRResult> PerformOllamaOCR(
    const uint8_t* image_data,
    int width,
    int height,
    const std::string& model_name = "",
    const std::string& ollama_url = ""
);

#ifdef USE_PADDLEOCR
/// 使用 PaddleOCR 进行 OCR
std::vector<OCRResult> PerformPaddleOCR(
    const uint8_t* image_data,
    int width,
    int height,
    const std::string& det_model_path = "",
    const std::string& rec_model_path = ""
);
#endif

/// 从帧缓冲数据创建图像并进行 OCR
/// @param ocr_type OCR 类型
std::vector<OCRResult> PerformOCR(
    const std::vector<uint8_t>& bgra_data,
    int width,
    int height,
    OCRType ocr_type = OCRType::Ollama
);

/// 设置 Ollama 配置
void SetOllamaConfig(const std::string& model, const std::string& url);
std::string GetOllamaModel();
std::string GetOllamaUrl();

/// 使用 Ollama 进行翻译
/// @param prompt 完整的翻译prompt（包含原文、上下文、要求等）
/// @param target_lang 目标语言（如 "中文", "English", "日本語"）
/// @param model_name Ollama 模型名称（默认使用配置的模型）
/// @param ollama_url Ollama API 地址（默认使用配置的地址）
std::string TranslateText(
    const std::string& prompt,
    const std::string& target_lang,
    const std::string& model_name = "",
    const std::string& ollama_url = ""
);

#ifdef USE_PADDLEOCR
/// 设置 OCR 模型路径（用于 PaddleOCR）
void SetOCRModelPaths(const std::string& det_path, const std::string& rec_path);

/// 获取 OCR 模型路径
std::string GetDetModelPath();
std::string GetRecModelPath();
#endif