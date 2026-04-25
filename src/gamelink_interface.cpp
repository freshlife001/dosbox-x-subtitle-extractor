#include "../include/gamelink_interface.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <thread>
#include <atomic>
#include <iconv.h>

// 外部全局运行标志
extern std::atomic<bool> g_running;

// 调试开关
static bool g_scan_debug = false;

void SetScanDebug(bool debug) {
    g_scan_debug = debug;
}

// Shift-JIS 到 UTF-8 转换函数
std::string ShiftJISToUTF8(const std::string& input) {
    if (input.empty()) {
        return input;
    }

    // 初始化 iconv
    iconv_t cd = iconv_open("UTF-8//IGNORE", "SHIFT_JIS");
    if (cd == (iconv_t)-1) {
        // 如果 SHIFT_JIS 不行，尝试 CP932
        cd = iconv_open("UTF-8//IGNORE", "CP932");
        if (cd == (iconv_t)-1) {
            // 最后尝试不忽略错误
            cd = iconv_open("UTF-8", "SHIFT_JIS");
            if (cd == (iconv_t)-1) {
                std::cerr << "Warning: iconv_open failed for Shift-JIS" << std::endl;
                return input;
            }
        }
    }

    // 准备输入输出缓冲区
    char* inbuf = const_cast<char*>(input.data());
    size_t inbytesleft = input.size();

    // UTF-8 输出最多是输入的 4 倍
    size_t outbufsize = inbytesleft * 4;
    std::vector<char> outbuf(outbufsize);
    char* outbufptr = outbuf.data();
    size_t outbytesleft = outbufsize;

    // 执行转换
    size_t result = iconv(cd, &inbuf, &inbytesleft, &outbufptr, &outbytesleft);

    // 重置 iconv 状态（处理多字节序列的残留）
    iconv(cd, nullptr, nullptr, &outbufptr, &outbytesleft);

    // 关闭 iconv
    iconv_close(cd);

    // 返回转换后的字符串
    size_t converted_len = outbufsize - outbytesleft;
    return std::string(outbuf.data(), converted_len);
}

// UTF-8 到 Shift-JIS 转换函数（用于搜索日文文本）
std::string UTF8ToShiftJIS(const std::string& input) {
    if (input.empty()) {
        return input;
    }

    // 初始化 iconv
    iconv_t cd = iconv_open("SHIFT_JIS", "UTF-8");
    if (cd == (iconv_t)-1) {
        cd = iconv_open("CP932", "UTF-8");
        if (cd == (iconv_t)-1) {
            std::cerr << "Warning: iconv_open failed for UTF-8 to Shift-JIS" << std::endl;
            return input;
        }
    }

    // 准备输入输出缓冲区
    char* inbuf = const_cast<char*>(input.data());
    size_t inbytesleft = input.size();

    // Shift-JIS 输出最多是输入的 2 倍
    size_t outbufsize = inbytesleft * 2 + 1;
    std::vector<char> outbuf(outbufsize);
    char* outbufptr = outbuf.data();
    size_t outbytesleft = outbufsize;

    // 执行转换
    iconv(cd, &inbuf, &inbytesleft, &outbufptr, &outbytesleft);
    iconv(cd, nullptr, nullptr, &outbufptr, &outbytesleft);

    iconv_close(cd);

    size_t converted_len = outbufsize - outbytesleft;
    return std::string(outbuf.data(), converted_len);
}

#ifndef _WIN32
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <unistd.h>
#endif

namespace GameLink {

GameLinkInterface::GameLinkInterface()
    : m_pSharedMemory(nullptr)
#ifdef _WIN32
    , m_hMutex(NULL), m_hMapFile(NULL)
#else
    , m_fdMutex(SEM_FAILED), m_fdMapFile(-1)
#endif
{
}

GameLinkInterface::~GameLinkInterface() {
    Shutdown();
}

bool GameLinkInterface::Initialize() {
#ifdef _WIN32
    const char* MUTEX_NAME = "DWD_GAMELINK_MUTEX_R4";
    const char* MMAP_NAME = "DWD_GAMELINK_MMAP_R4";
    
    // 打开互斥体
    m_hMutex = OpenMutexA(SYNCHRONIZE, FALSE, MUTEX_NAME);
    if (m_hMutex == NULL) {
        std::cerr << "Failed to open Game Link mutex" << std::endl;
        return false;
    }
    
    // 打开文件映射
    m_hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, MMAP_NAME);
    if (m_hMapFile == NULL) {
        CloseHandle(m_hMutex);
        m_hMutex = NULL;
        std::cerr << "Failed to open Game Link shared memory" << std::endl;
        return false;
    }
    
    // 映射到内存
    m_pSharedMemory = reinterpret_cast<sSharedMemoryMap_R4*>(
        MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0)
    );
    
    if (m_pSharedMemory == NULL) {
        CloseHandle(m_hMapFile);
        CloseHandle(m_hMutex);
        m_hMapFile = NULL;
        m_hMutex = NULL;
        std::cerr << "Failed to map Game Link shared memory" << std::endl;
        return false;
    }
    
#else  // Linux/macOS
    const char* MUTEX_NAME = "/DWD_GAMELINK_MUTEX_R4";
#ifdef __APPLE__
    const char* MMAP_NAME = "/DWD_GAMELINK_MMAP_R4";
#else
    const char* MMAP_NAME = "DWD_GAMELINK_MMAP_R4";
#endif
    
    // 打开互斥体
    m_fdMutex = sem_open(MUTEX_NAME, 0, 0666, 0);
    if (m_fdMutex == SEM_FAILED) {
        std::cerr << "Failed to open Game Link mutex: " << MUTEX_NAME << std::endl;
        return false;
    }
    
    // 打开共享内存
    m_fdMapFile = shm_open(MMAP_NAME, O_RDWR, 0666);
    if (m_fdMapFile < 0) {
        sem_close(m_fdMutex);
        m_fdMutex = SEM_FAILED;
        std::cerr << "Failed to open Game Link shared memory: " << MMAP_NAME << std::endl;
        return false;
    }
    
    // 获取大小并映射
    const size_t mmap_size = sizeof(sSharedMemoryMap_R4) + 16 * 1024 * 1024; // 粗略估计
    m_pSharedMemory = reinterpret_cast<sSharedMemoryMap_R4*>(
        mmap(nullptr, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fdMapFile, 0)
    );
    
    if (m_pSharedMemory == MAP_FAILED) {
        close(m_fdMapFile);
        m_fdMapFile = -1;
        sem_close(m_fdMutex);
        m_fdMutex = SEM_FAILED;
        std::cerr << "Failed to map Game Link shared memory" << std::endl;
        return false;
    }
#endif
    
    std::cout << "Game Link initialized successfully" << std::endl;
    std::cout << "  System: " << m_pSharedMemory->system << std::endl;
    std::cout << "  Program: " << m_pSharedMemory->program << std::endl;
    std::cout << "  RAM: " << (m_pSharedMemory->ram_size / (1024 * 1024)) << " MB" << std::endl;
    
    return true;
}

void GameLinkInterface::Shutdown() {
#ifdef _WIN32
    if (m_pSharedMemory) {
        UnmapViewOfFile(m_pSharedMemory);
        m_pSharedMemory = nullptr;
    }
    if (m_hMapFile) {
        CloseHandle(m_hMapFile);
        m_hMapFile = NULL;
    }
    if (m_hMutex) {
        CloseHandle(m_hMutex);
        m_hMutex = NULL;
    }
#else
    if (m_pSharedMemory && m_pSharedMemory != MAP_FAILED) {
        munmap(m_pSharedMemory, sizeof(sSharedMemoryMap_R4) + 16 * 1024 * 1024);
        m_pSharedMemory = nullptr;
    }
    if (m_fdMapFile >= 0) {
        close(m_fdMapFile);
        m_fdMapFile = -1;
    }
    if (m_fdMutex != SEM_FAILED) {
        sem_close(m_fdMutex);
        m_fdMutex = SEM_FAILED;
    }
#endif
}

bool GameLinkInterface::IsConnected() const {
    return m_pSharedMemory != nullptr;
}

void GameLinkInterface::AddMonitorAddress(uint32_t address) {
    m_monitoredAddresses.push_back(address);
}

void GameLinkInterface::ClearMonitorAddresses() {
    m_monitoredAddresses.clear();
}

std::vector<uint8_t> GameLinkInterface::ReadMonitoredData(uint32_t timeout_ms) {
    std::vector<uint8_t> result;
    
    if (!IsConnected()) {
        return result;
    }
    
    if (!AcquireMutex(timeout_ms)) {
        return result;
    }
    
    // 设置要读取的地址
    m_pSharedMemory->peek.addr_count = std::min(
        static_cast<uint32_t>(m_monitoredAddresses.size()),
        static_cast<uint32_t>(sSharedMMapPeek_R2::PEEK_LIMIT)
    );
    
    for (uint32_t i = 0; i < m_pSharedMemory->peek.addr_count; ++i) {
        m_pSharedMemory->peek.addr[i] = m_monitoredAddresses[i];
    }
    
    ReleaseMutex();
    
    // 等待 DOSBox-X 填充数据
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    
    if (!AcquireMutex(timeout_ms)) {
        return result;
    }
    
    // 读取结果
    result.resize(m_pSharedMemory->peek.addr_count);
    for (uint32_t i = 0; i < m_pSharedMemory->peek.addr_count; ++i) {
        result[i] = m_pSharedMemory->peek.data[i];
    }
    
    ReleaseMutex();
    
    return result;
}

std::string GameLinkInterface::GetGameName() const {
    if (!IsConnected()) return "";
    return m_pSharedMemory->program;
}

std::string GameLinkInterface::GetSystemName() const {
    if (!IsConnected()) return "";
    return m_pSharedMemory->system;
}

bool GameLinkInterface::IsPaused() const {
    if (!IsConnected()) return false;
    return (m_pSharedMemory->flags & sSharedMemoryMap_R4::FLAG_PAUSED) != 0;
}

uint32_t GameLinkInterface::GetRAMSize() const {
    if (!IsConnected()) return 0;
    return m_pSharedMemory->ram_size;
}

const uint8_t* GameLinkInterface::GetFrameBuffer(uint16_t& width, uint16_t& height) const {
    if (!IsConnected()) {
        width = height = 0;
        return nullptr;
    }

    width = m_pSharedMemory->frame.width;
    height = m_pSharedMemory->frame.height;

    if (m_pSharedMemory->frame.image_fmt == 0) {
        return nullptr;  // No frame available
    }

    return m_pSharedMemory->frame.buffer;
}

std::vector<uint8_t> GameLinkInterface::GetFrameBufferData(uint16_t& width, uint16_t& height) {
    std::vector<uint8_t> result;

    if (!IsConnected()) {
        width = height = 0;
        return result;
    }

    width = m_pSharedMemory->frame.width;
    height = m_pSharedMemory->frame.height;

    if (m_pSharedMemory->frame.image_fmt == 0 || width == 0 || height == 0) {
        return result;  // No frame available
    }

    // 原始格式是 0xAARRGGBB (ARGB)，转换为 BGRA 用于 Vision OCR
    size_t frame_size = width * height * 4;
    result.resize(frame_size);

    const uint8_t* src = m_pSharedMemory->frame.buffer;

    // ARGB -> BGRA 转换
    for (size_t i = 0; i < width * height; ++i) {
        size_t offset = i * 4;
        // src: A R G B -> result: B G R A
        result[offset + 0] = src[offset + 3];  // B
        result[offset + 1] = src[offset + 2];  // G
        result[offset + 2] = src[offset + 1];  // R
        result[offset + 3] = src[offset + 0];  // A
    }

    return result;
}

bool GameLinkInterface::AcquireMutex(uint32_t timeout_ms) {
#ifdef _WIN32
    DWORD result = WaitForSingleObject(m_hMutex, timeout_ms == 0 ? INFINITE : timeout_ms);
    return result == WAIT_OBJECT_0;
#else
    // TODO: Implement timeout for POSIX semaphore
    int result = sem_wait(m_fdMutex);
    return result == 0;
#endif
}

void GameLinkInterface::ReleaseMutex() {
#ifdef _WIN32
    ReleaseMutex(m_hMutex);
#else
    sem_post(m_fdMutex);
#endif
}

// ============================================================================
// 内存扫描功能
// ============================================================================

std::vector<uint8_t> GameLinkInterface::ReadMemoryBlock(uint32_t start_addr, size_t length, uint32_t offset) {
    std::vector<uint8_t> result;

    if (!IsConnected() || length == 0) {
        return result;
    }

    size_t read_size = std::min(length, static_cast<size_t>(sSharedMMapPeek_R2::PEEK_LIMIT));

    if (!AcquireMutex(500)) {
        std::cerr << "Warning: Failed to acquire mutex for ReadMemoryBlock" << std::endl;
        return result;
    }

    // 设置要读取的地址（每个地址读取一个字节）
    // 地址 = 请求地址 + 偏移量（load address）
    m_pSharedMemory->peek.addr_count = static_cast<uint32_t>(read_size);

    for (size_t i = 0; i < read_size; ++i) {
        m_pSharedMemory->peek.addr[i] = start_addr + i + offset;
    }

    ReleaseMutex();

    // 等待 DOSBox-X 响应并填充数据
    // DOSBox-X 应该在检测到 addr_count > 0 时执行读取
    // 等待时间取决于 DOSBox-X 的帧率，通常 16-33ms
    for (int retry = 0; retry < 10; ++retry) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        if (!AcquireMutex(100)) {
            continue;
        }

        // 检查数据是否已填充（addr_count 应该被 DOSBox-X 清零或保持）
        // 直接读取 data 数组
        result.resize(read_size);
        for (size_t i = 0; i < read_size; ++i) {
            result[i] = m_pSharedMemory->peek.data[i];
        }

        ReleaseMutex();

        // 检查是否读取到了有效数据（非全零）
        bool has_data = false;
        for (size_t i = 0; i < std::min(read_size, static_cast<size_t>(100)); ++i) {
            if (result[i] != 0 && result[i] != 0xFF) {
                has_data = true;
                break;
            }
        }

        if (has_data || retry >= 5) {
            if (g_scan_debug) {
                std::cout << "  [DEBUG] Read block 0x" << std::hex << start_addr << std::dec
                          << " retry=" << retry << " has_data=" << has_data << std::endl;
                // 显示前 32 字节的原始数据
                std::cout << "  [DEBUG] First 32 bytes: ";
                for (size_t i = 0; i < std::min(read_size, static_cast<size_t>(32)); ++i) {
                    std::cout << std::hex << static_cast<int>(result[i]) << " ";
                }
                std::cout << std::dec << std::endl;
            }
            break;
        }
    }

    return result;
}

std::vector<std::pair<uint32_t, std::string>> GameLinkInterface::ScanMemoryRange(
    uint32_t start_addr,
    uint32_t end_addr,
    size_t min_length,
    const std::string& charset,
    const std::string& search_text,
    uint32_t offset
) {
    std::vector<std::pair<uint32_t, std::string>> results;

    if (!IsConnected() || start_addr >= end_addr) {
        return results;
    }

    // 每次读取的块大小
    const size_t BLOCK_SIZE = sSharedMMapPeek_R2::PEEK_LIMIT;
    uint32_t total_blocks = (end_addr - start_addr) / BLOCK_SIZE + 1;

    // 如果指定了搜索文本，转换为 Shift-JIS 进行搜索
    std::string search_pattern;
    if (!search_text.empty()) {
        search_pattern = UTF8ToShiftJIS(search_text);
        std::cout << "Searching for text: \"" << search_text << "\""
                  << " (Shift-JIS: " << search_pattern.size() << " bytes)" << std::endl;
    }

    std::cout << "Scanning memory range: 0x" << std::hex << start_addr
              << " - 0x" << end_addr << std::dec
              << " (offset=0x" << std::hex << offset << std::dec
              << ", " << total_blocks << " blocks, charset=" << charset << ")" << std::endl;

    // Shift-JIS 日文字符检测辅助函数
    auto isShiftJISFirstByte = [](uint8_t ch) {
        return (ch >= 0x81 && ch <= 0x9F) || (ch >= 0xE0 && ch <= 0xFC);
    };

    auto isShiftJISSecondByte = [](uint8_t ch) {
        return (ch >= 0x40 && ch <= 0x7E) || (ch >= 0x80 && ch <= 0xFC);
    };

    auto isHalfWidthKatakana = [](uint8_t ch) {
        return ch >= 0xA1 && ch <= 0xDF;
    };

    for (uint32_t block = 0; block < total_blocks && g_running; ++block) {
        uint32_t addr = start_addr + block * BLOCK_SIZE;
        size_t read_len = std::min(static_cast<size_t>(end_addr - addr), BLOCK_SIZE);

        auto data = ReadMemoryBlock(addr, read_len, offset);

        // 显示进度
        if (block % 10 == 0) {
            std::cout << "  Progress: " << (block * 100 / total_blocks) << "%"
                      << " (block " << block << "/" << total_blocks << ")" << std::endl;
        }

        // 扫描数据中的字符串
        size_t i = 0;
        while (i < data.size()) {
            size_t str_start = i;
            size_t str_len = 0;
            bool has_japanese = false;
            bool has_ascii_letters = false;

            // 尝试从当前位置开始解析字符串
            while (i < data.size()) {
                uint8_t ch = data[i];

                if (charset == "japanese") {
                    // 日文模式：检测 Shift-JIS 编码
                    if (isShiftJISFirstByte(ch) && i + 1 < data.size() && isShiftJISSecondByte(data[i + 1])) {
                        // 双字节日文字符（汉字、平假名、片假名）
                        has_japanese = true;
                        str_len += 2;
                        i += 2;
                    } else if (isHalfWidthKatakana(ch)) {
                        // 半角片假名
                        has_japanese = true;
                        str_len++;
                        i++;
                    } else if (ch >= 0x20 && ch <= 0x7E) {
                        // ASCII 字符（可以作为日文的一部分）
                        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
                            has_ascii_letters = true;
                        }
                        str_len++;
                        i++;
                    } else {
                        // 非可打印字符，字符串结束
                        break;
                    }
                } else if (charset == "ascii") {
                    // ASCII 模式：只检测纯 ASCII 字母字符串
                    if (ch >= 0x20 && ch <= 0x7E) {
                        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
                            has_ascii_letters = true;
                        }
                        str_len++;
                        i++;
                    } else {
                        break;
                    }
                } else {
                    // all 模式：检测所有可打印字符
                    if ((ch >= 0x20 && ch <= 0x7E) || (ch >= 0x80 && ch <= 0xFF)) {
                        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
                            has_ascii_letters = true;
                        }
                        if (isShiftJISFirstByte(ch) || isHalfWidthKatakana(ch)) {
                            has_japanese = true;
                        }
                        str_len++;
                        i++;
                    } else {
                        break;
                    }
                }
            }

            // 检查找到的字符串是否符合条件
            if (str_len >= min_length) {
                std::string raw_text(reinterpret_cast<const char*>(data.data() + str_start), str_len);

                // 如果是日文模式，转换为 UTF-8
                std::string display_text = (has_japanese && charset == "japanese")
                    ? ShiftJISToUTF8(raw_text)
                    : raw_text;

                bool valid = false;
                if (charset == "japanese") {
                    // 日文模式：必须包含日文字符
                    valid = has_japanese;
                } else if (charset == "ascii") {
                    // ASCII 模式：必须包含字母
                    valid = has_ascii_letters;
                } else {
                    // all 模式：任何有效字符串
                    valid = has_ascii_letters || has_japanese;
                }

                // 如果指定了搜索文本，检查是否包含
                if (valid && !search_pattern.empty()) {
                    // 在原始 Shift-JIS 数据中搜索
                    if (raw_text.find(search_pattern) == std::string::npos) {
                        valid = false;  // 不包含搜索文本，跳过
                    }
                }

                if (valid) {
                    uint32_t found_addr = addr + str_start;
                    // 存储 UTF-8 转换后的文本
                    results.push_back({found_addr, display_text});

                    if (g_scan_debug) {
                        std::cout << "  [FOUND] 0x" << std::hex << found_addr << std::dec
                                  << ": \"" << display_text << "\" (jp=" << has_japanese
                                  << ", ascii=" << has_ascii_letters << ")" << std::endl;
                    }
                }
            }

            // 移动到下一个位置
            if (str_len == 0) {
                i++;
            }
        }
    }

    return results;
}

}  // namespace GameLink
