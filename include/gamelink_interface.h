#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#ifdef _WIN32
#include <Windows.h>
#else
#include <semaphore.h>
#endif

namespace GameLink {

// Game Link 协议版本
constexpr uint8_t PROTOCOL_VER = 4;

// ============================================================================
// 数据结构定义 (来自 DOSBox-X src/gamelink/gamelink.h)
// ============================================================================

#pragma pack(push, 1)

// 视频帧结构 (Server -> Client)
struct sSharedMMapFrame_R1 {
    uint16_t seq;
    uint16_t width;
    uint16_t height;
    
    uint8_t image_fmt;  // 0 = no frame; 1 = 32-bit 0xAARRGGBB
    uint8_t reserved0;
    
    uint16_t par_x;     // pixel aspect ratio
    uint16_t par_y;
    
    enum { MAX_WIDTH = 1280 };
    enum { MAX_HEIGHT = 1024 };
    enum { MAX_PAYLOAD = MAX_WIDTH * MAX_HEIGHT * 4 };
    
    uint8_t buffer[MAX_PAYLOAD];
};

// 输入结构 (Client -> Server)
struct sSharedMMapInput_R2 {
    float mouse_dx;
    float mouse_dy;
    uint8_t ready;
    uint8_t mouse_btn;
    uint32_t keyb_state[8];
};

// 内存读取接口 <- 关键: 用于字幕提取
struct sSharedMMapPeek_R2 {
    enum { PEEK_LIMIT = 16 * 1024 };
    
    uint32_t addr_count;              // 要读取的地址数量
    uint32_t addr[PEEK_LIMIT];        // 地址列表
    uint8_t data[PEEK_LIMIT];         // 读取的数据结果
};

// 通用消息缓冲区
struct sSharedMMapBuffer_R1 {
    enum { BUFFER_SIZE = (64 * 1024) };
    
    uint16_t payload;
    uint8_t data[BUFFER_SIZE];
};

// 音频控制接口
struct sSharedMMapAudio_R1 {
    uint8_t master_vol_l;
    uint8_t master_vol_r;
};

// 共享内存映射 (顶层对象)
struct sSharedMemoryMap_R4 {
    enum {
        FLAG_WANT_KEYB = 1 << 0,
        FLAG_WANT_MOUSE = 1 << 1,
        FLAG_NO_FRAME = 1 << 2,
        FLAG_PAUSED = 1 << 3,
    };
    
    enum {
        SYSTEM_MAXLEN = 64,
        PROGRAM_MAXLEN = 260
    };
    
    uint8_t version;                        // PROTOCOL_VER
    uint8_t flags;
    char system[SYSTEM_MAXLEN];             // System name
    char program[PROGRAM_MAXLEN];           // Program name (zero-terminated)
    uint32_t program_hash[4];               // Program code hash (256-bits)
    
    sSharedMMapFrame_R1 frame;
    sSharedMMapInput_R2 input;
    sSharedMMapPeek_R2 peek;                // <- 字幕提取使用此接口
    sSharedMMapBuffer_R1 buf_recv;          // Message to us
    sSharedMMapBuffer_R1 buf_tohost;        // Message from us
    sSharedMMapAudio_R1 audio;
    
    uint32_t ram_size;
};

#pragma pack(pop)

// ============================================================================
// Game Link 接口类
// ============================================================================

class GameLinkInterface {
public:
    GameLinkInterface();
    ~GameLinkInterface();
    
    /// 初始化 Game Link 连接
    /// @return 成功返回 true
    bool Initialize();
    
    /// 清理资源
    void Shutdown();
    
    /// 是否已连接到 Game Link
    bool IsConnected() const;
    
    /// 添加要监控的内存地址
    /// @param address DOS 物理地址
    void AddMonitorAddress(uint32_t address);
    
    /// 清空所有监控地址
    void ClearMonitorAddresses();
    
    /// 读取监控地址的数据
    /// @param timeout_ms 超时时间(毫秒)
    /// @return 读取的数据向量
    std::vector<uint8_t> ReadMonitoredData(uint32_t timeout_ms = 16);
    
    /// 获取游戏信息
    std::string GetGameName() const;
    std::string GetSystemName() const;
    
    /// 获取运行状态
    bool IsPaused() const;
    
    /// 获取 RAM 大小
    uint32_t GetRAMSize() const;
    
    /// 获取当前帧
    const uint8_t* GetFrameBuffer(uint16_t& width, uint16_t& height) const;

    /// 扫描内存范围，查找可打印字符串
    /// @param start_addr 起始地址
    /// @param end_addr 结束地址
    /// @param min_length 最小字符串长度
    /// @param charset 字符集过滤: "japanese", "ascii", "all"
    /// @param search_text 搜索特定文本（可选，UTF-8输入会被转换为Shift-JIS搜索）
    /// @return 找到的字符串列表 (地址, 内容)
    std::vector<std::pair<uint32_t, std::string>> ScanMemoryRange(
        uint32_t start_addr,
        uint32_t end_addr,
        size_t min_length = 4,
        const std::string& charset = "japanese",
        const std::string& search_text = ""
    );

    /// 读取指定地址的内存块
    /// @param start_addr 起始地址
    /// @param length 读取长度
    /// @return 读取的数据
    std::vector<uint8_t> ReadMemoryBlock(uint32_t start_addr, size_t length);

    /// 获取共享内存指针 (用于直接访问)
    sSharedMemoryMap_R4* GetSharedMemory() { return m_pSharedMemory; }
    
private:
    sSharedMemoryMap_R4* m_pSharedMemory;
    std::vector<uint32_t> m_monitoredAddresses;
    
#ifdef _WIN32
    HANDLE m_hMutex;
    HANDLE m_hMapFile;
#else
    sem_t* m_fdMutex;
    int m_fdMapFile;
#endif
    
    bool AcquireMutex(uint32_t timeout_ms);
    void ReleaseMutex();
};

} // namespace GameLink

// 全局调试开关
void SetScanDebug(bool debug);

// Shift-JIS 到 UTF-8 转换函数
std::string ShiftJISToUTF8(const std::string& input);

// UTF-8 到 Shift-JIS 转换函数（用于搜索）
std::string UTF8ToShiftJIS(const std::string& input);
