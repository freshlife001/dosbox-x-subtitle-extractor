// Ollama OCR - Cross-platform C++ implementation
// Uses standard sockets for HTTP API calls (Windows/Linux)

#include "ocr_bridge.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>
#include <vector>
#include <memory>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
    #define SOCKET_TYPE SOCKET
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #define CLOSE_SOCKET close
    #define SOCKET_TYPE int
    #define INVALID_SOCKET_VALUE -1
#endif

// Global Ollama configuration
static std::string g_ollama_model = "glm-ocr:latest";
static std::string g_ollama_url = "http://localhost:11434";

void SetOllamaConfig(const std::string& model, const std::string& url) {
    g_ollama_model = model;
    g_ollama_url = url;
}

std::string GetOllamaModel() { return g_ollama_model; }
std::string GetOllamaUrl() { return g_ollama_url; }

// Base64 encoding table
static const char* BASE64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const std::vector<uint8_t>& data) {
    std::string result;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    size_t in_len = data.size();
    const uint8_t* bytes_to_encode = data.data();

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                result += BASE64_CHARS[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (int j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (int j = 0; j < i + 1; j++)
            result += BASE64_CHARS[char_array_4[j]];

        while (i++ < 3)
            result += '=';
    }

    return result;
}

// Initialize Winsock (Windows only)
static void InitSockets() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

// Cleanup Winsock (Windows only)
static void CleanupSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// HTTP request to Ollama API
static std::string HttpPost(const std::string& host, int port, const std::string& path,
                            const std::string& body, int timeout_sec = 60) {
    InitSockets();

    std::string response;

    // Resolve hostname
    struct hostent* he = gethostbyname(host.c_str());
    if (!he) {
        std::cerr << "Failed to resolve hostname: " << host << std::endl;
        CleanupSockets();
        return "";
    }

    // Create socket
    SOCKET_TYPE sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET_VALUE) {
        std::cerr << "Failed to create socket" << std::endl;
        CleanupSockets();
        return "";
    }

    // Set timeout
#ifdef _WIN32
    DWORD tv = timeout_sec * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    // Connect
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr = *((struct in_addr*)he->h_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to connect to " << host << ":" << port << std::endl;
        CLOSE_SOCKET(sock);
        CleanupSockets();
        return "";
    }

    // Build HTTP request
    std::ostringstream request;
    request << "POST " << path << " HTTP/1.1\r\n";
    request << "Host: " << host << "\r\n";
    request << "Content-Type: application/json\r\n";
    request << "Content-Length: " << body.length() << "\r\n";
    request << "Connection: close\r\n";
    request << "\r\n";
    request << body;

    // Send request
    std::string req_str = request.str();
    if (send(sock, req_str.c_str(), req_str.length(), 0) < 0) {
        std::cerr << "Failed to send request" << std::endl;
        CLOSE_SOCKET(sock);
        CleanupSockets();
        return "";
    }

    // Receive response
    char buffer[4096];
    int received;
    while ((received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[received] = '\0';
        response += buffer;
    }

    CLOSE_SOCKET(sock);
    CleanupSockets();

    // Parse HTTP response - find body after headers
    size_t body_start = response.find("\r\n\r\n");
    if (body_start != std::string::npos) {
        return response.substr(body_start + 4);
    }

    return "";
}

// Parse JSON to extract "response" field
static std::string ParseJsonResponse(const std::string& json) {
    // Simple JSON parsing - find "response":"..."
    size_t key_pos = json.find("\"response\"");
    if (key_pos == std::string::npos) {
        return "";
    }

    size_t colon_pos = json.find(':', key_pos);
    if (colon_pos == std::string::npos) {
        return "";
    }

    // Skip whitespace
    size_t quote_start = colon_pos + 1;
    while (quote_start < json.size() && (json[quote_start] == ' ' || json[quote_start] == '\t')) {
        quote_start++;
    }

    // Find opening quote
    if (quote_start >= json.size() || json[quote_start] != '"') {
        return "";
    }
    quote_start++;

    // Find closing quote (handle escaped quotes)
    std::string result;
    size_t pos = quote_start;
    while (pos < json.size()) {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            // Escape sequence
            pos++;
            if (json[pos] == 'n') result += '\n';
            else if (json[pos] == 'r') result += '\r';
            else if (json[pos] == 't') result += '\t';
            else result += json[pos];
            pos++;
        } else if (json[pos] == '"') {
            break;
        } else {
            result += json[pos];
            pos++;
        }
    }

    return result;
}

// Convert BGRA to PNG using OpenCV
static std::vector<uint8_t> BGRAtoPNG(const uint8_t* bgra_data, int width, int height) {
    // Create OpenCV Mat from BGRA data
    cv::Mat bgra(height, width, CV_8UC4, const_cast<uint8_t*>(bgra_data));

    // Convert BGRA to BGR
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);

    // Encode to PNG
    std::vector<uint8_t> png_data;
    cv::imencode(".png", bgr, png_data);

    return png_data;
}

std::vector<OCRResult> PerformOllamaOCR(
    const uint8_t* image_data,
    int width,
    int height,
    const std::string& model_name,
    const std::string& ollama_url
) {
    std::vector<OCRResult> results;

    if (image_data == nullptr || width <= 0 || height <= 0) {
        return results;
    }

    // Use configured values if not specified
    std::string model = model_name.empty() ? g_ollama_model : model_name;
    std::string url = ollama_url.empty() ? g_ollama_url : ollama_url;

    std::cout << "[Ollama] Using model: " << model << " at " << url << std::endl;

    // Parse URL to extract host and port
    // Format: http://hostname:port
    std::string host = "localhost";
    int port = 11434;

    size_t http_pos = url.find("http://");
    if (http_pos != std::string::npos) {
        size_t host_start = http_pos + 7;
        size_t port_pos = url.find(':', host_start);
        if (port_pos != std::string::npos) {
            host = url.substr(host_start, port_pos - host_start);
            port = std::stoi(url.substr(port_pos + 1));
        } else {
            host = url.substr(host_start);
        }
    }

    // Convert BGRA to PNG and encode to base64
    std::vector<uint8_t> png_data = BGRAtoPNG(image_data, width, height);
    std::string base64_png = Base64Encode(png_data);

    // Build JSON request for /api/generate endpoint
    std::ostringstream json_body;
    json_body << "{\"model\":\"" << model << "\","
              << "\"prompt\":\"Text Recognition:\","
              << "\"images\":[\"" << base64_png << "\"],"
              << "\"stream\":false}";

    // Call Ollama API
    auto start = std::chrono::high_resolution_clock::now();
    std::string response = HttpPost(host, port, "/api/generate", json_body.str(), 60);
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "[Ollama] API call took " << ms << " ms" << std::endl;

    if (!response.empty()) {
        // Parse JSON response
        std::string ocr_text = ParseJsonResponse(response);

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