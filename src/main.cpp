#include "../include/subtitle_extractor.h"
#include "../include/subtitle_format.h"
#include "../include/gamelink_interface.h"
#include "../include/ocr_bridge.h"
#include "../include/web_server.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <csignal>
#include <map>

// 全局运行标志（用于 Ctrl+C 停止）
std::atomic<bool> g_running(true);

// 全局 GameLink 实例（用于输入回调）
GameLink::GameLinkInterface* g_gameLink = nullptr;

void SignalHandler(int signal) {
    g_running = false;
    std::cout << "\nStopping..." << std::endl;
}

struct Options {
    std::string game_config;
    std::string output_file;
    bool display_subtitles = false;
    std::string encoding_from = "cp437";
    std::string encoding_to = "utf-8";
    uint32_t timeout_ms = 16;
    bool verbose = false;
    bool help = false;

    // Web 远程控制参数
    bool web_remote = false;
    int web_port = 9091;           // Web 服务器端口

    // OCR 模式参数
    bool ocr_mode = false;
    uint32_t ocr_interval = 1000;    // OCR 间隔 (ms)
    bool ocr_continuous = false;     // 持续 OCR 监控
    float ocr_min_confidence = 0.5;  // 最小置信度

    // Scan 模式参数
    bool scan_mode = false;
    uint32_t scan_start = 0;         // 默认从 0 开始
    uint32_t scan_end = 0;           // 默认到 RAM_SIZE
    uint32_t scan_interval = 1000;   // 扫描间隔 (ms)
    size_t scan_min_length = 4;      // 最小字符串长度
    bool scan_continuous = false;    // 是否持续扫描（监控变化）
    bool scan_debug = false;         // 显示调试信息
    std::string scan_charset = "japanese"; // 字符集过滤: "japanese", "ascii", "all"
    std::string scan_text;          // 指定要搜索的文本（可选）
    uint32_t scan_offset = 0;       // 内存偏移量 (load address)
};

void PrintUsage(const char* program) {
    std::cout << "DOSBox-X Subtitle Extractor\n\n";
    std::cout << "Usage: " << program << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --game <name>              Game configuration file\n";
    std::cout << "  --config <path>            Custom config file path\n";
    std::cout << "  --output <path>            Export subtitles to file (srt/ass/vtt/json)\n";
    std::cout << "  --display                  Display subtitles in real-time\n";
    std::cout << "  --encoding <from:to>       Encoding conversion (default: cp437:utf-8)\n";
    std::cout << "  --timeout <ms>             Game Link timeout (default: 16)\n";
    std::cout << "  --verbose                  Verbose output\n";
    std::cout << "  --help, -h                 Show this help message\n";
    std::cout << "\nWeb Remote Control:\n";
    std::cout << "  --web                      Start web remote control server\n";
    std::cout << "  --web-port <port>          Web server port (default: 9091)\n";
    std::cout << "\nOCR Mode (Vision OCR):\n";
    std::cout << "  --ocr                      Enable OCR mode using macOS Vision framework\n";
    std::cout << "  --ocr-interval <ms>        OCR interval (default: 1000)\n";
    std::cout << "  --ocr-continuous           Continuously monitor screen for text changes\n";
    std::cout << "  --ocr-min-confidence <f>   Minimum confidence threshold (default: 0.5)\n";
    std::cout << "\nScan Mode (experimental):\n";
    std::cout << "  --scan                     Enable memory scan mode\n";
    std::cout << "  --scan-start <addr>        Scan start address (hex, default: 0)\n";
    std::cout << "  --scan-end <addr>          Scan end address (hex, default: RAM size)\n";
    std::cout << "  --scan-interval <ms>       Scan interval (default: 1000)\n";
    std::cout << "  --scan-min-len <n>         Minimum string length (default: 4)\n";
    std::cout << "  --scan-continuous          Continuously monitor for changes\n";
    std::cout << "  --scan-charset <type>      Character set filter: japanese, ascii, all (default: japanese)\n";
    std::cout << "  --scan-text <text>         Search for specific text pattern (Shift-JIS or UTF-8)\n";
    std::cout << "  --scan-offset <addr>       Memory offset/load address (hex, default: 0)\n";
    std::cout << "  --scan-debug               Show raw data for debugging\n";
    std::cout << std::endl;
}

bool ParseArguments(int argc, char* argv[], Options& options) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--game" && i + 1 < argc) {
            options.game_config = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            options.game_config = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            options.output_file = argv[++i];
        } else if (arg == "--display") {
            options.display_subtitles = true;
        } else if (arg == "--encoding" && i + 1 < argc) {
            std::string enc = argv[++i];
            size_t colon = enc.find(':');
            if (colon != std::string::npos) {
                options.encoding_from = enc.substr(0, colon);
                options.encoding_to = enc.substr(colon + 1);
            }
        } else if (arg == "--timeout" && i + 1 < argc) {
            options.timeout_ms = std::stoul(argv[++i]);
        } else if (arg == "--verbose") {
            options.verbose = true;
        } else if (arg == "--scan") {
            options.scan_mode = true;
        } else if (arg == "--scan-start" && i + 1 < argc) {
            options.scan_start = std::stoul(argv[++i], nullptr, 16);
        } else if (arg == "--scan-end" && i + 1 < argc) {
            options.scan_end = std::stoul(argv[++i], nullptr, 16);
        } else if (arg == "--scan-interval" && i + 1 < argc) {
            options.scan_interval = std::stoul(argv[++i]);
        } else if (arg == "--scan-min-len" && i + 1 < argc) {
            options.scan_min_length = std::stoul(argv[++i]);
        } else if (arg == "--scan-continuous") {
            options.scan_continuous = true;
        } else if (arg == "--scan-debug") {
            options.scan_debug = true;
        } else if (arg == "--scan-charset" && i + 1 < argc) {
            options.scan_charset = argv[++i];
        } else if (arg == "--scan-text" && i + 1 < argc) {
            options.scan_text = argv[++i];
        } else if (arg == "--scan-offset" && i + 1 < argc) {
            options.scan_offset = std::stoul(argv[++i], nullptr, 16);
        } else if (arg == "--ocr") {
            options.ocr_mode = true;
        } else if (arg == "--ocr-interval" && i + 1 < argc) {
            options.ocr_interval = std::stoul(argv[++i]);
        } else if (arg == "--ocr-continuous") {
            options.ocr_continuous = true;
        } else if (arg == "--ocr-min-confidence" && i + 1 < argc) {
            options.ocr_min_confidence = std::stof(argv[++i]);
        } else if (arg == "--web") {
            options.web_remote = true;
        } else if (arg == "--web-port" && i + 1 < argc) {
            options.web_port = std::stoi(argv[++i]);
        }
    }

    return !options.help;
}

Subtitle::CodePage StringToCodePage(const std::string& name) {
    if (name == "cp437") return Subtitle::CodePage::CP437;
    if (name == "cp850") return Subtitle::CodePage::CP850;
    if (name == "cp852") return Subtitle::CodePage::CP852;
    if (name == "cp866") return Subtitle::CodePage::CP866;
    if (name == "cp932") return Subtitle::CodePage::CP932;
    if (name == "cp936") return Subtitle::CodePage::CP936;
    if (name == "cp949") return Subtitle::CodePage::CP949;
    if (name == "utf-8" || name == "utf8") return Subtitle::CodePage::UTF8;
    return Subtitle::CodePage::CP437;
}

// ============================================================================
// Web 远程控制模式
// ============================================================================

int RunWebRemoteMode(const Options& options) {
    GameLink::GameLinkInterface gameLink;
    g_gameLink = &gameLink;

    std::cout << "Initializing Game Link for Web Remote Control..." << std::endl;

    if (!gameLink.Initialize()) {
        std::cerr << "Error: Failed to initialize Game Link. Is DOSBox-X running?" << std::endl;
        return 1;
    }

    std::cout << "Game: " << gameLink.GetGameName() << std::endl;
    std::cout << "System: " << gameLink.GetSystemName() << std::endl;

    // 注册信号处理
    std::signal(SIGINT, SignalHandler);

    // 创建 Web 服务器
    WebRemoteServer webServer;

    // 帧数据获取回调
    auto frameGetter = [&gameLink](uint16_t& width, uint16_t& height) -> std::vector<uint8_t> {
        return gameLink.GetFrameBufferData(width, height);
    };

    // OCR 结果存储（供 web 服务器使用）
    static std::string s_latest_ocr_text;

    // OCR 类型存储（供 web 服务器使用）
    static std::string s_ocr_type = "ollama";  // 默认使用 Ollama OCR

    // OCR 区域存储（供 web 服务器和 OCR 循环使用）
    static struct OCRRegion {
        int x = -1, y = -1, width = -1, height = -1;
        bool valid = false;
    } s_ocr_region;

    // 输入处理回调
    auto inputCallback = [](const uint32_t* key_states, float mouse_x, float mouse_y, float mouse_dx, float mouse_dy, uint8_t mouse_btn) {
        if (g_gameLink) {
            g_gameLink->SendInput(key_states, mouse_x, mouse_y, mouse_dx, mouse_dy, mouse_btn);
        }
    };

    // OCR 结果获取回调
    auto ocrGetter = []() -> std::string {
        return s_latest_ocr_text;
    };

    // OCR 区域设置回调
    auto ocrRegionSetter = [](int x, int y, int w, int h, bool valid) {
        s_ocr_region.x = x;
        s_ocr_region.y = y;
        s_ocr_region.width = w;
        s_ocr_region.height = h;
        s_ocr_region.valid = valid;
    };

    // OCR 类型设置回调
    auto ocrTypeSetter = [](const std::string& type) {
        s_ocr_type = type;
    };

    // 帧更新请求回调 (触发 DOSBox-X 刷新帧缓冲)
    auto frameRequester = [&gameLink]() {
        gameLink.RequestFrameUpdate();
    };

    // 启动 Web 服务器
    if (!webServer.Start(options.web_port, frameGetter, inputCallback, ocrGetter, ocrRegionSetter, ocrTypeSetter, frameRequester)) {
        std::cerr << "Error: Failed to start web server" << std::endl;
        gameLink.Shutdown();
        return 1;
    }

    std::cout << "\nOpen " << webServer.GetURL() << " in your browser to control DOSBox-X" << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;

    // 主循环 - 同时运行 OCR
    Subtitle::SubtitleFile subtitleFile;
    std::string last_ocr_text;
    uint64_t start_time = 0;
    auto last_ocr_time = std::chrono::high_resolution_clock::now();

    // 保存上一帧 OCR 数据用于变化检测
    std::vector<uint8_t> last_ocr_data;
    int last_ocr_width = 0;
    int last_ocr_height = 0;
    const float CHANGE_THRESHOLD = 0.01f;  // 10% 变化阈值

    // 计算帧变化率的函数
    auto CalculateFrameChange = [](const std::vector<uint8_t>& current, const std::vector<uint8_t>& last) -> float {
        if (current.empty() || last.empty() || current.size() != last.size()) {
            return 1.0f;  // 如果没有上一帧或大小不同，强制执行 OCR
        }

        size_t changed_pixels = 0;
        size_t total_pixels = current.size() / 4;  // BGRA 格式

        // 比较每个像素（只比较亮度，忽略细微变化）
        for (size_t i = 0; i < current.size(); i += 4) {
            // 计算亮度差异
            int curr_brightness = (current[i] + current[i+1] + current[i+2]) / 3;
            int last_brightness = (last[i] + last[i+1] + last[i+2]) / 3;
            int diff = std::abs(curr_brightness - last_brightness);

            // 如果亮度差异超过阈值（比如 15），认为像素有变化
            if (diff > 15) {
                changed_pixels++;
            }
        }

        return static_cast<float>(changed_pixels) / total_pixels;
    };

    while (g_running && webServer.IsRunning()) {
        auto current_time = std::chrono::high_resolution_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_ocr_time).count();

        // 检查是否到达 OCR 采样周期
        if (elapsed_ms >= options.ocr_interval) {
            last_ocr_time = current_time;

            // 获取帧并进行 OCR
            uint16_t width, height;
            auto frame_data = gameLink.GetFrameBufferData(width, height);

            if (!frame_data.empty() && options.ocr_continuous) {
                // 如果设置了 OCR 区域，裁剪帧数据
                std::vector<uint8_t> ocr_data;
                int ocr_width = width;
                int ocr_height = height;

                if (s_ocr_region.valid) {
                    // 裁剪 BGRA 帧数据到指定区域
                    int crop_x = std::max(0, std::min(s_ocr_region.x, (int)width - 1));
                    int crop_y = std::max(0, std::min(s_ocr_region.y, (int)height - 1));
                    int crop_w = std::min(s_ocr_region.width, (int)width - crop_x);
                    int crop_h = std::min(s_ocr_region.height, (int)height - crop_y);

                    if (crop_w > 0 && crop_h > 0) {
                        ocr_data.resize(crop_w * crop_h * 4);
                        for (int y = 0; y < crop_h; ++y) {
                            for (int x = 0; x < crop_w; ++x) {
                                size_t src_offset = ((crop_y + y) * width + (crop_x + x)) * 4;
                                size_t dst_offset = (y * crop_w + x) * 4;
                                for (int b = 0; b < 4; ++b) {
                                    ocr_data[dst_offset + b] = frame_data[src_offset + b];
                                }
                            }
                        }
                        ocr_width = crop_w;
                        ocr_height = crop_h;
                    }
                } else {
                    ocr_data = frame_data;  // 使用完整帧
                }

                // 计算帧变化率，如果变化率低于阈值则跳过 OCR
                float change_rate = CalculateFrameChange(ocr_data, last_ocr_data);
                bool should_do_ocr = (change_rate >= CHANGE_THRESHOLD);

                // 如果 OCR 区域大小变化，也强制执行 OCR
                if (ocr_width != last_ocr_width || ocr_height != last_ocr_height) {
                    should_do_ocr = true;
                }

                if (!should_do_ocr) {
                    // 变化率低于阈值，跳过 OCR
                    last_ocr_data = ocr_data;
                    last_ocr_width = ocr_width;
                    last_ocr_height = ocr_height;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                std::cout << "[OCR] Execute: change rate " << (change_rate * 100) << "%" << std::endl;

                // 更新上一帧数据
                last_ocr_data = ocr_data;
                last_ocr_width = ocr_width;
                last_ocr_height = ocr_height;

                // 根据 OCR 类型选择引擎
                OCRType ocr_type;
                if (s_ocr_type == "ollama") {
                    ocr_type = OCRType::Ollama;
                } else if (s_ocr_type == "vision") {
                    ocr_type = OCRType::Vision;
#ifdef USE_PADDLEOCR
                } else if (s_ocr_type == "paddle") {
                    ocr_type = OCRType::PaddleOCR;
#endif
                } else {
                    ocr_type = OCRType::Ollama;  // 默认
                }
                auto ocr_start = std::chrono::high_resolution_clock::now();
                auto ocr_results = PerformOCR(ocr_data, ocr_width, ocr_height, ocr_type);
                auto ocr_end = std::chrono::high_resolution_clock::now();
                auto ocr_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ocr_end - ocr_start).count();
                std::cout << "[OCR] elapsed: " << ocr_ms << " ms" << std::endl;

                std::string current_text;
                for (const auto& result : ocr_results) {
                    if (result.confidence >= options.ocr_min_confidence) {
                        if (!current_text.empty()) current_text += "\n";
                        current_text += result.text;
                    }
                }

                if (!current_text.empty() && current_text != last_ocr_text) {
                    std::cout << "[OCR] " << current_text << std::endl;
                    last_ocr_text = current_text;
                    s_latest_ocr_text = current_text;  // 更新 web OCR 显示

                    // 通过 WebSocket 推送 OCR 结果到前端
                    webServer.BroadcastOCR(current_text);

                    if (!options.output_file.empty()) {
                        if (start_time == 0) {
                            start_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::high_resolution_clock::now().time_since_epoch()
                            ).count();
                        }
                        uint32_t current_ms = static_cast<uint32_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::high_resolution_clock::now().time_since_epoch()
                            ).count() - start_time
                        );
                        subtitleFile.AddSubtitle(current_ms, current_ms + options.ocr_interval, current_text);
                    }
                }
            }
        }

        // 小延迟防止 CPU 占用过高（不影响采样周期）
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // 导出字幕
    if (!options.output_file.empty() && subtitleFile.GetSubtitleCount() > 0) {
        std::cout << "\nExporting " << subtitleFile.GetSubtitleCount()
                  << " subtitles to: " << options.output_file << std::endl;
        subtitleFile.ExportToSRT(options.output_file);
    }

    webServer.Stop();
    gameLink.Shutdown();
    g_gameLink = nullptr;

    return 0;
}

// ============================================================================
// OCR 模式实现 (使用 macOS Vision framework)
// ============================================================================

int RunOCRMode(const Options& options) {
    GameLink::GameLinkInterface gameLink;

    std::cout << "Initializing Game Link for OCR mode..." << std::endl;

    if (!gameLink.Initialize()) {
        std::cerr << "Error: Failed to initialize Game Link. Is DOSBox-X running?" << std::endl;
        return 1;
    }

    std::cout << "Game: " << gameLink.GetGameName() << std::endl;
    std::cout << "System: " << gameLink.GetSystemName() << std::endl;
    std::cout << "OCR Interval: " << options.ocr_interval << " ms" << std::endl;
    std::cout << "Min Confidence: " << options.ocr_min_confidence << std::endl;
    std::cout << "\nPress Ctrl+C to stop\n" << std::endl;

    // 注册信号处理
    std::signal(SIGINT, SignalHandler);

    // 记录上次识别的文字（用于检测变化）
    std::string last_text;
    int ocr_count = 0;
    Subtitle::SubtitleFile subtitleFile;
    uint64_t start_time = 0;

    while (g_running) {
        ocr_count++;

        // 获取帧缓冲
        uint16_t width, height;
        auto frame_data = gameLink.GetFrameBufferData(width, height);

        if (frame_data.empty()) {
            if (options.verbose) {
                std::cout << "No frame data available" << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(options.ocr_interval));
            continue;
        }

        if (options.verbose) {
            std::cout << "\n=== OCR #" << ocr_count << " (frame: " << width << "x" << height << ") ===" << std::endl;
        }

        // 执行 OCR（默认使用 Ollama）
        OCRType ocr_type = OCRType::Ollama;
        auto ocr_results = PerformOCR(frame_data, width, height, ocr_type);

        // 过滤低置信度结果并合并文本
        std::string current_text;
        for (const auto& result : ocr_results) {
            if (result.confidence >= options.ocr_min_confidence && !result.text.empty()) {
                if (!current_text.empty()) {
                    current_text += "\n";
                }
                current_text += result.text;

                if (options.verbose) {
                    std::cout << "  [conf=" << result.confidence << "] "
                              << "(" << result.x << "," << result.y << ") "
                              << "\"" << result.text << "\"" << std::endl;
                }
            }
        }

        // 检查是否有变化
        if (!current_text.empty() && current_text != last_text) {
            std::cout << "\n[NEW TEXT] " << current_text << std::endl;

            last_text = current_text;

            // 记录字幕
            if (!options.output_file.empty()) {
                if (start_time == 0) {
                    start_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now().time_since_epoch()
                    ).count();
                }

                uint32_t current_ms = static_cast<uint32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now().time_since_epoch()
                    ).count() - start_time
                );

                subtitleFile.AddSubtitle(current_ms, current_ms + options.ocr_interval, current_text);
            }
        }

        // 如果不是持续模式，只执行一次
        if (!options.ocr_continuous) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(options.ocr_interval));
    }

    // 导出字幕
    if (!options.output_file.empty() && subtitleFile.GetSubtitleCount() > 0) {
        std::string output_lower = options.output_file;
        std::transform(output_lower.begin(), output_lower.end(), output_lower.begin(),
                      [](char c) { return std::tolower(c); });

        std::cout << "\nExporting " << subtitleFile.GetSubtitleCount()
                  << " subtitles to: " << options.output_file << std::endl;

        if (output_lower.find(".srt") != std::string::npos) {
            subtitleFile.ExportToSRT(options.output_file);
        } else if (output_lower.find(".ass") != std::string::npos ||
                   output_lower.find(".ssa") != std::string::npos) {
            subtitleFile.ExportToASS(options.output_file);
        } else if (output_lower.find(".vtt") != std::string::npos) {
            subtitleFile.ExportToVTT(options.output_file);
        } else {
            subtitleFile.ExportToJSON(options.output_file);
        }
    }

    gameLink.Shutdown();
    return 0;
}

// ============================================================================
// Scan 模式实现
// ============================================================================

int RunScanMode(const Options& options) {
    // 设置调试模式
    SetScanDebug(options.scan_debug);

    // 直接使用 GameLink 接口
    GameLink::GameLinkInterface gameLink;

    std::cout << "Initializing Game Link for scan mode..." << std::endl;

    if (!gameLink.Initialize()) {
        std::cerr << "Error: Failed to initialize Game Link. Is DOSBox-X running?" << std::endl;
        return 1;
    }

    uint32_t ram_size = gameLink.GetRAMSize();
    if (ram_size == 0) {
        ram_size = 640 * 1024;  // 默认 640KB
    }

    // DOS 常规内存范围是 0 - 0xA0000 (640KB)
    // 默认扫描常规内存，除非用户指定
    uint32_t conventional_mem_end = 0xA0000;  // 640KB
    uint32_t scan_start = options.scan_start;
    uint32_t scan_end = options.scan_end > 0 ? options.scan_end : conventional_mem_end;

    // 如果用户指定了超过常规内存的范围，使用 RAM size
    if (scan_end > conventional_mem_end && options.scan_end == 0) {
        scan_end = std::min(ram_size, static_cast<uint32_t>(0x100000));  // 限制 1MB
    }

    std::cout << "Game: " << gameLink.GetGameName() << std::endl;
    std::cout << "RAM Size: " << (ram_size / 1024) << " KB" << std::endl;
    std::cout << "Scan Range: 0x" << std::hex << scan_start << " - 0x" << scan_end << std::dec << std::endl;
    std::cout << "Charset: " << options.scan_charset << std::endl;
    std::cout << "Min Length: " << options.scan_min_length << " characters" << std::endl;
    std::cout << "\nPress Ctrl+C to stop\n" << std::endl;

    // 注册信号处理
    std::signal(SIGINT, SignalHandler);

    // 记录上次找到的字符串（用于检测变化）
    std::map<uint32_t, std::string> last_strings;
    int scan_count = 0;

    while (g_running) {
        scan_count++;

        if (options.verbose) {
            std::cout << "\n=== Scan #" << scan_count << " ===" << std::endl;
        }

        auto results = gameLink.ScanMemoryRange(
            scan_start,
            scan_end,
            options.scan_min_length,
            options.scan_charset,
            options.scan_text,
            options.scan_offset
        );

        if (!results.empty()) {
            std::cout << "Found " << results.size() << " strings:" << std::endl;

            for (const auto& [addr, text] : results) {
                // 检查是否有变化
                bool changed = false;
                if (last_strings.find(addr) == last_strings.end()) {
                    changed = true;
                } else if (last_strings[addr] != text) {
                    changed = true;
                }

                // 显示字符串
                if (options.scan_continuous) {
                    if (changed) {
                        std::cout << "  [NEW] 0x" << std::hex << addr << std::dec
                                  << ": \"" << text << "\"" << std::endl;
                    } else if (options.verbose) {
                        std::cout << "  [SAME] 0x" << std::hex << addr << std::dec
                                  << ": \"" << text << "\"" << std::endl;
                    }
                } else {
                    std::cout << "  0x" << std::hex << addr << std::dec
                              << ": \"" << text << "\"" << std::endl;
                }

                last_strings[addr] = text;
            }
        } else if (options.verbose) {
            std::cout << "No strings found in this scan" << std::endl;
        }

        // 如果不是持续扫描模式，只扫描一次
        if (!options.scan_continuous) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(options.scan_interval));
    }

    std::cout << "\nScan completed. Total scans: " << scan_count << std::endl;
    std::cout << "Unique strings found: " << last_strings.size() << std::endl;

    gameLink.Shutdown();

    return 0;
}

// ============================================================================
// 正常提取模式
// ============================================================================

int RunExtractMode(const Options& options) {
    if (options.game_config.empty()) {
        std::cerr << "Error: No game configuration specified. Use --game <name>" << std::endl;
        PrintUsage("subtitle_extractor");
        return 1;
    }

    // 初始化字幕提取器
    Subtitle::SubtitleExtractor extractor;

    if (options.verbose) {
        std::cout << "Initializing Game Link..." << std::endl;
    }

    if (!extractor.Initialize()) {
        std::cerr << "Error: Failed to initialize Game Link. Is DOSBox-X running?" << std::endl;
        return 1;
    }

    if (options.verbose) {
        std::cout << "Loading game configuration: " << options.game_config << std::endl;
    }

    if (!extractor.LoadGameConfig(options.game_config)) {
        std::cerr << "Error: Failed to load game configuration" << std::endl;
        return 1;
    }

    // 设置编码
    auto from_cp = StringToCodePage(options.encoding_from);
    auto to_cp = StringToCodePage(options.encoding_to);
    extractor.SetEncoding(from_cp, to_cp);

    if (options.verbose) {
        std::cout << "Game: " << extractor.GetGameName() << std::endl;
        std::cout << "Encoding: " << options.encoding_from << " -> " << options.encoding_to << std::endl;
        std::cout << "Output file: " << (options.output_file.empty() ? "(none)" : options.output_file) << std::endl;
        std::cout << "\nStarting subtitle extraction...\n" << std::endl;
    }

    // 注册信号处理
    std::signal(SIGINT, SignalHandler);

    // 字幕收集
    Subtitle::SubtitleFile subtitleFile;
    uint32_t subtitle_count = 0;
    uint64_t start_time = 0;

    // 主循环
    while (g_running) {
        auto subtitle = extractor.ExtractSubtitle();

        if (subtitle.changed && !subtitle.text.empty()) {
            subtitle_count++;

            if (options.verbose) {
                std::cout << "[" << subtitle_count << "] " << subtitle.text << std::endl;
            }

            if (!options.output_file.empty()) {
                // 更新开始时间
                if (start_time == 0) {
                    start_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now().time_since_epoch()
                    ).count();
                }

                // 添加到字幕文件
                uint32_t current_time = static_cast<uint32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now().time_since_epoch()
                    ).count() - start_time
                );

                uint32_t end_time = current_time + subtitle.duration_ms;
                subtitleFile.AddSubtitle(current_time, end_time, subtitle.text);
            }

            if (options.display_subtitles) {
                std::cout << subtitle.text << std::endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(options.timeout_ms));
    }

    // 导出字幕
    if (!options.output_file.empty() && subtitle_count > 0) {
        std::string output_lower = options.output_file;
        std::transform(output_lower.begin(), output_lower.end(), output_lower.begin(),
                      [](char c) { return std::tolower(c); });

        if (options.verbose) {
            std::cout << "\nExporting " << subtitle_count << " subtitles to: "
                     << options.output_file << std::endl;
        }

        if (output_lower.find(".srt") != std::string::npos) {
            subtitleFile.ExportToSRT(options.output_file);
        } else if (output_lower.find(".ass") != std::string::npos ||
                   output_lower.find(".ssa") != std::string::npos) {
            subtitleFile.ExportToASS(options.output_file);
        } else if (output_lower.find(".vtt") != std::string::npos) {
            subtitleFile.ExportToVTT(options.output_file);
        } else if (output_lower.find(".json") != std::string::npos) {
            subtitleFile.ExportToJSON(options.output_file);
        } else {
            std::cerr << "Error: Unsupported file format. Use .srt, .ass, .vtt, or .json" << std::endl;
            return 1;
        }

        if (options.verbose) {
            std::cout << "Export completed successfully!" << std::endl;
        }
    }

    extractor.Shutdown();

    return 0;
}

// ============================================================================
// 主入口
// ============================================================================

int main(int argc, char* argv[]) {
    Options options;

    ParseArguments(argc, argv, options);

    if (options.help) {
        PrintUsage(argv[0]);
        return 0;
    }

    // 选择运行模式
    if (options.web_remote) {
        return RunWebRemoteMode(options);
    } else if (options.ocr_mode) {
        return RunOCRMode(options);
    } else if (options.scan_mode) {
        return RunScanMode(options);
    } else {
        return RunExtractMode(options);
    }
}