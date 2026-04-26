#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <thread>
#include <atomic>

/// Web 远程控制服务器
/// 提供浏览器访问 DOSBox-X 画面和发送键盘输入
class WebRemoteServer {
public:
    /// 输入回调函数类型
    /// @param key_states 键盘状态数组 (8个 uint32_t)
    /// @param mouse_dx 鼠标 X 增量
    /// @param mouse_dy 鼠盘 Y 增量
    /// @param mouse_btn 鼠盘按钮状态
    using InputCallback = std::function<void(
        const uint32_t* key_states,
        float mouse_dx,
        float mouse_dy,
        uint8_t mouse_btn
    )>;

    /// 帧数据获取函数类型
    /// @return 帧缓冲数据 (ARGB 格式) 和宽高
    using FrameGetter = std::function<std::vector<uint8_t>(uint16_t& width, uint16_t& height)>;

    /// OCR 结果获取函数类型
    /// @return OCR 识别的文本
    using OCRGetter = std::function<std::string()>;

    /// OCR 区域设置函数类型
    /// @param x 区域左上角 x
    /// @param y 区域左上角 y
    /// @param width 区域宽度
    /// @param height 区域高度
    /// @param valid 是否有效（false 表示清除区域）
    using OCRRegionSetter = std::function<void(int x, int y, int width, int height, bool valid)>;

    WebRemoteServer();
    ~WebRemoteServer();

    /// 启动服务器
    /// @param port 端口号 (默认 8080)
    /// @param frame_getter 获取帧数据的回调
    /// @param input_callback 处理输入的回调
    /// @param ocr_getter 获取 OCR 结果的回调（可选）
    /// @param ocr_region_setter 设置 OCR 区域的回调（可选）
    /// @return 成功返回 true
    bool Start(int port, FrameGetter frame_getter, InputCallback input_callback,
               OCRGetter ocr_getter = nullptr, OCRRegionSetter ocr_region_setter = nullptr);

    /// 停止服务器
    void Stop();

    /// 是否正在运行
    bool IsRunning() const;

    /// 获取服务器 URL
    std::string GetURL() const;

private:
    std::atomic<bool> m_running;
    std::thread m_thread;
    int m_port;
    int m_socket;

    FrameGetter m_frameGetter;
    InputCallback m_inputCallback;
    OCRGetter m_ocrGetter;
    OCRRegionSetter m_ocrRegionSetter;

    std::string m_htmlContent;  // 缓存 HTML 内容

    void ServerLoop();
    void HandleRequest(int client_socket);
    std::vector<uint8_t> EncodeFrameToJPEG(
        const uint8_t* argb_data,
        int width,
        int height
    );
};