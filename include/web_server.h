#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <set>

// Forward declaration
struct lws_context;
struct lws;

/// Web 远程控制服务器 (WebSocket 版本 - 使用 libwebsockets)
/// 提供浏览器访问 DOSBox-X 画面和发送键盘输入
class WebRemoteServer {
public:
    /// 输入回调函数类型
    using InputCallback = std::function<void(
        const uint32_t* key_states,
        float mouse_x,
        float mouse_y,
        float mouse_dx,
        float mouse_dy,
        uint8_t mouse_btn
    )>;

    /// 帧数据获取函数类型
    using FrameGetter = std::function<std::vector<uint8_t>(uint16_t& width, uint16_t& height)>;

    /// OCR 结果获取函数类型
    using OCRGetter = std::function<std::string()>;

    /// OCR 区域设置函数类型
    using OCRRegionSetter = std::function<void(int x, int y, int width, int height, bool valid)>;

    /// OCR 类型设置函数类型
    using OCRTypeSetter = std::function<void(const std::string& ocr_type)>;

    /// 翻译语言设置函数类型
    using TranslationLangSetter = std::function<void(const std::string& lang)>;

    /// 帧更新请求函数类型 (触发DOSBox-X刷新帧缓冲)
    using FrameUpdateRequester = std::function<void()>;

    WebRemoteServer();
    ~WebRemoteServer();

    /// 启动服务器
    bool Start(int port, FrameGetter frame_getter, InputCallback input_callback,
               OCRGetter ocr_getter = nullptr, OCRRegionSetter ocr_region_setter = nullptr,
               OCRTypeSetter ocr_type_setter = nullptr, TranslationLangSetter translation_lang_setter = nullptr,
               FrameUpdateRequester frame_requester = nullptr);

    /// 停止服务器
    void Stop();

    /// 是否正在运行
    bool IsRunning() const;

    /// 获取服务器 URL
    std::string GetURL() const;

    /// 广播帧数据到所有客户端
    void BroadcastFrame(const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height);

    /// 广播 OCR 结果到所有客户端
    void BroadcastOCR(const std::string& ocr_text);

    /// 广播翻译结果到所有客户端
    void BroadcastTranslation(const std::string& translation_text);

    // === Internal state (public for callback access) ===
    std::string m_htmlContent;
    InputCallback m_inputCallback;
    OCRRegionSetter m_ocrRegionSetter;
    OCRTypeSetter m_ocrTypeSetter;
    TranslationLangSetter m_translationLangSetter;
    FrameUpdateRequester m_frameRequester;

    std::set<lws*> m_wsiClients;
    std::mutex m_clientsMutex;

    std::vector<std::vector<uint8_t>> m_frameQueue;
    std::vector<std::string> m_ocrQueue;
    std::vector<std::string> m_translationQueue;
    std::mutex m_queueMutex;

    // Cached frame data for continuous broadcast when GameLink has no new data
    std::vector<uint8_t> m_lastFrameData;
    uint16_t m_lastWidth = 0;
    uint16_t m_lastHeight = 0;
    bool m_lastFrameValid = false;
    std::mutex m_frameCacheMutex;

private:
    std::atomic<bool> m_running;
    int m_port;

    FrameGetter m_frameGetter;
    OCRGetter m_ocrGetter;

    lws_context* m_context;
    std::thread m_thread;

    void ServiceLoop();
};

// Global server instance pointer (for callbacks)
extern WebRemoteServer* g_web_server_instance;