// Web Remote Server - HTTP server for browser-based remote control

#include "web_server.h"
#include <iostream>
#include <cstring>
#include <sstream>
#include <fstream>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>

// 加载 HTML 文件内容
static std::string LoadHTMLFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Warning: Failed to load HTML file: " << filepath << std::endl;
        return "<html><body><h1>HTML file not found</h1><p>Please check resources/web_remote.html</p></body></html>";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// 简单 JSON 解析：提取数字值
static int ParseJsonInt(const std::string& json, const std::string& key) {
    size_t key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return -1;

    size_t colon_pos = json.find(':', key_pos);
    if (colon_pos == std::string::npos) return -1;

    // 跳过空白
    size_t num_start = colon_pos + 1;
    while (num_start < json.size() && (json[num_start] == ' ' || json[num_start] == '\t')) {
        num_start++;
    }

    // 解析数字
    if (num_start < json.size() && (json[num_start] >= '0' && json[num_start] <= '9')) {
        return std::stoi(json.substr(num_start));
    }

    return -1;
}

WebRemoteServer::WebRemoteServer()
    : m_running(false)
    , m_port(8080)
    , m_socket(-1)
{
}

WebRemoteServer::~WebRemoteServer() {
    Stop();
}

bool WebRemoteServer::Start(int port, FrameGetter frame_getter, InputCallback input_callback,
                              OCRGetter ocr_getter, OCRRegionSetter ocr_region_setter) {
    // 忽略 SIGPIPE 信号（防止客户端断开时崩溃）
    signal(SIGPIPE, SIG_IGN);

    m_port = port;
    m_frameGetter = frame_getter;
    m_inputCallback = input_callback;
    m_ocrGetter = ocr_getter;
    m_ocrRegionSetter = ocr_region_setter;

    // 加载 HTML 文件
    m_htmlContent = LoadHTMLFile("resources/web_remote.html");

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
        // 使用 select 检查是否有连接请求，避免 CPU 占用过高
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000;  // 50ms timeout

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(m_socket, &read_fds);

        int ready = select(m_socket + 1, &read_fds, nullptr, nullptr, &tv);

        if (ready > 0 && FD_ISSET(m_socket, &read_fds)) {
            // 接受新连接
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_socket = accept(m_socket, (struct sockaddr*)&client_addr, &client_len);

            if (client_socket >= 0) {
                // 设置客户端 socket 为阻塞模式（确保 recv 能正常工作）
                int client_flags = fcntl(client_socket, F_GETFL, 0);
                fcntl(client_socket, F_SETFL, client_flags & ~O_NONBLOCK);

                // 处理请求
                HandleRequest(client_socket);
                close(client_socket);
            }
        }
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
        // 返回 HTML 页面（每次重新加载，确保获取最新版本）
        std::string htmlContent = LoadHTMLFile("resources/web_remote.html");
        response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n";
        response += htmlContent;

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
        size_t body_start = request.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            std::string body = request.substr(body_start + 4);

            // 解析 JSON (简单解析)
            uint32_t key_states[8] = {0};

            size_t keys_pos = body.find("\"keys\"");
            if (keys_pos != std::string::npos) {
                size_t array_start = body.find('[', keys_pos);
                size_t array_end = body.find(']', array_start);

                if (array_start != std::string::npos && array_end != std::string::npos) {
                    std::string array_content = body.substr(array_start + 1, array_end - array_start - 1);

                    size_t pos = 0;
                    int index = 0;
                    while (pos < array_content.size() && index < 8) {
                        while (pos < array_content.size() && (array_content[pos] == ' ' || array_content[pos] == ',' || array_content[pos] == '\n')) {
                            pos++;
                        }

                        if (pos < array_content.size() && (array_content[pos] >= '0' && array_content[pos] <= '9')) {
                            key_states[index] = std::stoul(array_content.substr(pos));
                            index++;
                            while (pos < array_content.size() && array_content[pos] >= '0' && array_content[pos] <= '9') {
                                pos++;
                            }
                        } else {
                            pos++;
                        }
                    }
                }
            }

            if (m_inputCallback) {
                m_inputCallback(key_states, 0, 0, 0);
            }
        }

        response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"status\":\"ok\"}";

    } else if (path == "/region" && method == "POST") {
        // 处理 OCR 区域设置
        size_t body_start = request.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            std::string body = request.substr(body_start + 4);

            // 解析区域参数
            int x = ParseJsonInt(body, "x");
            int y = ParseJsonInt(body, "y");
            int w = ParseJsonInt(body, "width");
            int h = ParseJsonInt(body, "height");

            bool valid = (x >= 0 && y >= 0 && w > 0 && h > 0);

            if (m_ocrRegionSetter) {
                m_ocrRegionSetter(x, y, w, h, valid);
            }

            if (valid) {
                std::cout << "[Region] OCR region set: (" << x << "," << y << ") " << w << "x" << h << std::endl;
            } else {
                std::cout << "[Region] OCR region cleared (full screen)" << std::endl;
            }
        }

        response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"status\":\"ok\"}";

    } else if (path == "/ocr") {
        // 返回 OCR 结果
        std::string ocr_text = "";
        if (m_ocrGetter) {
            ocr_text = m_ocrGetter();
        }

        // JSON 编码文本
        std::string json_text = "";
        for (char c : ocr_text) {
            if (c == '"') json_text += "\\\"";
            else if (c == '\\') json_text += "\\\\";
            else if (c == '\n') json_text += "\\n";
            else if (c == '\r') json_text += "";
            else json_text += c;
        }
        response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"text\":\"" + json_text + "\"}";

    } else {
        response = "HTTP/1.1 404 Not Found\r\n\r\nNot found";
    }

    send(client_socket, response.c_str(), response.size(), 0);
}