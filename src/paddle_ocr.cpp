// PaddleOCR C++ Inference - Based on PaddleOCR deploy/cpp_infer
// Using Paddle Inference API for PP-OCRv5
// Reference: https://github.com/PaddlePaddle/PaddleOCR/tree/main/deploy/cpp_infer

#include "ocr_bridge.h"
#include "paddle_inference_api.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <vector>
#include <memory>
#include <string>

// Global model paths (PP-OCRv5 server models - Paddle format with .pdmodel/.pdiparams)
static std::string g_det_model_dir = "models/PP-OCRv5_server_det_infer";
static std::string g_rec_model_dir = "models/PP-OCRv5_server_rec_infer";
static std::string g_cls_model_dir = "models/ch_ppocr_mobile_v2.0_cls_infer";
static std::string g_dict_path = "models/ppocr_keys_v1.txt";  // PP-OCRv5 character dict (extracted from inference.yml)

// Global Paddle Inference predictors
static std::shared_ptr<paddle_infer::Predictor> g_det_predictor;
static std::shared_ptr<paddle_infer::Predictor> g_rec_predictor;
static std::shared_ptr<paddle_infer::Predictor> g_cls_predictor;

// Input/output tensor handles
static std::unique_ptr<paddle_infer::Tensor> g_det_input_tensor;
static std::unique_ptr<paddle_infer::Tensor> g_det_output_tensor;
static std::unique_ptr<paddle_infer::Tensor> g_rec_input_tensor;
static std::unique_ptr<paddle_infer::Tensor> g_rec_output_tensor;

// Character dictionary for recognition
static std::vector<std::string> g_char_dict;

void SetOCRModelPaths(const std::string& det_path, const std::string& rec_path) {
    g_det_model_dir = det_path;
    g_rec_model_dir = rec_path;
}

std::string GetDetModelPath() { return g_det_model_dir; }
std::string GetRecModelPath() { return g_rec_model_dir; }

// Load character dictionary
static bool LoadCharDict(const std::string& dict_path) {
    std::ifstream file(dict_path);
    if (!file.is_open()) {
        std::cerr << "Warning: Failed to load char dict: " << dict_path << std::endl;
        return false;
    }

    g_char_dict.clear();
    std::string line;
    while (std::getline(file, line)) {
        g_char_dict.push_back(line);
    }

    return true;
}

// Create Paddle Inference predictor (following PaddleOCR static_infer.cc)
static std::shared_ptr<paddle_infer::Predictor> CreatePredictor(
    const std::string& model_dir, int cpu_threads = 4) {

    // Model paths: support both .pdmodel (traditional) and .json (PaddleX new format)
    std::string model_file_json = model_dir + "/inference.json";
    std::string model_file_pdmodel = model_dir + "/inference.pdmodel";
    std::string params_file = model_dir + "/inference.pdiparams";

    // Check which model format exists
    std::string model_file;
    std::ifstream json_file(model_file_json);
    std::ifstream pdmodel_file(model_file_pdmodel);

    if (json_file.good()) {
        model_file = model_file_json;
    } else if (pdmodel_file.good()) {
        model_file = model_file_pdmodel;
    } else {
        std::cerr << "Model files not found in: " << model_dir << std::endl;
        return nullptr;
    }

    // Check params file
    std::ifstream pf(params_file);
    if (!pf.good()) {
        std::cerr << "Params file not found: " << params_file << std::endl;
        return nullptr;
    }

    paddle_infer::Config config;
    config.SetModel(model_file, params_file);

    // CPU mode
    config.DisableGpu();
    config.SetCpuMathLibraryNumThreads(cpu_threads);

    // Check if MKLDNN is available (Intel CPU)
    // For Apple Silicon, we disable MKLDNN
    #ifdef __APPLE__
    config.DisableMKLDNN();
    #else
    config.EnableMKLDNN();
    config.SetMkldnnCacheCapacity(10);
    #endif

    // Enable optimizations
    config.EnableMemoryOptim();
    config.EnableNewIR(true);
    config.EnableNewExecutor();
    config.SetOptimizationLevel(3);
    config.DisableGlogInfo();

    auto predictor = paddle_infer::CreatePredictor(config);
    return predictor;
}

// Initialize Paddle Inference
static bool InitializePaddleInference() {
    if (g_det_predictor && g_rec_predictor) return true;

    try {
        // Create detection predictor
        g_det_predictor = CreatePredictor(g_det_model_dir, 4);
        if (!g_det_predictor) {
            std::cerr << "Failed to create detection predictor" << std::endl;
            return false;
        }

        // Get input/output handles
        auto det_input_names = g_det_predictor->GetInputNames();
        g_det_input_tensor = g_det_predictor->GetInputHandle(det_input_names[0]);

        auto det_output_names = g_det_predictor->GetOutputNames();
        g_det_output_tensor = g_det_predictor->GetOutputHandle(det_output_names[0]);

        // Create recognition predictor
        g_rec_predictor = CreatePredictor(g_rec_model_dir, 4);
        if (!g_rec_predictor) {
            std::cerr << "Failed to create recognition predictor" << std::endl;
            return false;
        }

        auto rec_input_names = g_rec_predictor->GetInputNames();
        g_rec_input_tensor = g_rec_predictor->GetInputHandle(rec_input_names[0]);

        auto rec_output_names = g_rec_predictor->GetOutputNames();
        g_rec_output_tensor = g_rec_predictor->GetOutputHandle(rec_output_names[0]);

        // Load dictionary
        LoadCharDict(g_dict_path);

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Paddle Inference init error: " << e.what() << std::endl;
        return false;
    }
}

// BGRA to BGR (OpenCV format)
static cv::Mat BGRAtoBGR(const uint8_t* bgra_data, int width, int height) {
    cv::Mat bgra(height, width, CV_8UC4, const_cast<uint8_t*>(bgra_data));
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

// Detection preprocessing - resize and normalize
static std::vector<float> DetPreprocess(const cv::Mat& image, std::vector<int>& input_shape) {
    int h = image.rows;
    int w = image.cols;

    // Target size: 960 (from inference config)
    int target_size = 960;
    float scale = static_cast<float>(target_size) / std::max(h, w);
    int new_h = static_cast<int>(h * scale);
    int new_w = static_cast<int>(w * scale);

    // Ensure divisible by 32
    new_h = (new_h / 32) * 32;
    new_w = (new_w / 32) * 32;
    new_h = std::max(new_h, 32);
    new_w = std::max(new_w, 32);

    cv::Mat resized;
    cv::resize(image, resized, cv::Size(new_w, new_h));

    // Normalize with ImageNet values
    cv::Mat normalized;
    resized.convertTo(normalized, CV_32F, 1.0 / 255.0);

    std::vector<cv::Mat> channels(3);
    cv::split(normalized, channels);
    channels[0] = (channels[0] - 0.485) / 0.229;  // B
    channels[1] = (channels[1] - 0.456) / 0.224;  // G
    channels[2] = (channels[2] - 0.406) / 0.225;  // R
    cv::merge(channels, normalized);

    // HWC to CHW
    std::vector<float> input_data(3 * new_h * new_w);
    for (int c = 0; c < 3; c++) {
        for (int i = 0; i < new_h * new_w; i++) {
            input_data[c * new_h * new_w + i] = normalized.at<cv::Vec3f>(i / new_w, i % new_w)[c];
        }
    }

    input_shape = {1, 3, new_h, new_w};
    return input_data;
}

// Detection postprocess - DB algorithm
static std::vector<cv::RotatedRect> DetPostprocess(const std::vector<float>& output_data,
                                                    const std::vector<int>& output_shape,
                                                    int orig_h, int orig_w,
                                                    float thresh = 0.3,
                                                    float box_thresh = 0.6,
                                                    float unclip_ratio = 1.5) {
    std::vector<cv::RotatedRect> boxes;

    int out_h = output_shape[2];
    int out_w = output_shape[3];

    // Create bitmap
    cv::Mat bitmap(out_h, out_w, CV_32F);
    memcpy(bitmap.ptr<float>(), output_data.data(), output_data.size() * sizeof(float));

    // Threshold
    cv::Mat binary;
    cv::threshold(bitmap, binary, thresh, 255, cv::THRESH_BINARY);
    binary.convertTo(binary, CV_8U);

    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    float scale_x = static_cast<float>(orig_w) / out_w;
    float scale_y = static_cast<float>(orig_h) / out_h;

    for (const auto& contour : contours) {
        if (contour.size() < 4) continue;

        cv::RotatedRect rect = cv::minAreaRect(contour);

        // Filter small regions
        float area = rect.size.width * rect.size.height;
        if (area < 100) continue;

        // Check score
        int cx = static_cast<int>(rect.center.x);
        int cy = static_cast<int>(rect.center.y);
        cx = std::max(0, std::min(cx, out_w - 1));
        cy = std::max(0, std::min(cy, out_h - 1));
        float score = bitmap.at<float>(cy, cx);
        if (score < box_thresh) continue;

        // Unclip
        rect.size.width *= unclip_ratio;
        rect.size.height *= unclip_ratio;

        // Scale back
        rect.center.x *= scale_x;
        rect.center.y *= scale_y;
        rect.size.width *= scale_x;
        rect.size.height *= scale_y;

        boxes.push_back(rect);
    }

    return boxes;
}

// Recognition preprocessing
static std::vector<float> RecPreprocess(const cv::Mat& image, std::vector<int>& input_shape) {
    int h = image.rows;
    int w = image.cols;

    // Target height: 48
    int target_height = 48;
    float scale = static_cast<float>(target_height) / h;
    int new_w = static_cast<int>(w * scale);

    // Limit width
    new_w = std::max(new_w, 10);
    new_w = std::min(new_w, 320);

    cv::Mat resized;
    cv::resize(image, resized, cv::Size(new_w, target_height));

    // Normalize
    cv::Mat normalized;
    resized.convertTo(normalized, CV_32F, 1.0 / 255.0);

    // HWC to CHW
    std::vector<float> input_data(3 * target_height * new_w);
    for (int c = 0; c < 3; c++) {
        for (int i = 0; i < target_height * new_w; i++) {
            input_data[c * target_height * new_w + i] = normalized.at<cv::Vec3f>(i / new_w, i % new_w)[c];
        }
    }

    input_shape = {1, 3, target_height, new_w};
    return input_data;
}

// Recognition postprocess - CTC decode
static std::pair<std::string, float> RecPostprocess(const std::vector<float>& output_data,
                                                     const std::vector<int>& output_shape) {
    std::string text;
    float total_conf = 0.0f;
    int char_count = 0;

    int text_len = output_shape[1];
    int dict_size = output_shape[2];  // Model output dict size (includes blank at index 0)

    int last_idx = 0;
    for (int t = 0; t < text_len; t++) {
        // Find max probability
        int max_idx = 0;
        float max_prob = output_data[t * dict_size];
        for (int c = 1; c < dict_size; c++) {
            float prob = output_data[t * dict_size + c];
            if (prob > max_prob) {
                max_prob = prob;
                max_idx = c;
            }
        }

        // CTC decode: skip blank (index 0) and repeated characters
        if (max_idx == 0 || (t > 0 && max_idx == last_idx)) {
            last_idx = max_idx;
            continue;
        }

        // Decode: model output index 1 maps to dict[0], so use max_idx - 1
        int dict_idx = max_idx - 1;
        if (dict_idx >= 0 && dict_idx < static_cast<int>(g_char_dict.size())) {
            text += g_char_dict[dict_idx];
        }

        total_conf += max_prob;
        char_count++;
        last_idx = max_idx;
    }

    float avg_conf = char_count > 0 ? total_conf / char_count : 0.0f;
    return {text, avg_conf};
}

// Rotate and crop image for recognition
static cv::Mat GetRotateCropImage(const cv::Mat& image, const cv::RotatedRect& rect) {
    cv::Mat M = cv::getRotationMatrix2D(rect.center, rect.angle, 1.0);
    cv::Mat rotated;
    cv::warpAffine(image, rotated, M, image.size());

    cv::Mat cropped;
    cv::getRectSubPix(rotated, rect.size, rect.center, cropped);

    // Make width > height
    if (cropped.rows > cropped.cols) {
        cv::transpose(cropped, cropped);
        cv::flip(cropped, cropped, 0);
    }

    return cropped;
}

std::vector<OCRResult> PerformPaddleOCR(
    const uint8_t* image_data,
    int width,
    int height,
    const std::string& det_model_path,
    const std::string& rec_model_path
) {
    std::vector<OCRResult> results;

    if (!det_model_path.empty()) g_det_model_dir = det_model_path;
    if (!rec_model_path.empty()) g_rec_model_dir = rec_model_path;

    if (!InitializePaddleInference()) {
        std::cerr << "Failed to initialize Paddle Inference" << std::endl;
        return results;
    }

    try {
        // Convert BGRA to BGR
        cv::Mat image = BGRAtoBGR(image_data, width, height);

        // Detection preprocessing
        std::vector<int> det_input_shape;
        std::vector<float> det_input = DetPreprocess(image, det_input_shape);

        // Set input tensor
        g_det_input_tensor->Reshape(det_input_shape);
        g_det_input_tensor->CopyFromCpu<float>(det_input.data());

        // Run detection
        g_det_predictor->Run();

        // Get output
        std::vector<int> det_output_shape = g_det_output_tensor->shape();
        size_t det_output_size = 1;
        for (int dim : det_output_shape) det_output_size *= dim;
        std::vector<float> det_output(det_output_size);
        g_det_output_tensor->CopyToCpu(det_output.data());

        // Postprocess detection
        std::vector<cv::RotatedRect> boxes = DetPostprocess(
            det_output, det_output_shape, height, width
        );

        // Recognition for each box
        for (auto& box : boxes) {
            // Normalize RotatedRect: ensure width >= height, adjust angle
            // This fixes boxes detected with swapped width/height
            if (box.size.width < box.size.height) {
                std::swap(box.size.width, box.size.height);
                box.angle += 90.0;
                if (box.angle >= 180.0) box.angle -= 180.0;
            }

            cv::Mat crop_img = GetRotateCropImage(image, box);

            if (crop_img.cols < 10 || crop_img.rows < 10) continue;

            // Recognition preprocessing
            std::vector<int> rec_input_shape;
            std::vector<float> rec_input = RecPreprocess(crop_img, rec_input_shape);

            // Set input
            g_rec_input_tensor->Reshape(rec_input_shape);
            g_rec_input_tensor->CopyFromCpu<float>(rec_input.data());

            // Run recognition
            g_rec_predictor->Run();

            // Get output
            std::vector<int> rec_output_shape = g_rec_output_tensor->shape();
            size_t rec_output_size = 1;
            for (int dim : rec_output_shape) rec_output_size *= dim;
            std::vector<float> rec_output(rec_output_size);
            g_rec_output_tensor->CopyToCpu(rec_output.data());

            // Postprocess recognition
            auto [text, conf] = RecPostprocess(rec_output, rec_output_shape);

            if (!text.empty() && conf > 0.5f) {
                OCRResult result;
                result.text = text;
                result.confidence = conf;
                // Use boundingRect for correct coordinates
                cv::Rect bbox = box.boundingRect();
                result.x = std::max(0, bbox.x);
                result.y = std::max(0, bbox.y);
                result.width = std::min(bbox.width, width - result.x);
                result.height = std::min(bbox.height, height - result.y);
                results.push_back(result);
            }
        }

        // Sort by y
        std::sort(results.begin(), results.end(),
            [](const OCRResult& a, const OCRResult& b) {
                return a.y < b.y;
            });

    } catch (const std::exception& e) {
        std::cerr << "PaddleOCR error: " << e.what() << std::endl;
    }

    return results;
}

std::vector<OCRResult> PerformOCR(
    const std::vector<uint8_t>& bgra_data,
    int width,
    int height,
    OCRType ocr_type
) {
    if (ocr_type == OCRType::PaddleOCR) {
        return PerformPaddleOCR(bgra_data.data(), width, height);
    } else {
        return PerformVisionOCR(bgra_data.data(), width, height, true);
    }
}