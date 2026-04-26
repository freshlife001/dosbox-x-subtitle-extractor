// Test OCR - Vision vs Ollama Performance Comparison
#include "ocr_bridge.h"
#include <opencv2/opencv.hpp>
#include <opencv2/freetype.hpp>
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <algorithm>

// Font path for Chinese/Japanese text
static const char* FONT_PATH = "/System/Library/Fonts/STHeiti Light.ttc";

// Timer helper
class Timer {
public:
    Timer(const std::string& name) : name_(name), start_(std::chrono::high_resolution_clock::now()) {}
    ~Timer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_);
        std::cout << "[" << name_ << "] Time: " << duration.count() << " ms" << std::endl;
    }
    long elapsed_ms() {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
    }
private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_;
};

// Create English test image
cv::Mat createEnglishTestImage() {
    cv::Mat image(200, 500, CV_8UC3, cv::Scalar(255, 255, 255));

    cv::putText(image, "Hello World", cv::Point(20, 50),
                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 0), 2);
    cv::putText(image, "OCR Test 12345", cv::Point(20, 100),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 0), 2);
    cv::putText(image, "The quick brown fox", cv::Point(20, 150),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 0), 2);

    return image;
}

// Create Japanese test image with real Japanese characters
cv::Mat createJapaneseTestImage() {
    cv::Mat image(480, 640, CV_8UC3, cv::Scalar(40, 40, 60));

    cv::rectangle(image, cv::Point(20, 350), cv::Point(620, 460), cv::Scalar(30, 30, 50), -1);
    cv::rectangle(image, cv::Point(20, 350), cv::Point(620, 460), cv::Scalar(200, 200, 220), 2);

    cv::Ptr<cv::freetype::FreeType2> ft2 = cv::freetype::createFreeType2();
    try {
        ft2->loadFontData(FONT_PATH, 0);
        ft2->putText(image, "こんにちは世界", cv::Point(40, 400), 28, cv::Scalar(255, 255, 255), -1, cv::LINE_AA, true);
        ft2->putText(image, "ようこそ！", cv::Point(40, 440), 24, cv::Scalar(255, 255, 255), -1, cv::LINE_AA, true);
    } catch (const cv::Exception& e) {
        std::cerr << "FreeType error: " << e.what() << std::endl;
        cv::putText(image, "Japanese Text", cv::Point(40, 390),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
    }

    return image;
}

// Create Chinese test image with real Chinese characters
cv::Mat createChineseTestImage() {
    cv::Mat image(480, 640, CV_8UC3, cv::Scalar(255, 255, 255));

    cv::Ptr<cv::freetype::FreeType2> ft2 = cv::freetype::createFreeType2();
    try {
        ft2->loadFontData(FONT_PATH, 0);
        ft2->putText(image, "你好世界", cv::Point(20, 60), 40, cv::Scalar(0, 0, 0), -1, cv::LINE_AA, true);
        ft2->putText(image, "测试文字识别", cv::Point(20, 120), 35, cv::Scalar(0, 0, 0), -1, cv::LINE_AA, true);
        ft2->putText(image, "欢迎使用OCR", cv::Point(20, 180), 30, cv::Scalar(0, 0, 0), -1, cv::LINE_AA, true);
    } catch (const cv::Exception& e) {
        std::cerr << "FreeType error: " << e.what() << std::endl;
        cv::putText(image, "Chinese Text", cv::Point(20, 50),
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 0), 2);
    }

    return image;
}

// Create game screenshot simulation
cv::Mat createGameScreenshot() {
    cv::Mat image(480, 640, CV_8UC3, cv::Scalar(20, 30, 50));

    cv::rectangle(image, cv::Point(10, 10), cv::Point(200, 60), cv::Scalar(60, 70, 90), -1);
    cv::putText(image, "HP: 100", cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);

    cv::rectangle(image, cv::Point(50, 300), cv::Point(590, 450), cv::Scalar(40, 50, 70), -1);
    cv::rectangle(image, cv::Point(50, 300), cv::Point(590, 450), cv::Scalar(150, 160, 180), 2);

    cv::putText(image, "Character: Hello!", cv::Point(70, 340),
                cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 2);
    cv::putText(image, "Welcome to the game!", cv::Point(70, 380),
                cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 2);
    cv::putText(image, "Press START to continue", cv::Point(70, 420),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200, 200, 200), 1);

    return image;
}

// Get OCR type name
std::string getOCRTypeName(OCRType ocr_type) {
    switch (ocr_type) {
        case OCRType::Vision: return "Vision";
        case OCRType::Ollama: return "Ollama";
        default: return "Unknown";
    }
}

// Test OCR on image
void testOCR(const cv::Mat& image, const std::string& test_name, OCRType ocr_type) {
    std::string type_name = getOCRTypeName(ocr_type);

    cv::Mat bgra;
    cv::cvtColor(image, bgra, cv::COLOR_BGR2BGRA);
    std::vector<uint8_t> image_data(bgra.data, bgra.data + bgra.total() * bgra.elemSize());

    std::cout << "\n=== " << test_name << " (" << type_name << ") ===" << std::endl;

    Timer timer(test_name + " - " + type_name);
    auto results = PerformOCR(image_data, image.cols, image.rows, ocr_type);

    std::cout << "Found " << results.size() << " text regions" << std::endl;
    for (const auto& result : results) {
        std::cout << "  \"" << result.text << "\""
                  << " | Conf: " << std::fixed << std::setprecision(2) << result.confidence
                  << " | (" << result.x << "," << result.y << ")"
                  << std::endl;
    }
}

// Performance test (Vision OCR only - Ollama is too slow for iterations)
void testVisionPerformance(const cv::Mat& image, int iterations = 5) {
    cv::Mat bgra;
    cv::cvtColor(image, bgra, cv::COLOR_BGR2BGRA);
    std::vector<uint8_t> image_data(bgra.data, bgra.data + bgra.total() * bgra.elemSize());

    std::cout << "\n=== Vision OCR Performance (" << iterations << " iterations) ===" << std::endl;

    std::vector<long> times;
    for (int i = 0; i < iterations; i++) {
        Timer timer("Iteration " + std::to_string(i+1));
        auto results = PerformOCR(image_data, image.cols, image.rows, OCRType::Vision);
        times.push_back(timer.elapsed_ms());
    }

    long total = 0, min_t = times[0], max_t = times[0];
    for (long t : times) {
        total += t;
        min_t = std::min(min_t, t);
        max_t = std::max(max_t, t);
    }

    std::cout << "Min: " << min_t << "ms | Max: " << max_t << "ms | Avg: " << (total/iterations) << "ms" << std::endl;
    std::cout << "FPS: " << std::fixed << std::setprecision(2) << (1000.0 * iterations / total) << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "========================================" << std::endl;
    std::cout << "  OCR Performance Comparison Test" << std::endl;
    std::cout << "  Vision vs Ollama (glm-ocr)" << std::endl;
    std::cout << "========================================" << std::endl;

    // Test 1: English text
    std::cout << "\n[Test 1: English Text]" << std::endl;
    cv::Mat english_img = createEnglishTestImage();
    cv::imwrite("test_english.png", english_img);

    testOCR(english_img, "English", OCRType::Vision);
    testOCR(english_img, "English", OCRType::Ollama);

    // Test 2: Japanese-style game screenshot
    std::cout << "\n[Test 2: Japanese Game Style]" << std::endl;
    cv::Mat japanese_img = createJapaneseTestImage();
    cv::imwrite("test_japanese.png", japanese_img);

    testOCR(japanese_img, "Japanese", OCRType::Vision);
    testOCR(japanese_img, "Japanese", OCRType::Ollama);

    // Test 3: Chinese-style
    std::cout << "\n[Test 3: Chinese Text]" << std::endl;
    cv::Mat chinese_img = createChineseTestImage();
    cv::imwrite("test_chinese.png", chinese_img);

    testOCR(chinese_img, "Chinese", OCRType::Vision);
    testOCR(chinese_img, "Chinese", OCRType::Ollama);

    // Test 4: Full game screenshot
    std::cout << "\n[Test 4: Game Screenshot]" << std::endl;
    cv::Mat game_img = createGameScreenshot();
    cv::imwrite("test_game.png", game_img);

    testOCR(game_img, "Game", OCRType::Vision);
    testOCR(game_img, "Game", OCRType::Ollama);

    // Test 5: Performance comparison (Vision only)
    std::cout << "\n[Test 5: Vision OCR Performance]" << std::endl;
    testVisionPerformance(english_img, 5);
    testVisionPerformance(game_img, 5);

    // Summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Test Complete" << std::endl;
    std::cout << "  Saved images: test_english.png, test_japanese.png," << std::endl;
    std::cout << "                test_chinese.png, test_game.png" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}