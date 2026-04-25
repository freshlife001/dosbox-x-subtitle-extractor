// OCR Bridge - macOS Vision Framework
// Objective-C++ implementation

#include "ocr_bridge.h"
#include <CoreFoundation/CoreFoundation.h>
#include <AppKit/AppKit.h>
#include <Vision/Vision.h>

#include <iostream>
#include <vector>

std::vector<OCRResult> PerformVisionOCR(
    const uint8_t* image_data,
    int width,
    int height,
    bool recognize_japanese
) {
    std::vector<OCRResult> results;

    if (image_data == nullptr || width <= 0 || height <= 0) {
        return results;
    }

    // 创建 BGRA 图像数据
    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);

    // 创建数据提供者
    CFDataRef data = CFDataCreateWithBytesNoCopy(
        kCFAllocatorDefault,
        image_data,
        width * height * 4,  // BGRA = 4 bytes per pixel
        kCFAllocatorNull
    );

    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);

    // 创建 CGImage
    CGImageRef cgImage = CGImageCreate(
        width, height,
        8,  // bits per component
        32, // bits per pixel
        width * 4,  // bytes per row
        colorSpace,
        kCGBitmapByteOrder32Little | kCGImageAlphaFirst,  // BGRA
        provider,
        nullptr, false, kCGRenderingIntentDefault
    );

    CFRelease(data);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);

    if (cgImage == nullptr) {
        std::cerr << "Error: Failed to create CGImage" << std::endl;
        return results;
    }

    // 创建 Vision 请求
    @autoreleasepool {
        VNImageRequestHandler* requestHandler = [[VNImageRequestHandler alloc]
            initWithCGImage:cgImage
            options:@{}];

        // 创建文字识别请求
        VNRecognizeTextRequest* textRequest = [[VNRecognizeTextRequest alloc] init];

        // 设置识别级别（准确但较慢）
        textRequest.recognitionLevel = VNRequestTextRecognitionLevelAccurate;

        // 设置语言（支持日文）
        if (recognize_japanese) {
            // 使用自动语言检测或指定日语
            textRequest.recognitionLanguages = @[@"ja", @"en"];
        }

        // 使用语言校正
        textRequest.usesLanguageCorrection = true;

        NSError* error = nil;
        BOOL success = [requestHandler performRequests:@[textRequest] error:&error];

        if (success) {
            NSArray<VNRecognizedTextObservation*>* observations = textRequest.results;

            for (VNRecognizedTextObservation* obs in observations) {
                // 获取识别的文字
                NSArray<VNRecognizedText*>* topCandidates = [obs topCandidates:1];
                if (topCandidates.count > 0) {
                    VNRecognizedText* candidate = topCandidates[0];

                    OCRResult result;
                    result.text = std::string([candidate.string UTF8String]);
                    result.confidence = candidate.confidence;

                    // 获取文字区域坐标
                    CGRect boundingBox = obs.boundingBox;
                    result.x = static_cast<int>(boundingBox.origin.x * width);
                    result.y = static_cast<int>(boundingBox.origin.y * height);
                    result.width = static_cast<int>(boundingBox.size.width * width);
                    result.height = static_cast<int>(boundingBox.size.height * height);

                    results.push_back(result);
                }
            }
        } else if (error) {
            std::cerr << "OCR Error: " << [[error localizedDescription] UTF8String] << std::endl;
        }
    }

    CGImageRelease(cgImage);

    return results;
}

std::vector<OCRResult> PerformOCROnBGRA(
    const std::vector<uint8_t>& bgra_data,
    int width,
    int height
) {
    return PerformVisionOCR(bgra_data.data(), width, height, true);
}