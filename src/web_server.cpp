// Web Remote Server - WebSocket server using libwebsockets library

#include "web_server.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <chrono>
#include <vector>

#include <libwebsockets.h>

// TurboJPEG (快速 JPEG 编码，比 OpenCV imencode 快 2-5 倍)
#ifdef HAVE_TURBOJPEG
#include <turbojpeg.h>
#else
#include <opencv2/opencv.hpp>
#endif

// Global server instance
WebRemoteServer* g_web_server_instance = nullptr;

// Protocol definitions
static int callback_http(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len);
static int callback_ws(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len);

static const struct lws_protocols protocols[] = {
    {
        "http",
        callback_http,
        0,
        0,
        0, nullptr, 0
    },
    {
        "ws",
        callback_ws,
        0,
        0,
        0, nullptr, 0
    },
    { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
};

// JSON parsing helpers
static int ParseJsonInt(const std::string& json, const std::string& key) {
    size_t key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return -1;
    size_t colon_pos = json.find(':', key_pos);
    if (colon_pos == std::string::npos) return -1;
    size_t num_start = colon_pos + 1;
    while (num_start < json.size() && (json[num_start] == ' ' || json[num_start] == '\t')) num_start++;
    if (num_start < json.size() && ((json[num_start] >= '0' && json[num_start] <= '9') || json[num_start] == '-'))
        return std::stoi(json.substr(num_start));
    return -1;
}

static float ParseJsonFloat(const std::string& json, const std::string& key) {
    size_t key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return 0.0f;
    size_t colon_pos = json.find(':', key_pos);
    if (colon_pos == std::string::npos) return 0.0f;
    size_t num_start = colon_pos + 1;
    while (num_start < json.size() && (json[num_start] == ' ' || json[num_start] == '\t')) num_start++;
    if (num_start < json.size() && ((json[num_start] >= '0' && json[num_start] <= '9') || json[num_start] == '-' || json[num_start] == '.'))
        return std::stof(json.substr(num_start));
    return 0.0f;
}

static std::string ParseJsonString(const std::string& json, const std::string& key) {
    size_t key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return "";
    size_t colon_pos = json.find(':', key_pos);
    if (colon_pos == std::string::npos) return "";
    size_t str_start = colon_pos + 1;
    while (str_start < json.size() && (json[str_start] == ' ' || json[str_start] == '\t')) str_start++;
    if (str_start < json.size() && json[str_start] == '"') {
        str_start++;
        size_t str_end = json.find('"', str_start);
        if (str_end != std::string::npos) return json.substr(str_start, str_end - str_start);
    }
    return "";
}

static std::string LoadHTMLFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Warning: Failed to load HTML file: " << filepath << std::endl;
        return "<html><body><h1>HTML file not found</h1></body></html>";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

WebRemoteServer::WebRemoteServer()
    : m_running(false)
    , m_port(9091)
    , m_context(nullptr)
{
}

WebRemoteServer::~WebRemoteServer() {
    Stop();
}

bool WebRemoteServer::Start(int port, FrameGetter frame_getter, InputCallback input_callback,
                              OCRGetter ocr_getter, OCRRegionSetter ocr_region_setter,
                              OCRTypeSetter ocr_type_setter, TranslationLangSetter translation_lang_setter,
                              FrameUpdateRequester frame_requester) {
    g_web_server_instance = this;
    m_port = port;
    m_frameGetter = frame_getter;
    m_inputCallback = input_callback;
    m_ocrGetter = ocr_getter;
    m_ocrRegionSetter = ocr_region_setter;
    m_ocrTypeSetter = ocr_type_setter;
    m_translationLangSetter = translation_lang_setter;
    m_frameRequester = frame_requester;
    m_htmlContent = LoadHTMLFile("resources/web_remote.html");

    lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE, nullptr);

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = port;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = 0;

    m_context = lws_create_context(&info);
    if (!m_context) {
        std::cerr << "Error: Failed to create libwebsockets context" << std::endl;
        return false;
    }

    m_running = true;
    m_thread = std::thread(&WebRemoteServer::ServiceLoop, this);

    std::cout << "WebSocket Remote Server started at http://localhost:" << m_port << std::endl;
    std::cout << "Open this URL in your browser to control DOSBox-X" << std::endl;
    return true;
}

void WebRemoteServer::Stop() {
    m_running = false;

    if (m_context) {
        lws_context_destroy(m_context);
        m_context = nullptr;
    }

    if (m_thread.joinable()) m_thread.join();

    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        m_wsiClients.clear();
    }

    g_web_server_instance = nullptr;
}

bool WebRemoteServer::IsRunning() const {
    return m_running;
}

std::string WebRemoteServer::GetURL() const {
    return "http://localhost:" + std::to_string(m_port);
}

void WebRemoteServer::ServiceLoop() {
    auto last_frame_time = std::chrono::high_resolution_clock::now();
    constexpr int FRAME_INTERVAL_MS = 16;  // ~60 FPS

    while (m_running) {
        // lws_service 处理所有事件，最多等待 1ms
        lws_service(m_context, 1);

        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame_time).count();

        if (elapsed >= FRAME_INTERVAL_MS) {
            last_frame_time = now;

            // 请求 DOSBox-X 更新帧缓冲
            if (m_frameRequester) {
                m_frameRequester();
            }

            // 获取帧数据
            uint16_t width, height;
            auto frame_data = m_frameGetter(width, height);

            // 如果有新帧数据，缓存它
            if (!frame_data.empty()) {
                std::lock_guard<std::mutex> cache_lock(m_frameCacheMutex);
                m_lastFrameData = frame_data;
                m_lastWidth = width;
                m_lastHeight = height;
                m_lastFrameValid = true;
            }

            // 使用缓存的帧数据进行广播
            std::vector<uint8_t> broadcast_data;
            uint16_t bw = 0, bh = 0;
            {
                std::lock_guard<std::mutex> cache_lock(m_frameCacheMutex);
                if (m_lastFrameValid) {
                    broadcast_data = m_lastFrameData;
                    bw = m_lastWidth;
                    bh = m_lastHeight;
                }
            }

            if (!broadcast_data.empty()) {
                BroadcastFrame(broadcast_data, bw, bh);

                // 触发 writable
                {
                    std::lock_guard<std::mutex> lock(m_clientsMutex);
                    for (lws* wsi : m_wsiClients) {
                        lws_callback_on_writable(wsi);
                    }
                }
            }
        }

        // OCR 消息检查
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (!m_ocrQueue.empty()) {
                lws_cancel_service(m_context);
            }
        }
    }
}

void WebRemoteServer::BroadcastFrame(const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height) {
    std::vector<uint8_t> message;

    if (!frame_data.empty() && width > 0 && height > 0) {
#ifdef HAVE_TURBOJPEG
        // 使用 TurboJPEG (比 OpenCV imencode 快 2-5 倍)
        static tjhandle tj_compressor = tjInitCompress();

        if (tj_compressor) {
            unsigned char* jpeg_buf = nullptr;
            unsigned long jpeg_size = 0;

            // 先分配足够大的缓冲区 (JPEG 最大不会超过原始数据)
            jpeg_buf = (unsigned char*)tjAlloc(frame_data.size());
            if (!jpeg_buf) {
                std::cerr << "[TurboJPEG] tjAlloc failed" << std::endl;
            }

            int result = tjCompress2(tj_compressor,
                                     frame_data.data(),
                                     width,
                                     width * 4,  // pitch = width * 4 bytes (BGRA)
                                     height,
                                     TJPF_BGRA,
                                     &jpeg_buf,
                                     &jpeg_size,
                                     TJSAMP_420,
                                     60,
                                     TJFLAG_FASTDCT);

            if (result == 0 && jpeg_buf && jpeg_size > 0) {
                message.push_back(0x02);
                message.push_back(width & 0xFF);
                message.push_back((width >> 8) & 0xFF);
                message.push_back(height & 0xFF);
                message.push_back((height >> 8) & 0xFF);
                message.insert(message.end(), jpeg_buf, jpeg_buf + jpeg_size);
            } else {
                std::cerr << "[TurboJPEG] tjCompress2 failed: " << tjGetErrorStr() << std::endl;
                // 发送原始数据
                message.push_back(0x01);
                message.push_back(width & 0xFF);
                message.push_back((width >> 8) & 0xFF);
                message.push_back(height & 0xFF);
                message.push_back((height >> 8) & 0xFF);
                message.insert(message.end(), frame_data.begin(), frame_data.end());
            }

            if (jpeg_buf) tjFree(jpeg_buf);
        } else {
            std::cerr << "[TurboJPEG] tjInitCompress failed" << std::endl;
            message.push_back(0x01);
            message.push_back(width & 0xFF);
            message.push_back((width >> 8) & 0xFF);
            message.push_back(height & 0xFF);
            message.push_back((height >> 8) & 0xFF);
            message.insert(message.end(), frame_data.begin(), frame_data.end());
        }
#else
        // 使用 OpenCV imencode
        cv::Mat mat(height, width, CV_8UC4, (void*)frame_data.data());
        cv::Mat bgr;
        cv::cvtColor(mat, bgr, cv::COLOR_BGRA2BGR);

        std::vector<uint8_t> jpeg_data;
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 60};
        if (cv::imencode(".jpg", bgr, jpeg_data, params)) {
            message.push_back(0x02);
            message.push_back(width & 0xFF);
            message.push_back((width >> 8) & 0xFF);
            message.push_back(height & 0xFF);
            message.push_back((height >> 8) & 0xFF);
            message.insert(message.end(), jpeg_data.begin(), jpeg_data.end());
        } else {
            message.push_back(0x01);
            message.push_back(width & 0xFF);
            message.push_back((width >> 8) & 0xFF);
            message.push_back(height & 0xFF);
            message.push_back((height >> 8) & 0xFF);
            message.insert(message.end(), frame_data.begin(), frame_data.end());
        }
#endif
    }

    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_frameQueue.clear();
    if (!message.empty()) {
        m_frameQueue.push_back(std::move(message));
    }
}

void WebRemoteServer::BroadcastOCR(const std::string& ocr_text) {
    std::string json_text;
    for (char c : ocr_text) {
        if (c == '"') json_text += "\\\"";
        else if (c == '\\') json_text += "\\\\";
        else if (c == '\n') json_text += "\\n";
        else if (c == '\r') json_text += "";
        else json_text += c;
    }
    std::string message = "{\"type\":\"ocr\",\"text\":\"" + json_text + "\"}";

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_ocrQueue.clear();
        m_ocrQueue.push_back(std::move(message));
    }

    if (m_context) {
        lws_cancel_service(m_context);
    }
}

void WebRemoteServer::BroadcastTranslation(const std::string& translation_text) {
    std::string json_text;
    for (char c : translation_text) {
        if (c == '"') json_text += "\\\"";
        else if (c == '\\') json_text += "\\\\";
        else if (c == '\n') json_text += "\\n";
        else if (c == '\r') json_text += "";
        else json_text += c;
    }
    std::string message = "{\"type\":\"translation\",\"text\":\"" + json_text + "\"}";

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_translationQueue.clear();
        m_translationQueue.push_back(std::move(message));
    }

    if (m_context) {
        lws_cancel_service(m_context);
    }
}

// HTTP callback
static int callback_http(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
    (void)user;
    (void)len;

    switch (reason) {
        case LWS_CALLBACK_HTTP: {
            const char* path = (const char*)in;
            if (!path) path = "/";

            if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
                std::string html = g_web_server_instance ? g_web_server_instance->m_htmlContent : "";

                // Set HTTP headers
                unsigned char buffer[1024 + LWS_PRE];
                unsigned char *p = buffer + LWS_PRE;
                unsigned char *end = p + sizeof(buffer) - LWS_PRE;

                if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &p, end))
                    return 1;
                if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
                    (unsigned char *)"text/html; charset=utf-8", 24, &p, end))
                    return 1;
                if (lws_add_http_header_content_length(wsi, html.size(), &p, end))
                    return 1;
                if (lws_finalize_http_header(wsi, &p, end))
                    return 1;

                // Write headers
                int n = lws_write(wsi, buffer + LWS_PRE, p - (buffer + LWS_PRE), LWS_WRITE_HTTP_HEADERS);
                if (n < 0) return 1;

                // Write body
                size_t total_size = LWS_PRE + html.size();
                std::vector<unsigned char> body_buf(total_size);
                memcpy(&body_buf[LWS_PRE], html.data(), html.size());
                lws_write(wsi, &body_buf[LWS_PRE], html.size(), LWS_WRITE_HTTP);

                if (lws_http_transaction_completed(wsi))
                    return -1;
            }
            break;
        }
        default:
            break;
    }
    return 0;
}

// WebSocket callback
static int callback_ws(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len) {
    (void)user;

    auto* srv = g_web_server_instance;
    if (!srv) return 0;

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            std::lock_guard<std::mutex> lock(srv->m_clientsMutex);
            srv->m_wsiClients.insert(wsi);
            std::cout << "[WS] Client connected" << std::endl;
            // 立即触发 writable
            lws_callback_on_writable(wsi);
            break;
        }

        case LWS_CALLBACK_CLOSED: {
            std::lock_guard<std::mutex> lock(srv->m_clientsMutex);
            srv->m_wsiClients.erase(wsi);
            std::cout << "[WS] Client disconnected" << std::endl;
            break;
        }

        case LWS_CALLBACK_RECEIVE: {
            std::string message((const char*)in, len);

            if (message.find("\"type\":\"input\"") != std::string::npos) {
                uint32_t key_states[8] = {0};
                size_t keys_pos = message.find("\"keys\"");
                if (keys_pos != std::string::npos) {
                    size_t arr_start = message.find('[', keys_pos);
                    size_t arr_end = message.find(']', arr_start);
                    if (arr_start != std::string::npos && arr_end != std::string::npos) {
                        std::string arr = message.substr(arr_start + 1, arr_end - arr_start - 1);
                        size_t pos = 0;
                        int idx = 0;
                        while (pos < arr.size() && idx < 8) {
                            while (pos < arr.size() && (arr[pos] == ' ' || arr[pos] == ',')) pos++;
                            if (pos < arr.size() && arr[pos] >= '0' && arr[pos] <= '9') {
                                key_states[idx++] = std::stoul(arr.substr(pos));
                                while (pos < arr.size() && arr[pos] >= '0' && arr[pos] <= '9') pos++;
                            } else pos++;
                        }
                    }
                }
                float mx = ParseJsonFloat(message, "mouse_x");
                float my = ParseJsonFloat(message, "mouse_y");
                float dx = ParseJsonFloat(message, "mouse_dx");
                float dy = ParseJsonFloat(message, "mouse_dy");
                int btn = ParseJsonInt(message, "mouseBtn");

                if (srv->m_inputCallback) {
                    srv->m_inputCallback(key_states, mx, my, dx, dy, btn);
                }
            } else if (message.find("\"type\":\"region\"") != std::string::npos) {
                int x = ParseJsonInt(message, "x");
                int y = ParseJsonInt(message, "y");
                int w = ParseJsonInt(message, "width");
                int h = ParseJsonInt(message, "height");
                bool valid = (x >= 0 && y >= 0 && w > 0 && h > 0);
                if (srv->m_ocrRegionSetter) {
                    srv->m_ocrRegionSetter(x, y, w, h, valid);
                }
                std::cout << "[WS] Region: (" << x << "," << y << ") " << w << "x" << h << std::endl;
            } else if (message.find("\"type\":\"ocr-type\"") != std::string::npos) {
                std::string ocr_type = ParseJsonString(message, "ocr_type");
                if (srv->m_ocrTypeSetter && !ocr_type.empty()) {
                    srv->m_ocrTypeSetter(ocr_type);
                    std::cout << "[WS] OCR type: " << ocr_type << std::endl;
                }
            } else if (message.find("\"type\":\"translation-lang\"") != std::string::npos) {
                std::string lang = ParseJsonString(message, "lang");
                if (srv->m_translationLangSetter && !lang.empty()) {
                    srv->m_translationLangSetter(lang);
                    std::cout << "[WS] Translation lang: " << lang << std::endl;
                }
            }
            break;
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: {
            std::lock_guard<std::mutex> lock(srv->m_queueMutex);

            if (!srv->m_ocrQueue.empty()) {
                for (const auto& msg : srv->m_ocrQueue) {
                    size_t total_size = LWS_PRE + msg.size();
                    std::vector<unsigned char> buf(total_size);
                    memcpy(&buf[LWS_PRE], msg.data(), msg.size());
                    lws_write(wsi, &buf[LWS_PRE], msg.size(), LWS_WRITE_TEXT);
                }
                srv->m_ocrQueue.clear();
            }

            if (!srv->m_translationQueue.empty()) {
                for (const auto& msg : srv->m_translationQueue) {
                    size_t total_size = LWS_PRE + msg.size();
                    std::vector<unsigned char> buf(total_size);
                    memcpy(&buf[LWS_PRE], msg.data(), msg.size());
                    lws_write(wsi, &buf[LWS_PRE], msg.size(), LWS_WRITE_TEXT);
                }
                srv->m_translationQueue.clear();
            }

            // 发送帧数据，限制60FPS
            if (!srv->m_frameQueue.empty()) {
                static auto last_send_time = std::chrono::high_resolution_clock::now();
                constexpr int SEND_INTERVAL_MS = 16;  // ~60 FPS
                auto now = std::chrono::high_resolution_clock::now();
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_send_time).count();

                if (elapsed_ms >= SEND_INTERVAL_MS) {
                    last_send_time = now;
                    const auto& msg = srv->m_frameQueue.back();
                    size_t total_size = LWS_PRE + msg.size();
                    std::vector<unsigned char> buf(total_size);
                    memcpy(&buf[LWS_PRE], msg.data(), msg.size());
                    lws_write(wsi, &buf[LWS_PRE], msg.size(), LWS_WRITE_BINARY);
                }
            }

            // 总是请求下一次 writable，保持发送循环
            lws_callback_on_writable(wsi);
            break;
        }

        default:
            break;
    }

    return 0;
}