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

    WebRemoteServer();
    ~WebRemoteServer();

    /// 启动服务器
    /// @param port 端口号 (默认 8080)
    /// @param frame_getter 获取帧数据的回调
    /// @param input_callback 处理输入的回调
    /// @return 成功返回 true
    bool Start(int port, FrameGetter frame_getter, InputCallback input_callback);

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

    void ServerLoop();
    void HandleRequest(int client_socket);
    std::vector<uint8_t> EncodeFrameToJPEG(
        const uint8_t* argb_data,
        int width,
        int height
    );
};