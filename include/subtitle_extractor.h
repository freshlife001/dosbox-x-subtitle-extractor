#pragma once

#include "gamelink_interface.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <map>

namespace Subtitle {

// ============================================================================
// 字幕数据结构
// ============================================================================

struct SubtitleData {
    std::string text;           // 字幕文本
    uint32_t duration_ms;       // 显示时长 (毫秒)
    uint64_t timestamp_ms;      // 时间戳
    bool changed;               // 是否有变化
};

// ============================================================================
// 代码页转换
// ============================================================================

enum class CodePage {
    CP437,      // 美国英文 (DOS)
    CP850,      // 西欧
    CP852,      // 中欧
    CP866,      // 西里尔文
    CP932,      // 日文
    CP936,      // 简体中文
    CP949,      // 韩文
    UTF8        // UTF-8
};

class CodePageConverter {
public:
    /// 转换字符串编码
    /// @param data 原始数据
    /// @param length 数据长度
    /// @param from 源代码页
    /// @param to 目标代码页
    /// @return 转换后的字符串
    static std::string Convert(
        const uint8_t* data,
        size_t length,
        CodePage from = CodePage::CP437,
        CodePage to = CodePage::UTF8
    );
    
private:
    static std::string CP437ToUTF8(const uint8_t* data, size_t length);
    static std::string CP936ToUTF8(const uint8_t* data, size_t length);
    // ... 其他代码页转换函数
};

// ============================================================================
// 字幕提取器
// ============================================================================

class SubtitleExtractor {
public:
    SubtitleExtractor();
    ~SubtitleExtractor();
    
    /// 初始化字幕提取器
    /// @return 成功返回 true
    bool Initialize();
    
    /// 从配置文件加载游戏配置
    /// @param config_path 配置文件路径
    /// @return 成功返回 true
    bool LoadGameConfig(const std::string& config_path);
    
    /// 设置要监控的内存地址
    /// @param text_addr 字幕文本地址
    /// @param duration_addr 字幕时长地址 (可选)
    /// @param status_addr 字幕状态地址 (可选)
    void SetMonitorAddresses(
        uint32_t text_addr,
        uint32_t duration_addr = 0,
        uint32_t status_addr = 0
    );
    
    /// 提取当前字幕
    /// @return SubtitleData 结构
    SubtitleData ExtractSubtitle();
    
    /// 设置代码页
    void SetEncoding(CodePage from, CodePage to);
    
    /// 获取游戏名称
    std::string GetGameName() const;
    
    /// 清理资源
    void Shutdown();
    
private:
    std::unique_ptr<GameLink::GameLinkInterface> m_gameLink;
    
    struct Config {
        std::string game_name;
        uint32_t text_address;
        uint32_t duration_address;
        uint32_t status_address;
        size_t max_text_length;
        uint8_t text_terminator;
        CodePage codepage_from;
        CodePage codepage_to;
    } m_config;
    
    SubtitleData m_lastSubtitle;
    
    std::string ExtractTextFromMemory(
        const std::vector<uint8_t>& data,
        size_t max_length
    );
};

} // namespace Subtitle
