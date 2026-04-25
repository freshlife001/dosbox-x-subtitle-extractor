#include "../include/subtitle_extractor.h"
#include <iostream>
#include <fstream>
#include <algorithm>

namespace Subtitle {

// ============================================================================
// 代码页转换实现
// ============================================================================

std::string CodePageConverter::Convert(
    const uint8_t* data,
    size_t length,
    CodePage from,
    CodePage to
) {
    if (from == CodePage::UTF8) {
        return std::string(reinterpret_cast<const char*>(data), length);
    }
    
    switch (from) {
        case CodePage::CP437:
            return CP437ToUTF8(data, length);
        case CodePage::CP936:
            return CP936ToUTF8(data, length);
        default:
            // Fallback: 直接转换
            return std::string(reinterpret_cast<const char*>(data), length);
    }
}

// CP437 到 UTF-8 的转换表 (部分)
std::string CodePageConverter::CP437ToUTF8(const uint8_t* data, size_t length) {
    std::string result;
    result.reserve(length * 2);
    
    for (size_t i = 0; i < length; ++i) {
        uint8_t ch = data[i];
        
        if (ch == 0) break;  // 零终止符
        
        if (ch < 128) {
            // ASCII
            result += static_cast<char>(ch);
        } else {
            // DOS 特殊字符映射到 UTF-8
            // 这里是部分常用字符映射
            switch (ch) {
                case 0x80: result += "\xc4\x80"; break;  // Ā
                case 0x81: result += "\xc3\xa1"; break;  // á
                case 0x82: result += "\xc3\xa0"; break;  // à
                case 0x83: result += "\xc3\xa2"; break;  // â
                // ... 更多映射 ...
                default:
                    // Fallback: 使用 Latin-1 编码
                    if (ch >= 0x80) {
                        result += static_cast<char>(0xc0 | (ch >> 6));
                        result += static_cast<char>(0x80 | (ch & 0x3f));
                    } else {
                        result += static_cast<char>(ch);
                    }
            }
        }
    }
    
    return result;
}

std::string CodePageConverter::CP936ToUTF8(const uint8_t* data, size_t length) {
    // GBK/GB2312 到 UTF-8 转换 (简化实现)
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        uint8_t ch = data[i];
        
        if (ch == 0) break;
        
        if (ch < 128) {
            result += static_cast<char>(ch);
        } else if (i + 1 < length) {
            // 双字节字符
            uint16_t code = (static_cast<uint16_t>(ch) << 8) | data[i + 1];
            
            // 简单的 GBK 到 UTF-8 转换逻辑
            // 实际应该使用专业的转换库
            // 这里只是示意
            result += static_cast<char>(ch);
            result += static_cast<char>(data[++i]);
        }
    }
    
    return result;
}

// ============================================================================
// 字幕提取器实现
// ============================================================================

SubtitleExtractor::SubtitleExtractor()
    : m_gameLink(std::make_unique<GameLink::GameLinkInterface>()),
      m_config{}
{
    m_config.max_text_length = 256;
    m_config.text_terminator = 0x00;
    m_config.codepage_from = CodePage::CP437;
    m_config.codepage_to = CodePage::UTF8;
}

SubtitleExtractor::~SubtitleExtractor() {
    Shutdown();
}

bool SubtitleExtractor::Initialize() {
    if (!m_gameLink->Initialize()) {
        std::cerr << "Failed to initialize Game Link" << std::endl;
        return false;
    }
    return true;
}

bool SubtitleExtractor::LoadGameConfig(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "Failed to load game config: " << config_path << std::endl;
        return false;
    }
    
    // 简单的 INI 解析
    std::string line;
    std::string section;
    
    while (std::getline(file, line)) {
        // 移除空格
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        
        if (line[0] == '[') {
            section = line.substr(1, line.rfind(']') - 1);
        } else {
            size_t eq_pos = line.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = line.substr(0, eq_pos);
                std::string value = line.substr(eq_pos + 1);
                
                // 移除空格
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                
                if (section == "Game Info") {
                    if (key == "Title") m_config.game_name = value;
                } else if (section == "Subtitle Addresses") {
                    if (key == "text_address") {
                        m_config.text_address = std::stoul(value, nullptr, 0);
                    } else if (key == "duration_address") {
                        m_config.duration_address = std::stoul(value, nullptr, 0);
                    } else if (key == "status_address") {
                        m_config.status_address = std::stoul(value, nullptr, 0);
                    }
                } else if (section == "Subtitle Format") {
                    if (key == "max_length") {
                        m_config.max_text_length = std::stoul(value);
                    } else if (key == "terminator") {
                        m_config.text_terminator = std::stoul(value, nullptr, 0);
                    }
                }
            }
        }
    }
    
    file.close();
    
    std::cout << "Game config loaded: " << m_config.game_name << std::endl;
    std::cout << "  Text address: 0x" << std::hex << m_config.text_address << std::endl;
    
    return true;
}

void SubtitleExtractor::SetMonitorAddresses(
    uint32_t text_addr,
    uint32_t duration_addr,
    uint32_t status_addr
) {
    m_config.text_address = text_addr;
    m_config.duration_address = duration_addr;
    m_config.status_address = status_addr;
    
    m_gameLink->ClearMonitorAddresses();
    m_gameLink->AddMonitorAddress(text_addr);
    if (duration_addr) m_gameLink->AddMonitorAddress(duration_addr);
    if (status_addr) m_gameLink->AddMonitorAddress(status_addr);
}

SubtitleData SubtitleExtractor::ExtractSubtitle() {
    SubtitleData result;
    result.changed = false;
    result.duration_ms = 0;
    result.timestamp_ms = 0;
    
    if (!m_gameLink->IsConnected()) {
        return result;
    }
    
    auto data = m_gameLink->ReadMonitoredData();
    if (data.empty()) {
        return result;
    }
    
    // 解析文本
    if (data.size() > 0) {
        std::string text = ExtractTextFromMemory(data, m_config.max_text_length);
        
        if (text != m_lastSubtitle.text) {
            result.text = text;
            result.changed = true;
            m_lastSubtitle.text = text;
        }
    }
    
    // 解析时长
    if (m_config.duration_address && data.size() > 1) {
        uint32_t duration = *(reinterpret_cast<const uint32_t*>(data.data() + 1));
        if (duration != m_lastSubtitle.duration_ms) {
            result.duration_ms = duration;
            result.changed = true;
            m_lastSubtitle.duration_ms = duration;
        }
    }
    
    result.text = CodePageConverter::Convert(
        reinterpret_cast<const uint8_t*>(result.text.data()),
        result.text.size(),
        m_config.codepage_from,
        m_config.codepage_to
    );
    
    return result;
}

void SubtitleExtractor::SetEncoding(CodePage from, CodePage to) {
    m_config.codepage_from = from;
    m_config.codepage_to = to;
}

std::string SubtitleExtractor::GetGameName() const {
    return m_config.game_name.empty() ? m_gameLink->GetGameName() : m_config.game_name;
}

void SubtitleExtractor::Shutdown() {
    if (m_gameLink) {
        m_gameLink->Shutdown();
    }
}

std::string SubtitleExtractor::ExtractTextFromMemory(
    const std::vector<uint8_t>& data,
    size_t max_length
) {
    std::string result;
    result.reserve(max_length);
    
    for (size_t i = 0; i < data.size() && i < max_length; ++i) {
        if (data[i] == m_config.text_terminator) break;
        result += static_cast<char>(data[i]);
    }
    
    return result;
}

}  // namespace Subtitle
