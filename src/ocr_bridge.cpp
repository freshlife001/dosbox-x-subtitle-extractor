// OCR Bridge - Unified OCR entry point
// Handles platform-specific OCR type selection

#include "ocr_bridge.h"
#include <iostream>

// Forward declarations
#ifdef __APPLE__
extern std::vector<OCRResult> PerformVisionOCR(
    const uint8_t* image_data,
    int width,
    int height,
    bool recognize_japanese
);
#endif

extern std::vector<OCRResult> PerformOllamaOCR(
    const uint8_t* image_data,
    int width,
    int height,
    const std::string& model_name,
    const std::string& ollama_url
);

#ifdef USE_PADDLEOCR
extern std::vector<OCRResult> PerformPaddleOCR(
    const uint8_t* image_data,
    int width,
    int height,
    const std::string& det_model_path,
    const std::string& rec_model_path
);
#endif

std::vector<OCRResult> PerformOCR(
    const std::vector<uint8_t>& bgra_data,
    int width,
    int height,
    OCRType ocr_type
) {
    if (bgra_data.empty() || width <= 0 || height <= 0) {
        return {};
    }

    // Handle OCR type based on platform availability
#ifdef __APPLE__
    if (ocr_type == OCRType::Vision) {
        return PerformVisionOCR(bgra_data.data(), width, height, true);
    }
#endif

#ifdef USE_PADDLEOCR
    if (ocr_type == OCRType::PaddleOCR) {
        return PerformPaddleOCR(bgra_data.data(), width, height);
    }
#endif

    // Default: Ollama OCR (cross-platform)
    if (ocr_type == OCRType::Ollama) {
        return PerformOllamaOCR(bgra_data.data(), width, height);
    }

    // Fallback to Ollama if requested type not available on this platform
#ifdef __APPLE__
    if (ocr_type == OCRType::Vision) {
        std::cerr << "[OCR] Vision OCR not available, falling back to Ollama" << std::endl;
    }
#endif

#ifdef USE_PADDLEOCR
    if (ocr_type == OCRType::PaddleOCR) {
        std::cerr << "[OCR] PaddleOCR not enabled, falling back to Ollama" << std::endl;
    }
#endif

    return PerformOllamaOCR(bgra_data.data(), width, height);
}