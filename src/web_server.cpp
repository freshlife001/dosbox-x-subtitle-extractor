// Web Remote Server - HTTP server for browser-based remote control

#include "web_server.h"
#include <iostream>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

// HTML 页面模板（包含 JavaScript 远程控制代码）
static const char* HTML_PAGE = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>DOSBox-X Remote Control</title>
    <style>
        body { margin: 0; background: #000; display: flex; justify-content: center; align-items: center; height: 100vh; }
        canvas { border: 2px solid #333; }
        #info { position: fixed; top: 10px; left: 10px; color: #fff; font-family: monospace; }
        #ocr { position: fixed; top: 10px; right: 10px; color: #0f0; font-family: monospace; background: rgba(0,0,0,0.7); padding: 10px; }
    </style>
</head>
<body>
    <div id="info">FPS: <span id="fps">0</span></div>
    <div id="ocr">OCR: <span id="ocr-text">-</span></div>
    <canvas id="screen"></canvas>
    <script>
const canvas = document.getElementById('screen');
const ctx = canvas.getContext('2d');
const fpsSpan = document.getElementById('fps');
const ocrText = document.getElementById('ocr-text');

// 键盘状态
const keyStates = new Uint32Array(8);
const keyMap = {
    'Escape': 0x01, '1': 0x02, '2': 0x03, '3': 0x04, '4': 0x05,
    '5': 0x06, '6': 0x07, '7': 0x08, '8': 0x09, '9': 0x0A, '0': 0x0B,
    'Minus': 0x0C, 'Equal': 0x0D, 'Backspace': 0x0E, 'Tab': 0x0F,
    'q': 0x10, 'w': 0x11, 'e': 0x12, 'r': 0x13, 't': 0x14,
    'y': 0x15, 'u': 0x16, 'i': 0x17, 'o': 0x18, 'p': 0x19,
    'BracketLeft': 0x1A, 'BracketRight': 0x1B, 'Enter': 0x1C,
    'ControlLeft': 0x1D, 'a': 0x1E, 's': 0x1F, 'd': 0x20, 'f': 0x21,
    'g': 0x22, 'h': 0x23, 'j': 0x24, 'k': 0x25, 'l': 0x26,
    'Semicolon': 0x27, 'Quote': 0x28, 'Backquote': 0x29,
    'ShiftLeft': 0x2A, 'Backslash': 0x2B, 'z': 0x2C, 'x': 0x2D,
    'c': 0x2E, 'v': 0x2F, 'b': 0x30, 'n': 0x31, 'm': 0x32,
    'Comma': 0x33, 'Period': 0x34, 'Slash': 0x35, 'ShiftRight': 0x36,
    'AltLeft': 0x38, 'Space': 0x39, 'CapsLock': 0x3A,
    'F1': 0x3B, 'F2': 0x3C, 'F3': 0x3D, 'F4': 0x3E, 'F5': 0x3F,
    'F6': 0x40, 'F7': 0x41, 'F8': 0x42, 'F9': 0x43, 'F10': 0x44,
    'ArrowUp': 0x48, 'ArrowLeft': 0x4B, 'ArrowRight': 0x4D, 'ArrowDown': 0x50,
    'Insert': 0x52, 'Delete': 0x53,
};

function setKey(scancode, pressed) {
    const index = scancode >> 5;
    const bit = scancode & 0x1F;
    if (pressed) {
        keyStates[index] |= (1 << bit);
    } else {
        keyStates[index] &= ~(1 << bit);
    }
    sendInput();
}

function sendInput() {
    fetch('/input', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ keys: Array.from(keyStates) })
    });
}

document.addEventListener('keydown', (e) => {
    e.preventDefault();
    const scancode = keyMap[e.key] || keyMap[e.key.toLowerCase()];
    if (scancode) setKey(scancode, true);
});

document.addEventListener('keyup', (e) => {
    e.preventDefault();
    const scancode = keyMap[e.key] || keyMap[e.key.toLowerCase()];
    if (scancode) setKey(scancode, false);
});

let lastTime = 0;
let frameCount = 0;

function updateFrame() {
    fetch('/frame')
        .then(r => r.arrayBuffer())
        .then(data => {
            const view = new DataView(data);
            const width = view.getUint16(0, true);
            const height = view.getUint16(2, true);
            const frameData = new Uint8Array(data, 4);

            canvas.width = width;
            canvas.height = height;

            const imageData = ctx.createImageData(width, height);
            // ARGB -> RGBA
            for (let i = 0; i < width * height; i++) {
                const offset = i * 4;
                imageData.data[offset + 0] = frameData[offset + 2]; // R
                imageData.data[offset + 1] = frameData[offset + 1]; // G
                imageData.data[offset + 2] = frameData[offset + 0]; // B
                imageData.data[offset + 3] = frameData[offset + 3]; // A
            }
            ctx.putImageData(imageData, 0, 0);

            // FPS 计算
            frameCount++;
            const now = performance.now();
            if (now - lastTime >= 1000) {
                fpsSpan.textContent = frameCount;
                frameCount = 0;
                lastTime = now;
            }
        })
        .catch(err => console.error('Frame error:', err));

    // OCR 结果
    fetch('/ocr')
        .then(r => r.json())
        .then(data => {
            if (data.text) {
                ocrText.textContent = data.text;
            }
        })
        .catch(() => {});

    requestAnimationFrame(updateFrame);
}

updateFrame();
    </script>
</body>
</html>
)";

WebRemoteServer::WebRemoteServer()
    : m_running(false)
    , m_port(8080)
    , m_socket(-1)
{
}

WebRemoteServer::~WebRemoteServer() {
    Stop();
}

bool WebRemoteServer::Start(int port, FrameGetter frame_getter, InputCallback input_callback) {
    m_port = port;
    m_frameGetter = frame_getter;
    m_inputCallback = input_callback;

    // 创建 socket
    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket < 0) {
        std::cerr << "Error: Failed to create socket" << std::endl;
        return false;
    }

    // 设置非阻塞
    int flags = fcntl(m_socket, F_GETFL, 0);
    fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);

    // 绑定端口
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(m_port);

    if (bind(m_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Error: Failed to bind port " << m_port << std::endl;
        close(m_socket);
        return false;
    }

    // 开始监听
    if (listen(m_socket, 5) < 0) {
        std::cerr << "Error: Failed to listen" << std::endl;
        close(m_socket);
        return false;
    }

    m_running = true;
    m_thread = std::thread(&WebRemoteServer::ServerLoop, this);

    std::cout << "Web Remote Server started at http://localhost:" << m_port << std::endl;
    std::cout << "Open this URL in your browser to control DOSBox-X" << std::endl;

    return true;
}

void WebRemoteServer::Stop() {
    m_running = false;

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_socket >= 0) {
        close(m_socket);
        m_socket = -1;
    }
}

bool WebRemoteServer::IsRunning() const {
    return m_running;
}

std::string WebRemoteServer::GetURL() const {
    return "http://localhost:" + std::to_string(m_port);
}

void WebRemoteServer::ServerLoop() {
    while (m_running) {
        // 接受新连接
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(m_socket, (struct sockaddr*)&client_addr, &client_len);

        if (client_socket >= 0) {
            // 处理请求
            HandleRequest(client_socket);
            close(client_socket);
        }

        // 短暂休眠避免占用 CPU
        usleep(10000);  // 10ms
    }
}

void WebRemoteServer::HandleRequest(int client_socket) {
    // 读取请求
    char buffer[4096];
    ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

    if (bytes_read <= 0) {
        return;
    }

    buffer[bytes_read] = '\0';

    // 解析请求
    std::string request(buffer);
    std::string method, path;

    // 获取第一行
    size_t first_space = request.find(' ');
    if (first_space != std::string::npos) {
        method = request.substr(0, first_space);
        size_t second_space = request.find(' ', first_space + 1);
        if (second_space != std::string::npos) {
            path = request.substr(first_space + 1, second_space - first_space - 1);
        }
    }

    std::string response;
    std::string content_type = "text/html";

    if (path == "/" || path == "/index.html") {
        // 返回 HTML 页面
        response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n";
        response += HTML_PAGE;

    } else if (path == "/frame") {
        // 返回帧数据
        uint16_t width, height;
        auto frame_data = m_frameGetter(width, height);

        if (!frame_data.empty()) {
            // 构建响应：前 4 字节是宽高，后面是 ARGB 数据
            std::vector<uint8_t> response_data;
            response_data.resize(4 + frame_data.size());

            // 写入宽高
            response_data[0] = width & 0xFF;
            response_data[1] = (width >> 8) & 0xFF;
            response_data[2] = height & 0xFF;
            response_data[3] = (height >> 8) & 0xFF;

            // 写入帧数据
            std::memcpy(response_data.data() + 4, frame_data.data(), frame_data.size());

            // 发送响应头
            std::string header = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                "Content-Length: " + std::to_string(response_data.size()) + "\r\n\r\n";
            send(client_socket, header.c_str(), header.size(), 0);
            send(client_socket, response_data.data(), response_data.size(), 0);
            return;
        } else {
            response = "HTTP/1.1 503 Service Unavailable\r\n\r\nNo frame data";
        }

    } else if (path == "/input" && method == "POST") {
        // 处理输入
        // 找到 JSON body
        size_t body_start = request.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            std::string body = request.substr(body_start + 4);

            // 解析 JSON (简单解析)
            uint32_t key_states[8] = {0};

            // 查找 keys 数组
            size_t keys_pos = body.find("\"keys\"");
            if (keys_pos != std::string::npos) {
                size_t array_start = body.find('[', keys_pos);
                size_t array_end = body.find(']', array_start);

                if (array_start != std::string::npos && array_end != std::string::npos) {
                    std::string array_content = body.substr(array_start + 1, array_end - array_start - 1);

                    // 解析数字
                    size_t pos = 0;
                    int index = 0;
                    while (pos < array_content.size() && index < 8) {
                        // 找到数字
                        while (pos < array_content.size() && (array_content[pos] == ' ' || array_content[pos] == ',' || array_content[pos] == '\n')) {
                            pos++;
                        }

                        if (pos < array_content.size() && (array_content[pos] >= '0' && array_content[pos] <= '9')) {
                            key_states[index] = std::stoul(array_content.substr(pos));
                            index++;
                            // 跳过数字
                            while (pos < array_content.size() && array_content[pos] >= '0' && array_content[pos] <= '9') {
                                pos++;
                            }
                        } else {
                            pos++;
                        }
                    }
                }
            }

            // 调用输入回调
            if (m_inputCallback) {
                m_inputCallback(key_states, 0, 0, 0);
            }
        }

        response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"status\":\"ok\"}";

    } else if (path == "/ocr") {
        // 返回 OCR 结果（简单返回）
        response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"text\":\"\"}";

    } else {
        response = "HTTP/1.1 404 Not Found\r\n\r\nNot found";
    }

    send(client_socket, response.c_str(), response.size(), 0);
}