// Ollama OCR - macOS native HTTP implementation
// Uses NSURLSession to call Ollama API

#include "ocr_bridge.h"
#include <Foundation/Foundation.h>
#include <ImageIO/ImageIO.h>
#include <CoreGraphics/CoreGraphics.h>
#include <iostream>
#include <sstream>
#include <chrono>

// Global Ollama configuration
static std::string g_ollama_model = "glm-ocr:latest";
static std::string g_ollama_url = "http://localhost:11434";

void SetOllamaConfig(const std::string& model, const std::string& url) {
    g_ollama_model = model;
    g_ollama_url = url;
}

std::string GetOllamaModel() { return g_ollama_model; }
std::string GetOllamaUrl() { return g_ollama_url; }

// Base64 encoding using macOS native API
static std::string Base64Encode(const uint8_t* data, size_t length) {
    NSData* nsData = [NSData dataWithBytes:data length:length];
    NSString* base64String = [nsData base64EncodedStringWithOptions:0];
    return std::string([base64String UTF8String]);
}

// Call Ollama API and get OCR result
// Uses /api/generate endpoint with images array
static std::string CallOllamaAPI(const std::string& base64_image, const std::string& model, const std::string& url) {
    @autoreleasepool {
        // Build JSON request using /api/generate endpoint
        // Format: {"model": "...", "prompt": "Text Recognition:", "images": ["base64"], "stream": false}
        NSString* jsonString = [NSString stringWithFormat:@"{\"model\":\"%s\",\"prompt\":\"Text Recognition:\",\"images\":[\"%s\"],\"stream\":false}",
            model.c_str(), base64_image.c_str()];

        NSData* requestBody = [jsonString dataUsingEncoding:NSUTF8StringEncoding];

        // Create URL - use /api/generate endpoint
        NSString* apiUrl = [NSString stringWithFormat:@"%s/api/generate", url.c_str()];
        NSURL* nsUrl = [NSURL URLWithString:apiUrl];

        // Create request
        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:nsUrl];
        [request setHTTPMethod:@"POST"];
        [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
        [request setHTTPBody:requestBody];
        [request setTimeoutInterval:60.0];  // 60 seconds timeout for large models

        // Use NSURLSession with semaphore for synchronous request
        NSError* error = nil;
        NSData* responseData = nil;
        NSURLResponse* response = nil;

        // Use deprecated but simpler NSURLConnection
        responseData = [NSURLConnection sendSynchronousRequest:request returningResponse:&response error:&error];

        if (error) {
            std::cerr << "Ollama API error: " << [[error localizedDescription] UTF8String] << std::endl;
            return "";
        }

        if (responseData == nil || [responseData length] == 0) {
            std::cerr << "Ollama API: empty response" << std::endl;
            return "";
        }

        // Parse JSON response
        // Response format for /api/generate: {"response":"...","done":true,...}
        NSError* jsonError = nil;
        NSDictionary* json = [NSJSONSerialization JSONObjectWithData:responseData options:0 error:&jsonError];

        if (jsonError || ![json isKindOfClass:[NSDictionary class]]) {
            std::cerr << "Ollama API: JSON parse error" << std::endl;
            NSString* responseString = [[NSString alloc] initWithData:responseData encoding:NSUTF8StringEncoding];
            std::cerr << "Response: " << [responseString UTF8String] << std::endl;
            return "";
        }

        // Extract "response" field from /api/generate response
        NSString* content = json[@"response"];
        if (content && [content isKindOfClass:[NSString class]]) {
            return std::string([content UTF8String]);
        }

        return "";
    }
}

std::vector<OCRResult> PerformOllamaOCR(
    const uint8_t* image_data,
    int width,
    int height,
    const std::string& model_name,
    const std::string& ollama_url
) {
    std::vector<OCRResult> results;

    if (image_data == NULL || width <= 0 || height <= 0) {
        return results;
    }

    // Use configured values if not specified
    std::string model = model_name.empty() ? g_ollama_model : model_name;
    std::string url = ollama_url.empty() ? g_ollama_url : ollama_url;

    std::cout << "[Ollama] Using model: " << model << " at " << url << std::endl;

    // Convert BGRA to PNG for Ollama (expects PNG/JPEG format)
    @autoreleasepool {
        // Create CGImage from BGRA
        CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        size_t image_size = width * height * 4;

        CFDataRef data = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, image_data, image_size, kCFAllocatorNull);
        CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);

        CGImageRef cgImage = CGImageCreate(
            width, height,
            8, 32, width * 4,
            colorSpace,
            kCGBitmapByteOrder32Little | kCGImageAlphaFirst,
            provider,
            NULL, false, kCGRenderingIntentDefault
        );

        CFRelease(data);
        CGDataProviderRelease(provider);
        CGColorSpaceRelease(colorSpace);

        if (cgImage == NULL) {
            std::cerr << "Error: Failed to create CGImage for Ollama" << std::endl;
            return results;
        }

        // Convert to PNG
        NSMutableData* pngData = [NSMutableData data];
        CGImageDestinationRef destination = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)pngData, kUTTypePNG, 1, NULL);
        CGImageDestinationAddImage(destination, cgImage, NULL);
        CGImageDestinationFinalize(destination);
        CFRelease(destination);
        CGImageRelease(cgImage);

        // Encode PNG to base64
        std::string base64_png = Base64Encode((const uint8_t*)[pngData bytes], [pngData length]);

        // Call Ollama API
        auto start = std::chrono::high_resolution_clock::now();
        std::string ocr_text = CallOllamaAPI(base64_png, model, url);
        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::cout << "[Ollama] API call took " << ms << " ms" << std::endl;

        if (!ocr_text.empty()) {
            // Create OCR result
            OCRResult result;
            result.text = ocr_text;
            result.confidence = 1.0f;  // VLM doesn't provide confidence, assume high
            result.x = 0;
            result.y = 0;
            result.width = width;
            result.height = height;
            results.push_back(result);
        }
    }

    return results;
}

// Translate text using Ollama API
std::string TranslateText(
    const std::string& prompt,
    const std::string& target_lang,
    const std::string& model_name,
    const std::string& ollama_url
) {
    std::cout << "[TranslateText] === START ===" << std::endl;
    std::cout << "[TranslateText] Target lang: " << target_lang << std::endl;
    std::cout << "[TranslateText] Prompt length: " << prompt.length() << std::endl;
    std::cout << "[TranslateText] Prompt preview: " << prompt.substr(0, 2000) << (prompt.length() > 2000 ? "..." : "") << std::endl;

    if (prompt.empty()) {
        std::cout << "[TranslateText] Empty prompt, returning" << std::endl;
        return "";
    }

    std::string model = model_name.empty() ? "gemma4:26b" : model_name;
    std::string url = ollama_url.empty() ? g_ollama_url : ollama_url;
    std::cout << "[TranslateText] Model: " << model << std::endl;
    std::cout << "[TranslateText] URL: " << url << std::endl;

    @autoreleasepool {
        // 使用 NSJSONSerialization 正确处理 UTF-8 编码
        NSString* modelStr = [NSString stringWithUTF8String:model.c_str()];
        NSString* promptStr = [NSString stringWithUTF8String:prompt.c_str()];

        NSDictionary* jsonDict = @{
            @"model": modelStr,
            @"prompt": promptStr,
            @"stream": @NO,
            @"think": @NO  // 禁止模型输出推理过程，只输出结果
        };

        NSError* jsonError = nil;
        NSData* requestBody = [NSJSONSerialization dataWithJSONObject:jsonDict options:0 error:&jsonError];

        if (jsonError || requestBody == nil) {
            std::cerr << "[TranslateText] Failed to create JSON: " << [[jsonError localizedDescription] UTF8String] << std::endl;
            return "";
        }

        NSString* jsonString = [[NSString alloc] initWithData:requestBody encoding:NSUTF8StringEncoding];
        std::cout << "[TranslateText] JSON request length: " << [jsonString length] << std::endl;

        NSString* apiUrl = [NSString stringWithFormat:@"%s/api/generate", url.c_str()];
        NSURL* nsUrl = [NSURL URLWithString:apiUrl];

        std::cout << "[TranslateText] Sending request to: " << [apiUrl UTF8String] << std::endl;

        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:nsUrl];
        [request setHTTPMethod:@"POST"];
        [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
        [request setHTTPBody:requestBody];
        [request setTimeoutInterval:30.0];

        NSError* error = nil;
        NSData* responseData = nil;
        NSURLResponse* response = nil;

        auto start_time = std::chrono::high_resolution_clock::now();
        responseData = [NSURLConnection sendSynchronousRequest:request returningResponse:&response error:&error];
        auto end_time = std::chrono::high_resolution_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        std::cout << "[TranslateText] Request elapsed: " << elapsed_ms << "ms" << std::endl;

        if (error) {
            std::cerr << "[TranslateText] API error: " << [[error localizedDescription] UTF8String] << std::endl;
            return "";
        }

        if (responseData == nil || [responseData length] == 0) {
            std::cerr << "[TranslateText] Empty response" << std::endl;
            return "";
        }

        std::cout << "[TranslateText] Response length: " << [responseData length] << " bytes" << std::endl;

        // Print raw response for debugging
        NSString* rawResponse = [[NSString alloc] initWithData:responseData encoding:NSUTF8StringEncoding];
        std::cout << "[TranslateText] Raw response: " << [rawResponse UTF8String] << std::endl;

        NSError* parseError = nil;
        NSDictionary* json = [NSJSONSerialization JSONObjectWithData:responseData options:0 error:&parseError];

        if (parseError) {
            std::cerr << "[TranslateText] JSON parse error: " << [[parseError localizedDescription] UTF8String] << std::endl;
            return "";
        }

        if (![json isKindOfClass:[NSDictionary class]]) {
            std::cerr << "[TranslateText] JSON not a dictionary" << std::endl;
            return "";
        }

        NSString* content = json[@"response"];
        if (content && [content isKindOfClass:[NSString class]]) {
            std::string result = std::string([content UTF8String]);
            std::cout << "[TranslateText] Result: " << result.substr(0, 100) << (result.length() > 100 ? "..." : "") << std::endl;
            std::cout << "[TranslateText] === END ===" << std::endl;
            return result;
        }

        std::cerr << "[TranslateText] No 'response' field in JSON" << std::endl;
        std::cout << "[TranslateText] === END (failed) ===" << std::endl;
        return "";
    }
}