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